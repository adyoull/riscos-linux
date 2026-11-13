/* NativeSupport: RISC OS for Linux running natively.

   The port's ROM is a Linux program for a 32-bit ARM. Where the processor
   runs 32-bit programs (a Raspberry Pi 4 or 5 with a 64-bit Linux), it
   runs straight on it, with no QEMU. The test bed's QEMU (qemu/) adds what
   the port needs beyond that; this module does the same natively:

   - Privileged instructions in code loaded from disc (modules) and in the
     ROM's sound modules, never converted to the port's PVOps. In Linux
     user mode MRS CPSR reads user mode and MSR CPSR_c and CPS are ignored,
     so these are patched (Service_ModulePreInit, before a module's
     initialisation; the ROM's when this module starts) to undefined
     instructions (UDF #n, n indexing a table of the originals), and the
     SIGILL handler does them as the port's own code does: through its
     SetCPSR_c(fs)_unstack routines (mode change, banked sp and lr, pending
     interrupts). The others such code uses - MRS/MSR SPSR, LDM/STM ^,
     exception returns, CP15 reads - are undefined in user mode anyway and
     are done the same way when they trap.
   - The ROM's DynArea_PMP_PhysOp, _LogOp, _Resize and _GetPages are stubs
     (GCCSDK's ARMEABISupport 1.08 needs them): they jump to kpmp_* here,
     which keep each physical memory pool in a memfd. (From RISC OS 5.31-3
     the kernel does these itself, in c.LinuxC; its ROM has no stubs, so
     find_kpmp_stubs finds none and this is left to the kernel.)
   - The SWI vector (OS_ClaimProcessorVector): OS_Memory 0 for application
     space and RISC OS's workspace (the port refuses; SoundDMA and
     ARMEABISupport 1.08 ask), and SWIs NativeSupport_PMPOp/AbortReturn
     (&5AF40/&5AF41, NativeSupport's chunk) and OS_DynamicArea 22
     for the test bed's ARMEABISupport (lite). (Not SIGSYS: the ROM's own
     SIGSYS handler must be the one Linux calls, as its interrupt handler
     holds interrupts back while the program counter is in it.)
   - New code: every code synchronisation (a module loaded, a program
     started, BASIC's assembler...) ends in the kernel's Linux cacheflush
     call, which is patched to look at the range first (anywhere but the
     port's own memory; a whole-cache synchronisation, the RMA only).
   - SIGSEGV/SIGBUS: IOC timer 0 at &3200040 (AcornSSL reads it for
     entropy), the lite ARMEABISupport's abort handlers, and the fault
     status that CP15 c5 (DFSR) reads.
   - IXFS's directory reads: getdents64 without "." and ".." (it counts
     them though it hides them, so OS_GBPB offsets had gaps).

   Added since:
   - MRS CPSR without a trap: a branch to a stub reading the vcpu's mode
     byte (mrs_stub). LDM/STM ^ of r13/r14 next to privileged code (or an
     MRS stub, or a PSR site patched already) is patched too: the
     Cortex-A78C/X1C run it as a plain LDM/STM instead of trapping.
   - Exception returns to Thumb code (Iris's JavaScript); SETEND; CP15
     barriers (DSB/DMB/ISB) in any mode, rewritten in application space to
     the ARMv7 instructions so they stop trapping.
   - Squeezed AIF programs: left until they have unsqueezed themselves,
     then patched at the next whole-cache synchronisation.
   - Faults: the ROM's abort register dump no longer loops on a bad user
     stack (KIND_ABTDUMP); fault reports unblock SIGSEGV/SIGBUS around
     themselves, so a crashing program ends instead of the machine.
   - Code synchronised in ARMEABISupport's mmap areas (JavaScriptCore's
     Thumb JIT code) is left alone. Reporter's SWIs are ignored when
     Reporter isn't loaded (ARMEABISupport 1.05 reports aborts it doesn't
     handle with Report_TextS, text inline).
   - Reports: HardDisc4.NativeDebug turns on the fault reports (fault,
     fault-ev, fault-wild, fault-mode...); *NativeSupport lists what was
     patched and handled, with the busiest patched sites.

   Under QEMU (which does all this) the module does nothing.

   Signal handlers: each of SIGILL, SIGSEGV and SIGBUS gets one here, with
   the flags, mask (the interrupt signals blocked) and restorer the ROM's
   has; what isn't done here goes on to the ROM's. Linux system calls go
   through the port's __PVirt_LinuxSyscall (the only SWI 0 its seccomp
   filter lets through), which is the instruction after its signal_exit,
   the handlers' restorer.

   NativeSupport$Debug set when it starts: what it does, on Linux's stderr.
   NativeSupport$NoStubs: MRS through SIGILL too (to compare).
   NativeSupport$NoFastSWI: SWIs through seccomp and SIGSYS, not the fast
   entry of riscos-linux's kernels (*NativeSupport FastSWI On|Off too).

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "cmodule.h"
typedef unsigned char uint8_t;

/* NativeSupport's SWI chunk (allocated by RISC OS Open, 7 Nov 2026; until
   then &C0002 and &C0003). The same numbers in the test bed's QEMU and the
   lite ARMEABISupport. */
#define NS_SWI_CHUNK 0x5AF40u
#define NS_SWI_PMPOP 0x5AF40u
#define NS_SWI_ABORTRETURN 0x5AF41u

#define IXSupport_LinuxSyscall	0x59EC0
#define OS_ReadVarVal		0x23
#define OS_SynchroniseCodeAreas	0x6E

/* The port's ROM (5.29, Git 385115bc): the HAL and its PVOps routines
   (checked when the module starts; nothing is emulated if they differ) */
#define ROM_LO			0x20000000u
#define ROM_HI			0x20C00000u
#define SETCPSR_FS_UNSTACK	0x208007fcu	/* msr CPSR_fs, r1; then... */
#define SETCPSR_C_UNSTACK	0x20800800u	/* mrs r3, CPSR; add ip, lr, #4 */
#define LOADUSRREGISTERS	0x2080071cu	/* mov r9, lr; bl GetVCPU */
#define SETCPSR_TCHECK		0x20800860u	/* strne r0, [r0, -r0] */
#define SYSHEAP_BASE		0x20C00000u

/* struct vcpu (the port's HAL h/header): its address is the TLS value */
#define VCPU_CPSR		13	/* byte: mode, I, F */
#define VCPU_DFAR		88

/* Linux (ARM EABI) */
#define SYS_read		3
#define SYS_open		5
#define SYS_close		6
#define SYS_ftruncate		93
#define SYS_mprotect		125
#define SYS_munmap		91
#define SYS_prctl		172
#define SYS_rt_sigaction	174
#define SYS_rt_sigprocmask	175
#define SYS_mmap2		192
#define SYS_getdents64		217
#define SYS_clock_gettime	263
#define SYS_fallocate		352
#define SYS_memfd_create	385
#define SIGILL_			4
#define SIGBUS_			7
#define SIGSEGV_		11
#define SIGSYS_			31

struct ksigaction { unsigned handler, flags, restorer, mask[2]; };
typedef void sighandler (int, void *, void *);

/* siginfo: si_addr (faults) and si_call_addr (SIGSYS) at +12 */
#define SI_CODE(si)	(((int *) (si))[2])
#define SI_ADDR(si)	(((unsigned *) (si))[3])
/* ucontext: uc_mcontext at +20: trap_no, error_code, oldmask, r0..r15,
   cpsr, fault_address */
#define UC_REGS(uc)	((unsigned *) ((char *) (uc) + 32))
#define UC_ERROR(uc)	(((unsigned *) (uc))[6])
#define CPSR_V		0x10000000u
#define CPSR_C		0x20000000u
#define CPSR_T		0x20u
/* ITSTATE (Thumb-2 IT blocks): bits 26:25 and 15:10 */
#define CPSR_IT		0x0600fc00u
/* what MSR CPSR_fs sets: N Z C V Q, GE */
#define CPSR_APSR	0xf80f0000u

static struct
{
  int active;			/* natively, and started */
  unsigned sys_entry;		/* __PVirt_LinuxSyscall */
  unsigned swi_handler;		/* the ROM's SIGSYS handler */
  int fastswi;			/* 1: on; 0: off; -1: not in this kernel */
  int fastfpa;			/* the same, for the fast FPA entry */
  int psr_ok, lur_ok, abtdump_ok, daaddr_ok;		/* the ROM's routines are where expected */
  struct ksigaction old_ill, old_segv, old_bus;
  unsigned midr;
  unsigned last_dfar, last_dfsr;
  unsigned sctlr;
  /* code ranges in the ROM emulated as disc code is (the sound modules) */
  unsigned emul_lo[8], emul_hi[8];
  int emul_n;
  /* counts, for *NativeSupport */
  unsigned cond_skipped, cond_nopped, lur_thumb, thumb_returns, squeezed_skipped, lur_stubbed;
  unsigned it_returns;		/* Thumb returns inside an IT block (full) */
  unsigned sync_app_data, sync_app_prog, sync_app_code, unpatched_words, unpatched_writes, sync_own, sync_big, sync_too_big;
  unsigned cp15_barriers, cp15_rewritten, abtdump_sw;
  unsigned da_low, da_high, da_over;
  int tchk_ok;
  unsigned patched, patched_rom, patched_sync, syncs, full_syncs, flush_sites, stubbed, full, probe_faults, emul_aborts, in_full, imb_full, emul_psr, emul_priv, getdents, mem0, mem24,
    pmp_swis, ioc, aborts, kpmp_ops, reports, sync_jit, aeabi105;
} ns;


/* The rest is in the files below, in this order: one translation unit, as
   when it was one file (the parts share their static data, and the compiler
   inlines across them). Each says at its top what it is for. scan.c and
   sites.c (and privform.c) are also built into tools/nsscan, which runs the scanner over
   programs and modules on the build machine. */
#include "sys.c"
#include "sites.c"
#include "scan.c"
#include "psr.c"
#include "modes.c"
#include "aeabi.c"
#include "osmemory.c"
#include "sync.c"
#include "faults.c"
#include "kpmp.c"
#include "start.c"
#include "swistubs.c"
