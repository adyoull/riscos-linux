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
  unsigned cond_skipped, lur_thumb, thumb_returns, squeezed_skipped, lur_stubbed;
  unsigned it_returns;		/* Thumb returns inside an IT block (full) */
  unsigned sync_app_data, sync_app_prog, sync_app_code, unpatched_words, unpatched_writes, sync_own, sync_big, sync_too_big;
  unsigned cp15_barriers, cp15_rewritten, abtdump_sw;
  unsigned da_low, da_high, da_over;
  int tchk_ok;
  unsigned patched, patched_rom, patched_sync, syncs, full_syncs, flush_sites, stubbed, full, probe_faults, in_full, imb_full, emul_psr, emul_priv, getdents, mem0, mem24,
    pmp_swis, ioc, aborts, kpmp_ops, reports, sync_jit, aeabi105;
} ns;

/* ------------------------------------------------------------------------
   Linux system calls, through the port's own SWI 0 */
static int
lsys (unsigned nr, unsigned a, unsigned b, unsigned c, unsigned d,
      unsigned e, unsigned f)
{
  register unsigned r0 asm ("r0") = a;
  register unsigned r1 asm ("r1") = b;
  register unsigned r2 asm ("r2") = c;
  register unsigned r3 asm ("r3") = d;
  register unsigned r4 asm ("r4") = e;
  register unsigned r5 asm ("r5") = f;
  register unsigned r7 asm ("r7") = nr;
  register unsigned ip asm ("ip") = ns.sys_entry;
  asm volatile ("blx	ip"
		: "+r" (r0), "+r" (ip)
		: "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5), "r" (r7)
		: "lr", "memory", "cc");
  return (int) r0;
}
#define lsys3(n, a, b, c) lsys ((n), (a), (b), (c), 0, 0, 0)

/* The same, before the entry is known: IXSupport_LinuxSyscall */
static int
lsys_swi (unsigned nr, unsigned a, unsigned b, unsigned c, unsigned d)
{
  regs r = {{ a, b, c, d, 0, 0, 0, nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;
  return (int) r.r[0];
}

static unsigned
get_tls (void)
{
  unsigned v;
  asm volatile ("mrc	p15, 0, %0, c13, c0, 3" : "=r" (v));
  return v;
}

static inline unsigned
ror32 (unsigned v, unsigned s)
{
  s &= 31;
  return s ? (v >> s) | (v << (32 - s)) : v;
}

static int
popcount16 (unsigned v)
{
  int n = 0;
  for (v &= 0xffff; v; v &= v - 1)
    n++;
  return n;
}

static int
cond_pass (unsigned insn, unsigned cpsr)
{
  int n = cpsr >> 31 & 1, z = cpsr >> 30 & 1, c = cpsr >> 29 & 1,
    v = cpsr >> 28 & 1;
  switch (insn >> 28)
    {
    case 0: return z;
    case 1: return !z;
    case 2: return c;
    case 3: return !c;
    case 4: return n;
    case 5: return !n;
    case 6: return v;
    case 7: return !v;
    case 8: return c && !z;
    case 9: return !c || z;
    case 10: return n == v;
    case 11: return n != v;
    case 12: return !z && n == v;
    case 13: return z || n != v;
    default: return 1;
    }
}

/* Code that is the port's own (its ROM), unless one of the ROM modules
   emulated as disc code is */
static int
pc_emulated (unsigned pc)
{
  if (pc < ROM_LO || pc >= ROM_HI)
    return 1;
  for (int i = 0; i < ns.emul_n; i++)
    if (pc >= ns.emul_lo[i] && pc < ns.emul_hi[i])
      return 1;
  return 0;
}

/* ------------------------------------------------------------------------
   The patch table: UDF #(UDF_BASE + i) stands for sites[i].orig at
   sites[i].addr. */
#define SITES_MAX	16384
#define UDF_BASE	0x1000
#define KIND_PSR	1	/* emulated: MRS/MSR CPSR, CPS, MRS/MSR SPSR */
#define KIND_GETDENTS	2	/* BL __PVirt_LinuxSyscall for getdents64 */
#define KIND_CACHEFLUSH	3	/* BL __PVirt_LinuxSyscall for cacheflush */
#define KIND_FULL_BEGIN	4	/* the kernel's do_IMB_Full: mov r1, #0 */
#define KIND_FULL_END	5	/* and its ldmfd sp!, {r0-r3, ip, pc} */
#define KIND_LUR	6	/* LoadUsrRegisters' first instruction */
#define KIND_ABTDUMP	7	/* the abort register dump's switch back to ABT */
#define KIND_DAADDR	8	/* AllocateAreaAddress's mmap2: where a new area goes */
/* The kernel's abort handling copies the aborted mode's r8-r14 by
   switching to that mode (SYS for a program), then back to ABT the port's
   way: push {r0-r3, ip, lr}; mov r1, #&D7; bl SetCPSR_c_unstack; msr. That
   push is on the program's stack: if its sp is what went wrong (a stack
   overflow, a crash with sp junk), it faults inside the abort handling,
   which starts again, until the ABT stack runs out (NetSurf, POV-Ray). Done
   here instead, without a stack. */
static unsigned ABTDUMP_SW;	/* found by find_kernel_sites: 5.29 &20824624, 5.31 &208248B0 */
static unsigned DAADDR_BL;	/* BL __PVirt_LinuxSyscall (mmap2 NULL, max): 5.29 &20817DAC, 5.31 &20817F30 */
static unsigned IMB_FULL;	/* do_IMB_Full: 5.29 &208169E0, 5.31 &20816A8C */

struct site { unsigned addr, orig, kind; };
static struct site *sites;
/* per site: how often it was hit, and where last (*NativeSupport lists the
   busiest); and traps that weren't sites, by pc */
static unsigned *site_hits;
struct other_trap { unsigned pc, insn, n; };
#define OTHER_N 32
static struct other_trap other_traps[OTHER_N];
/* and traps of instructions whose condition failed (skipped), by pc: some
   processors trap these, and each costs a signal */
static struct other_trap skip_traps[OTHER_N];
static unsigned reset_cs;	/* when the counts started (monotonic cs) */
static unsigned mono_cs (void);

static void
trap_count (struct other_trap *t, unsigned pc, unsigned w)
{
  int k, e = -1;
  for (k = 0; k < OTHER_N; k++)
    if (t[k].pc == pc)
      break;
    else if (e < 0 && !t[k].n)
      e = k;
  if (k == OTHER_N && e >= 0)
    k = e;
  else if (k == OTHER_N)
    {
      /* full: the new place takes the least-counted one's slot and count
	 (the "space-saving" count: a busy place always gets in, its count
	 at most that much over) */
      k = 0;
      for (int j = 1; j < OTHER_N; j++)
	if (t[j].n < t[k].n)
	  k = j;
    }
  t[k].pc = pc;
  t[k].insn = w;
  t[k].n++;
}
static unsigned nsites;

static unsigned
udf (unsigned imm)
{
  return 0xe7f000f0u | ((imm >> 4) << 8) | (imm & 15);
}

static int
udf_imm (unsigned w)
{
  if ((w & 0xfff000f0u) != 0xe7f000f0u)
    return -1;
  return (int) (((w >> 8) & 0xfff) << 4 | (w & 15));
}

/* The site for UDF #n at pc. A PSR instruction doesn't depend on where it
   is, so a copy of patched code (a module copying a routine into its
   workspace) works too; the others are where they were patched. */
static struct site *
site_at (unsigned pc, unsigned w)
{
  int imm = udf_imm (w);
  if (imm < UDF_BASE || (unsigned) (imm - UDF_BASE) >= nsites)
    return NULL;
  struct site *s = &sites[imm - UDF_BASE];
  return s->addr == pc || s->kind == KIND_PSR ? s : NULL;
}

/* (Sites aren't reused: application space's belong to tasks that may be
   paged out. 16384 of them is far more than a desktop's.) */
static int debug, debug_faults;
/* who synchronised application space (sync_patch, *NativeSupport) */
struct syncwho { unsigned caller, cao, lo, hi, n, sites, s_addr, s_orig; char what; };
#define SYNCWHO_MAX 48
static struct syncwho syncwho[SYNCWHO_MAX];
static unsigned nsyncwho, syncwho_lost, syncwho_logged;
static int syncwho_cur = -1;	/* the row whose sync is being patched */
static unsigned app_sites;	/* sites made in application space (ever) */
static void dbg (const char *what, unsigned a, unsigned b, unsigned c);
static int
site_add (unsigned *p, unsigned kind)
{
  unsigned i = nsites;
  if (i == SITES_MAX)
    {
      ns.full++;
      return 0;
    }
  nsites++;
  {
    static int app_logged;
    if (debug_faults && (unsigned) p < 0x20000000u && app_logged < 80)
      {
	app_logged++;
	dbg ("fault-site", (unsigned) p, *p, kind);
      }
  }
  if ((unsigned) p < 0x20000000u)
    app_sites++;
  if (syncwho_cur >= 0 && (unsigned) p < 0x20000000u)
    {
      struct syncwho *w = &syncwho[syncwho_cur];
      if (!w->sites++)
	w->s_addr = (unsigned) p, w->s_orig = *p;
    }
  sites[i].addr = (unsigned) p;
  sites[i].orig = *p;
  sites[i].kind = kind;
  *p = udf (UDF_BASE + i);
  return 1;
}

/* A real processor mode in bits 0-4 */
static int
valid_mode (unsigned m)
{
  m &= 0x1f;
  return m == 0x10 || m == 0x11 || m == 0x12 || m == 0x13 || m == 0x17
	 || m == 0x1b || m == 0x1f;
}

/* An instruction to patch? Only exact encodings (20 bits or more fixed),
   so that data is unlikely to match:
     always executed (condition AL): MRS CPSR/SPSR, MSR CPSR_c (register,
       or an unrotated constant that sets a real mode), MSR SPSR (register);
     conditional MRS/MSR CPSR: only next to an AL instruction (a table of
       numbers can look like them: 8.9375 as a float is MRSMI r0, CPSR),
       or a few instructions after TEQ pc, pc (the C veneers' test), or
       next to another with the same condition;
     CPS (always unconditional).
   MSR SPSR with a constant fixes too few bits; like the other privileged
   instructions, it's undefined in user mode, and done when it traps. */
#define STUBS_SIZE	0x10000
#define LOWSTUBS	0x2000u
#define LOWSTUBS_SIZE	0x1000u
static unsigned *stubs, nstub, nlow;

/* a branch to an MRS stub (an MRS patched already, earlier in the same
   scan: UnixLib's __setup_signalhandler_stack has "mrseq r1, cpsr" two
   words before its "ldmne r0, {sp}^") */
static int
stub_branch (const unsigned *q)
{
  unsigned x = *q;
  if ((x & 0x0f000000u) != 0x0a000000u)
    return 0;
  unsigned to = (unsigned) q + 8 + ((int) (x << 8) >> 6);
  return (stubs && to - (unsigned) stubs < STUBS_SIZE)
	 || to - LOWSTUBS < LOWSTUBS_SIZE;
}

static unsigned veneer_psr;	/* sites found by the TEQ pc, pc rule */
static int debug, debug_faults;
static void dbg (const char *what, unsigned a, unsigned b, unsigned c);
static int
psr_insn (const unsigned *p, const unsigned *lo, const unsigned *hi)
{
  unsigned w = *p, cond = w >> 28;
  if ((w & 0xfff1fe20u) == 0xf1000000u)			/* CPS */
    {
      /* (next to an ordinary AL instruction, as real code is: data
	 matches CPS easily) */
      if (!((w & 0x000e0000u) != 0 && (!(w & 0x20000u) || valid_mode (w))))
	return 0;
      for (int d = -2; d <= 2; d++)
	if (d && p + d >= lo && p + d < hi && (p[d] >> 28) == 14)
	  return 1;
      return 0;
    }
  if (cond == 15)
    return 0;
  int cpsr = 0, spsr = 0;
  if ((w & 0x0fff0fffu) == 0x010f0000u)			/* MRS Rd, CPSR */
    cpsr = ((w >> 12) & 15) != 15;
  else if ((w & 0x0ff0fff0u) == 0x0120f000u)		/* MSR CPSR_c.., Rm */
    cpsr = (w >> 16) & 1;
  else if ((w & 0x0ff0f000u) == 0x0320f000u)		/* MSR CPSR_c.., #i */
    cpsr = ((w >> 16) & 1) && !(w & 0xf00) && valid_mode (w);
  else if ((w & 0x0fff0fffu) == 0x014f0000u)		/* MRS Rd, SPSR */
    spsr = ((w >> 12) & 15) != 15;
  else if ((w & 0x0ff0fff0u) == 0x0160f000u)		/* MSR SPSR_.., Rm */
    spsr = ((w >> 16) & 15) != 0;
  if (cond == 14)
    return cpsr || spsr;
  if (!cpsr)
    return 0;
  /* Thumb-2 code read as ARM words: a 32-bit branch (B<c>.W, B.W, BL:
     first halfword F000-F7FF, second with its top bit set) is a
     conditional MSR CPSR, Rm to the rules below when its offset is
     right. Iris's JavaScriptCore has two in its interpreter (LLInt, in
     the shared library): "beq.w" after a call's slow path, F000 812D,
     is MSRHI CPSR_fsc, r0. Patched to a UDF, the Thumb code ran two
     other instructions instead and went to "throw" with no exception
     pending: the web process aborted (bbc.co.uk). Real ARM code doesn't
     write the CPSR under HI, LS, GE, LT or LE. */
  if ((w & 0xf800u) == 0xf000u && (w >> 31))
    return 0;
  for (int d = -2; d <= 2; d++)
    if (d && p + d >= lo && p + d < hi && (p[d] >> 28) == 14)
      return 1;
  /* the C veneers' (CMHG's) 26- or 32-bit test a few instructions before:
       teq pc, pc ... mrseq r6, CPSR; biceq r3, r6, #15; orreq r3, r3, #3
     with no AL instruction next to the MRS. Left alone, it reads the real
     (user) mode, and the veneer's way back "restores" user mode in the
     middle of an interrupt or event: NetTime's, every few seconds. */
  for (int d = 1; d <= 6; d++)
    if (p - d >= lo && p[-d] == 0xe13f000fu)
      {
	veneer_psr++;
	return 1;
      }
  /* or next to another conditional MRS/MSR CPSR with the same condition:
       mrseq r6, CPSR; msreq CPSR_c, r3; msreq CPSR_c, r6
     (a veneer built in the RMA at run time: two such encodings in a row
     are code, not data) */
  for (int d = -2; d <= 2; d++)
    if (d && p + d >= lo && p + d < hi && (p[d] >> 28) == cond
	&& ((p[d] & 0x0fff0fffu) == 0x010f0000u
	    /* (an MSR that sets the control field, as such a veneer's
	       does: a table in Iris's libwebkit2gtk .rodata has
	       &0128F006, &012FF007 side by side) */
	    || ((p[d] & 0x0ff0fff0u) == 0x0120f000u && (p[d] & 0x10000u))
	    || stub_branch (p + d)))
      {
	veneer_psr++;
	return 1;
      }
  /* or next to one of those patched already (earlier in this scan) */
  for (int d = -2; d <= -1; d++)
    {
      const struct site *t = p + d >= lo ? site_at ((unsigned) (p + d), p[d])
					 : NULL;
      if (t && t->kind == KIND_PSR && t->orig >> 28 == cond
	  && ((t->orig & 0x0fff0fffu) == 0x010f0000u
	      || (t->orig & 0x0ff0fff0u) == 0x0120f000u))
      {
	veneer_psr++;
	return 1;
      }
    }
  /* not patched (it may be data): with fault reports on, say where, so
     that one which is really code can be found */
  static int skipped_logged;
  if (debug_faults && skipped_logged < 60)
    {
      skipped_logged++;
      dbg ("fault-psr-left", (unsigned) p, w, p - lo);
    }
  return 0;
}

/* MRS Rd, CPSR without a signal: a branch (same condition) to a stub,
     mrc   p15, 0, Rd, c13, c0, 3	@ the vcpu (TLS)
     ldrb  Rd, [Rd, #13]		@ its CPSR byte: mode, I, F
     orrmi Rd, Rd, #&80000000		@ and the flags
     orreq Rd, Rd, #&40000000
     orrcs Rd, Rd, #&20000000
     orrvs Rd, Rd, #&10000000
     b     <the instruction after>
   Nothing else is changed and no memory is used (sp needn't be a stack:
   it isn't always where MRS is). Q and GE read as 0: code saving the
   interrupt state with MRS and putting it back with MSR CPSR_c, as such
   code does, doesn't see them.
   The stubs are in the RMA, within a branch's reach of the RMA's modules,
   and in a page mapped at &2000 (the port leaves &1000-&2FFF unmapped, as
   a guard: &1000-&1FFF still is), within reach of application space's
   first 32MB. A stub is shared by sites at the same address with the same
   register (tasks running the same library). Other code gets UDF. */

static int nostubs, traptest, nosyncpatch, skipsqueezed = 1;

static unsigned
branch_to (unsigned from, unsigned to, unsigned cond)
{
  int off = (int) (to - (from + 8));
  if (off < -0x2000000 || off >= 0x2000000)
    return 0;
  return cond << 28 | 0x0a000000u | (((unsigned) off >> 2) & 0xffffff);
}

/* LoadUsrRegisters' Thumb fix (see KIND_LUR in ill_body) without a
   signal: its first instruction (mov r9, lr) becomes a branch to a stub
   that does the fix in user-mode instructions:
     tst   r3, #&20			@ returning to Thumb code?
     bicne r3, r3, #&20		@ then without the T bit,
     ldrne r9, [r1, #60]		@ and with bit 0 of the pc set
     orrne r9, r9, #1
     strne r9, [r1, #60]
     mov   r9, lr			@ the instruction it replaces
     b     LoadUsrRegisters + 4
   (r9 and the flags are free here: the next instruction sets r9 and the
   routine ends by loading the PSR from r3.) The kernel returns to user
   mode through it, so it is called very often: as a trapping site it cost
   a signal each time (2.3 million in a RiscOSM map draw, about 10 s on a
   Pi 4). The stub goes in the ROM image's unused fill (&FF bytes) near
   its end, within a branch's reach; if there is none, the trap is used. */
static void sync_code (unsigned lo, unsigned hi);
static int rom_writable (unsigned lo, unsigned hi, int on);

static int
lur_stub (void)
{
  unsigned lur = LOADUSRREGISTERS;
  if (*(volatile unsigned *) lur != 0xe1a0900eu)	/* mov r9, lr */
    return 0;
  /* the highest 16 words of fill, below the last 1KB (signatures) */
  unsigned *slot = NULL;
  for (unsigned a = ROM_HI - 1024 - 64; a >= ROM_HI - 0x80000u; a -= 64)
    {
      unsigned *q = (unsigned *) a;
      int i = 0;
      while (i < 16 && q[i] == 0xffffffffu)
	i++;
      if (i == 16)
	{
	  slot = q;
	  break;
	}
    }
  if (!slot)
    return 0;
  unsigned to = branch_to (lur, (unsigned) slot, 14);
  unsigned back = branch_to ((unsigned) (slot + 9), lur + 4, 14);
  if (!to || !back)
    return 0;
  if (!rom_writable ((unsigned) slot, (unsigned) (slot + 10), 1))
    return 0;
  /* Inside an IT block (ITSTATE in r3) the T bit stays: the mode
     switch's T check then stops in fault_body, which returns through the
     signal frame (only that can set ITSTATE) */
  slot[0] = 0xe3130406u;	/* tst r3, #&06000000 */
  slot[1] = 0x03130cfcu;	/* tsteq r3, #&FC00 */
  slot[2] = 0x1a000004u;	/* bne slot+8 */
  slot[3] = 0xe3130020u;	/* tst r3, #&20 */
  slot[4] = 0x13c33020u;	/* bicne r3, r3, #&20 */
  slot[5] = 0x1591903cu;	/* ldrne r9, [r1, #60] */
  slot[6] = 0x13899001u;	/* orrne r9, r9, #1 */
  slot[7] = 0x1581903cu;	/* strne r9, [r1, #60] */
  slot[8] = 0xe1a0900eu;	/* mov r9, lr */
  slot[9] = back;
  rom_writable ((unsigned) slot, (unsigned) (slot + 10), 0);
  sync_code ((unsigned) slot, (unsigned) (slot + 10));
  if (!rom_writable (lur, lur + 4, 1))
    return 0;
  *(volatile unsigned *) lur = to;
  rom_writable (lur, lur + 4, 0);
  sync_code (lur, lur + 4);
  ns.lur_stubbed = 1;
  return 1;
}

static int
mrs_stub (unsigned *p)
{
  unsigned w = *p, rd = (w >> 12) & 15, cond = w >> 28;
  if (nostubs || rd == 13 || rd == 15)
    return 0;
  unsigned *s = NULL, *base, *n, max;
  if (stubs && branch_to ((unsigned) p, (unsigned) stubs, 14)
      && branch_to ((unsigned) p, (unsigned) stubs + STUBS_SIZE, 14))
    {
      base = stubs;
      n = &nstub;
      max = STUBS_SIZE / 4;
    }
  else if (nlow != ~0u && branch_to ((unsigned) p, LOWSTUBS, 14)
	   && branch_to ((unsigned) p, LOWSTUBS + LOWSTUBS_SIZE, 14))
    {
      base = (unsigned *) LOWSTUBS;
      n = &nlow;
      max = LOWSTUBS_SIZE / 4;
    }
  else
    return 0;
  /* one already for this site (another task's copy of the same code)? */
  for (unsigned i = 0; i + 7 <= *n; i += 7)
    if (base[i] == (0xee1d0f70u | rd << 12)
	&& base[i + 6] == branch_to ((unsigned) (base + i + 6), (unsigned) (p + 1), 14))
      {
	if (debug_faults && (unsigned) p < 0x20000000u)
	  dbg ("fault-site-mrs", (unsigned) p, *p, 0);
	*p = branch_to ((unsigned) p, (unsigned) (base + i), cond);
	ns.stubbed++;
	return 1;
      }
  if (*n + 7 > max)
    {
      ns.full++;
      return 0;
    }
  s = base + *n;
  unsigned br = branch_to ((unsigned) p, (unsigned) s, cond);
  unsigned back = branch_to ((unsigned) (s + 6), (unsigned) (p + 1), 14);
  if (!br || !back)
    return 0;
  int low = base == (unsigned *) LOWSTUBS;
  if (low)
    lsys3 (SYS_mprotect, LOWSTUBS, LOWSTUBS_SIZE, 7);
  s[0] = 0xee1d0f70u | rd << 12;			/* mrc (TLS) */
  s[1] = 0xe5d0000du | rd << 16 | rd << 12;		/* ldrb rd, [rd, #13] */
  s[2] = 0x43800102u | rd << 16 | rd << 12;		/* orrmi #&80000000 */
  s[3] = 0x03800101u | rd << 16 | rd << 12;		/* orreq #&40000000 */
  s[4] = 0x23800202u | rd << 16 | rd << 12;		/* orrcs #&20000000 */
  s[5] = 0x63800201u | rd << 16 | rd << 12;		/* orrvs #&10000000 */
  s[6] = back;
  if (low)
    lsys3 (SYS_mprotect, LOWSTUBS, LOWSTUBS_SIZE, 5);
  *n += 7;
  lsys3 (0xf0002, (unsigned) s, (unsigned) (s + 7), 0);	/* cacheflush */
  *p = br;
  ns.stubbed++;
  return 1;
}

/* LDM/STM ^ of the user registers (no pc), with r13 or r14 in the list:
   in user mode a processor may trap it (QEMU, the Cortex-A72) or just do
   a plain LDM/STM (the Cortex-A78C and X1C), which loads the real r13/r14
   instead of the user bank's: the C library's way back to a program
   ("ldmdb lr, {r0-lr}^; nop; ldm lr, {pc}^") then returns through the
   program's lr. So such code is patched, as the PSR instructions are -
   but these encodings fix few bits, so only next to privileged code. */
static unsigned da_place (unsigned *R);
static char pi_model[64];
static char da_name[32];
static char da_high[128];
static unsigned userbank_sites;

static int
priv_near (const unsigned *q)
{
  unsigned x = *q;
  if (stub_branch (q))
    return 1;
  /* (not LDM {..pc}^: it fixes too few bits - Thumb-2 code read as ARM
     words matches it, as Iris's JavaScript does) */
  return (x & 0x0fbf0fffu) == 0x010f0000u		/* MRS CPSR/SPSR */
	 || (x & 0x0fb0fff0u) == 0x0120f000u		/* MSR reg */
	 || (x & 0xffb0f000u) == 0xe320f000u		/* MSR imm (AL) */
	 || (x & 0x0fffffffu) == 0x01b0f00eu		/* MOVS pc, lr */
	 || (x & 0x0ffff000u) == 0x025ef000u		/* SUBS pc, lr, # */
	 || (x & 0x0f0fffffu) == 0x0f000016u		/* SWI (X)OS_EnterOS */
	 || udf_imm (x) >= UDF_BASE;			/* patched already */
}

static int
userbank_insn (const unsigned *p, const unsigned *lo, const unsigned *hi)
{
  unsigned w = *p;
  /* with r13 or r14 (the banked ones: for the others it makes no
     difference), conditional or not: UnixLib's thread switch has
     "ldmne r0, {sp}^" */
  if ((w >> 28) == 15 || (w & 0x0e400000u) != 0x08400000u
      || !(w & 0x6000u))
    return 0;
  if ((w & (1u << 20)) && (w & 0x8000u))
    return 0;				/* exception return: always traps */
  /* and the list one run of registers, without pc ({r0-lr}, {sp, lr},
     {sp}...: what such code saves and loads; Thumb-2 code read as ARM
     words has scattered lists) */
  unsigned list = w & 0xffffu;
  if ((w & (1u << 21)) || ((w >> 16) & 15) == 15 || (list & 0x8000u)
      || ((list + (list & -list)) & list))
    return 0;
  for (int d = -4; d <= 4; d++)
    if (d && p + d >= lo && p + d < hi && priv_near (p + d))
      {
	userbank_sites++;
	if (debug_faults && userbank_sites <= 60)
	  dbg ("fault-userbank", (unsigned) p, w, p[d]);
	return 1;
      }
  return 0;
}

static unsigned
patch_range (unsigned lo, unsigned hi)
{
  unsigned n = 0;
  unsigned *a = (unsigned *) (lo & ~3u), *b = (unsigned *) (hi & ~3u);
  for (unsigned *p = a; p < b; p++)
    if (userbank_insn (p, a, b))
      n += site_add (p, KIND_PSR);
    else if (psr_insn (p, a, b))
      {
	/* MRS rd, CPSR: a branch to a stub (no signal), but not in
	   application space: a program's image may be copied out as data
	   (CMHG's veneer templates in the DDE's ROM build), and a copy of
	   the branch can't be told from any other branch there; a UDF can,
	   and is put back as it's written to a file (unpatch_copies) */
	if ((*p & 0x0fff0fffu) == 0x010f0000u && (unsigned) p >= 0x20000000u
	    && mrs_stub (p))
	  n++;
	else if (site_add (p, KIND_PSR))
	  n++;
      }
  return n;
}

static void
sync_code (unsigned lo, unsigned hi)
{
  regs r = {{ 1, lo, hi }};
  swix_regs (OS_SynchroniseCodeAreas, &r);
}

/* Make ROM pages writable around [lo, hi) while patching them */
static int
rom_writable (unsigned lo, unsigned hi, int on)
{
  lo &= ~0xfffu;
  hi = (hi + 0xfff) & ~0xfffu;
  return lsys3 (SYS_mprotect, lo, hi - lo, on ? 7 : 5) == 0;
}

/* ------------------------------------------------------------------------
   MRS/MSR CPSR and CPS, through the port's SetCPSR routines (as the test
   bed's QEMU does: qemu/ patch item 4) */
static int
emulate_psr (unsigned *R, unsigned insn)
{
  unsigned pc = R[15], vcpu = get_tls (), sp;
  unsigned cpsr, vcpsr, val, mask, r1, fields, entry;
  if (!ns.psr_ok || !vcpu || (R[16] & CPSR_T))
    return 0;
  unsigned vmode = *(volatile uint8_t *) (vcpu + VCPU_CPSR);
  cpsr = R[16];
  vcpsr = (cpsr & ~0xffu) | vmode;

  if ((insn & 0x0fff0fffu) == 0x010f0000u)		/* MRS Rd, CPSR */
    {
      unsigned rd = (insn >> 12) & 15;
      if (rd == 15)
	return 0;
      R[rd] = vcpsr;
      R[15] = pc + 4;
      return 1;
    }
  if ((insn & 0x0ff0fff0u) == 0x0120f000u || (insn & 0x0ff0f000u) == 0x0320f000u)
    {
      fields = (insn >> 16) & 15;
      val = insn & (1u << 25) ? ror32 (insn & 0xff, ((insn >> 8) & 15) * 2)
			      : R[insn & 15];
    }
  else if ((insn & 0xfff1fe20u) == 0xf1000000u)		/* CPS */
    {
      unsigned imod = (insn >> 18) & 3;
      mask = val = 0;
      if (imod & 2)
	{
	  mask = insn & 0xc0;
	  if (imod & 1)
	    val = mask;
	}
      if (insn & (1u << 17))
	{
	  mask |= 0x1f;
	  val |= insn & 0x1f;
	}
      fields = 1;
      val = (vcpsr & ~mask) | val;
    }
  else
    return 0;
  if (!(fields & 1))
    {
      /* flags only (not patched, but here from a trap?): as the CPU does */
      if (fields & 8)
	R[16] = (R[16] & ~0xf8000000u) | (val & 0xf8000000u);
      R[15] = pc + 4;
      return 1;
    }
  if ((vmode & 0x1f) == 0x10)
    {
      /* USR mode: the control field can't be written (as on a real CPU);
	 MSR still sets the flags */
      if (fields & 8)
	R[16] = (R[16] & ~0xf8000000u) | (val & 0xf8000000u);
      R[15] = pc + 4;
      return 1;
    }
  r1 = val & 0xdf;					/* never Thumb */
  entry = SETCPSR_C_UNSTACK;
  if (fields & 8)
    {
      r1 |= (val & 0xff000000u) | ((fields & 4 ? val : cpsr) & 0x00ff0000u);
      entry = SETCPSR_FS_UNSTACK;
    }
  sp = R[13] - 24;
  unsigned *s = (unsigned *) sp;
  s[0] = R[0];
  s[1] = R[1];
  s[2] = R[2];
  s[3] = R[3];
  s[4] = R[12];
  s[5] = R[14];
  R[13] = sp;
  R[1] = r1;
  R[14] = pc;			/* the routine returns to lr + 4 */
  R[15] = entry;
  return 1;
}

static unsigned
bank (unsigned mode)
{
  switch (mode & 0x1f)
    {
    case 0x10: case 0x1f: return 28;
    case 0x11: return 52;
    case 0x12: return 40;
    case 0x17: return 76;
    case 0x1b: return 64;
    default: return 16;
    }
}

/* An exception return (LDM {..., pc}^, MOVS pc, lr...): r[0..14] the
   registers after the instruction; through the port's LoadUsrRegisters */
static int
exception_return (unsigned *R, unsigned vcpu, unsigned vmode,
		  const unsigned *r, unsigned newpc)
{
  if (!ns.lur_ok)
    return 0;
  unsigned b = vcpu + bank (vmode);
  unsigned spsr = *(unsigned *) (b + 8);
  unsigned usr13 = *(unsigned *) (vcpu + 28), usr14 = *(unsigned *) (vcpu + 32);
  unsigned blk = ((r[13] < R[13] ? r[13] : R[13]) - 256) & ~7u;
  unsigned *k = (unsigned *) blk;
  for (int i = 0; i < 13; i++)
    k[i] = r[i];
  /* back to Thumb code: an interworking load; but into the middle of an
     IT block (ARMEABISupport's abort handler mapping a thread's stack
     under a Thumb-2 load or store) the T bit stays, for the mode switch's
     T check: fault_body then returns through the signal frame, which
     puts ITSTATE back */
  int it = (spsr & 0x20) && (spsr & CPSR_IT);
  if ((spsr & 0x20) && !it)
    newpc |= 1;
  k[13] = usr13;
  k[14] = usr14;
  k[15] = newpc;
  R[1] = blk;
  R[2] = r[13];
  R[3] = it ? spsr : spsr & ~0x20u;
  R[14] = r[14];
  R[13] = blk;
  R[9] = r[14];			/* its first instruction (patched): mov r9, lr */
  R[15] = LOADUSRREGISTERS + 4;
  R[16] &= ~CPSR_T;
  return 1;
}

static int
emulate_priv (unsigned *R, unsigned insn)
{
  unsigned pc = R[15], vcpu = get_tls ();
  unsigned r[15];
  if ((R[16] & CPSR_T) || !vcpu)
    return 0;
  unsigned mode = *(volatile uint8_t *) (vcpu + VCPU_CPSR) & 0x1f;
  /* USR mode: really undefined, but for LDM/STM ^ of the user registers
     (patched: done as the plain LDM/STM it is there) */
  if (mode == 0x10
      && !((insn & 0x0e400000u) == 0x08400000u
	   && !((insn & (1u << 20)) && (insn & 0x8000u))))
    return 0;
  unsigned bk = vcpu + bank (mode);
  for (int i = 0; i < 15; i++)
    r[i] = R[i];

  if ((insn & 0x0f000f10u) == 0x0e000f10u)		/* MRC/MCR p15 */
    {
      unsigned rt = (insn >> 12) & 15, crn = (insn >> 16) & 15, crm = insn & 15;
      unsigned op1 = (insn >> 21) & 7, op2 = (insn >> 5) & 7;
      if (rt == 15)
	return 0;
      if (insn & (1u << 20))
	{
	  unsigned v = 0;
	  if (crn == 1 && crm == 0 && op1 == 0 && op2 == 0)
	    v = ns.sctlr;
	  else if (crn == 0 && crm == 0 && op1 == 0 && op2 == 0)
	    v = ns.midr;
	  else if (crn == 0 && crm == 0 && op1 == 0 && op2 == 1)
	    v = 0x8444c004u;				/* CTR (Cortex-A72's) */
	  else if (crn == 0 && crm == 0 && op1 == 0 && op2 == 5)
	    v = 0x80000000u;				/* MPIDR: CPU 0 */
	  else if (crn == 6 && crm == 0 && op1 == 0 && op2 == 0)
	    v = ns.last_dfar;				/* DFAR */
	  else if (crn == 5 && crm == 0 && op1 == 0 && op2 == 0)
	    v = ns.last_dfsr;				/* DFSR */
	  R[rt] = v;
	}
      else if (crn == 1 && crm == 0 && op1 == 0 && op2 == 0)
	ns.sctlr = R[rt];
      else if (crn == 7 && op1 == 0 && crm == 10 && op2 == 5)
	__asm__ volatile ("dmb ish" ::: "memory");	/* CP15 DMB */
      else if (crn == 7 && op1 == 0 && crm == 10 && op2 == 4)
	__asm__ volatile ("dsb ish" ::: "memory");	/* CP15 DSB */
      else if (crn == 7 && op1 == 0 && crm == 5 && op2 == 4)
	__asm__ volatile ("isb" ::: "memory");		/* CP15 ISB */
      else if (crn == 7 && op1 == 0 && op2 == 1
	       && (crm == 5 || crm == 10 || crm == 11 || crm == 14))
	{
	  /* I or D cache line by address: Linux's cacheflush */
	  unsigned a = R[rt] & ~63u;
	  lsys3 (0xf0002, a, a + 64, 0);
	}
      R[15] = pc + 4;
      return 1;
    }
  if ((insn & 0x0fff0fffu) == 0x014f0000u)		/* MRS Rd, SPSR */
    {
      unsigned rd = (insn >> 12) & 15;
      if (rd == 15)
	return 0;
      R[rd] = *(unsigned *) (bk + 8);
      R[15] = pc + 4;
      return 1;
    }
  if ((insn & 0x0ff0fff0u) == 0x0160f000u || (insn & 0x0ff0f000u) == 0x0360f000u)
    {
      unsigned val = insn & (1u << 25) ? ror32 (insn & 0xff, ((insn >> 8) & 15) * 2)
				       : R[insn & 15];
      unsigned mask = 0;
      for (int f = 0; f < 4; f++)
	if (insn & (1u << (16 + f)))
	  mask |= 0xffu << (8 * f);
      unsigned *sp = (unsigned *) (bk + 8);
      /* (the T bit kept: an interrupted Thumb program's context, saved
	 by the port as a real ARM saves it; the returns turn it into bit 0
	 of the pc - exception_return, LoadUsrRegisters, the mode switch) */
      *sp = (*sp & ~mask) | (val & mask);
      R[15] = pc + 4;
      return 1;
    }
  if ((insn & 0x0e400000u) == 0x08400000u)		/* LDM/STM with ^ */
    {
      int load = insn & (1u << 20), wb = insn & (1u << 21);
      int up = insn & (1u << 23), pre = insn & (1u << 24);
      unsigned rn = (insn >> 16) & 15, list = insn & 0xffff, n = popcount16 (list);
      int user_bank = !(load && (list & 0x8000));
      int other_bank = user_bank && mode != 0x1f && mode != 0x10;
      unsigned base = R[rn], addr, newpc = 0;
      if (rn == 15 || n == 0)
	return 0;
      addr = up ? base + (pre ? 4 : 0) : base - 4 * n + (pre ? 0 : 4);
      for (int i = 0; i < 16; i++)
	{
	  if (!(list & (1u << i)))
	    continue;
	  unsigned *m = (unsigned *) addr;
	  if (load)
	    {
	      unsigned v = *m;
	      if (i == 15)
		newpc = v;
	      else if (other_bank && (i == 13 || i == 14))
		*(unsigned *) (vcpu + 28 + (i - 13) * 4) = v;
	      else
		r[i] = v;
	    }
	  else
	    {
	      unsigned v;
	      if (i == 15)
		v = pc + 8;
	      else if (other_bank && (i == 13 || i == 14))
		v = *(unsigned *) (vcpu + 28 + (i - 13) * 4);
	      else
		v = R[i];
	      *m = v;
	    }
	  addr += 4;
	}
      if (wb && !(load && (list & (1u << rn))))
	r[rn] = up ? base + 4 * n : base - 4 * n;
      if (load && (list & 0x8000))
	return exception_return (R, vcpu, mode, r, newpc);
      for (int i = 0; i < 15; i++)
	R[i] = r[i];
      R[15] = pc + 4;
      return 1;
    }
  if ((insn & 0x0c10f000u) == 0x0010f000u)		/* <op>S pc, ... */
    {
      unsigned op = (insn >> 21) & 15, rn = (insn >> 16) & 15;
      unsigned op2, a, res;
      if (insn & (1u << 25))
	op2 = ror32 (insn & 0xff, ((insn >> 8) & 15) * 2);
      else if ((insn & 0xff0) == 0)
	op2 = (insn & 15) == 15 ? pc + 8 : R[insn & 15];
      else
	return 0;
      a = rn == 15 ? pc + 8 : R[rn];
      switch (op)
	{
	case 13: res = op2; break;
	case 2: res = a - op2; break;
	case 4: res = a + op2; break;
	default: return 0;
	}
      return exception_return (R, vcpu, mode, r, res);
    }
  return 0;
}

/* getdents64 without "." and ".." (IXFS) */
static int
getdents_filtered (unsigned fd, unsigned buf, unsigned size)
{
  for (;;)
    {
      int n = lsys3 (SYS_getdents64, fd, buf, size);
      if (n <= 0)
	return n;
      char *b = (char *) buf;
      int in = 0, out = 0;
      while (in < n)
	{
	  unsigned short reclen = *(unsigned short *) (b + in + 16);
	  const char *name = b + in + 19;
	  int dot = name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
	  if (!dot)
	    {
	      if (out != in)
		memmove (b + out, b + in, reclen);
	      out += reclen;
	    }
	  if (!reclen)
	    break;
	  in += reclen;
	}
      if (out > 0)
	return out;
    }
}

static void sync_patch (unsigned lo, unsigned hi);
static void sq_check (void);
static void fault_dump (unsigned *R, unsigned addr);

/* NativeSupport$Debug set when the module starts: each emulation reported
   on Linux's stderr ("faults": only the faults passed on to RISC OS) */
static int debug, debug_faults;
static void
dbg (const char *what, unsigned a, unsigned b, unsigned c)
{
  if (!debug && !(debug_faults && what[0] == 'f' && what[1] == 'a'))
    return;
  char buf[80];
  int n = 0;
  while (*what && n < 40)
    buf[n++] = *what++;
  unsigned v[3] = { a, b, c };
  for (int k = 0; k < 3; k++)
    {
      buf[n++] = ' ';
      for (int i = 28; i >= 0; i -= 4)
	buf[n++] = "0123456789abcdef"[(v[k] >> i) & 15];
    }
  buf[n++] = '\n';
  lsys3 (4, 2, (unsigned) buf, n);
}

static void
chain (const struct ksigaction *a, int sig, void *si, void *uc)
{
  if (a->handler > 1)
    ((sighandler *) a->handler) (sig, si, uc);
}

/* Memory the interrupted code points the handlers at may not be there: the
   emulations run through probe_call (header.s), so that a fault in them
   fails the emulation (the instruction goes to RISC OS, as under QEMU)
   instead of killing the process. */
extern int probe_call (int (*fn) (void *), void *arg);
extern void probe_recover (void);
extern unsigned probe_sp;
struct pargs { unsigned *R, insn, lo, hi; };
static unsigned rma_lo, rma_hi;		/* the RMA (its largest size) */

static int p_psr (void *a) { struct pargs *p = a; return emulate_psr (p->R, p->insn); }
static int p_priv (void *a) { struct pargs *p = a; return emulate_priv (p->R, p->insn); }
static int
p_rewrite (void *a)
{
  struct pargs *p = a;
  volatile unsigned *q = (volatile unsigned *) p->lo;
  if (*q != p->hi)
    return 0;
  *q = p->insn;
  lsys3 (0xf0002, p->lo, p->lo + 4, 0);
  return 1;
}
static int p_sync (void *a) { struct pargs *p = a; sync_patch (p->lo, p->hi); return 1; }

/* A whole-cache synchronisation: everything (screen memory, the whole of
   application space...) is too much to scan for code, but the RMA, where
   code is put that isn't a module (vector handlers, veneers; QEMU emulates
   them wherever they are), is a few MB. Page by page from its base until
   one isn't there (the probe ends it). */
static int
p_sync_rma (void *a)
{
  (void) a;
  sq_check ();
  for (unsigned pg = rma_lo; pg && pg < rma_hi; pg += 0x1000)
    {
      (void) *(volatile unsigned *) pg;
      sync_patch (pg, pg + 0x1000);
    }
  return 1;
}

/* SWP{B} Rt, Rt2, [Rn]: undefined on ARMv8 (unless Linux emulates it);
   RISC OS is one thread, and interrupts are held off here */
static int
p_swp (void *a)
{
  struct pargs *pp = a;
  unsigned *R = pp->R, w = pp->insn;
  unsigned rt = (w >> 12) & 15, rt2 = w & 15, rn = (w >> 16) & 15;
  if (rt == 15 || rt2 == 15 || rn == 15)
    return 0;
  unsigned addr = R[rn], v;
  if (w & (1u << 22))
    {
      v = *(volatile unsigned char *) addr;
      *(volatile unsigned char *) addr = (unsigned char) R[rt2];
    }
  else
    {
      v = *(volatile unsigned *) addr;
      *(volatile unsigned *) addr = R[rt2];
    }
  R[rt] = v;
  R[15] += 4;
  return 1;
}

/* Work done inside the SIGILL handler that calls RISC OS SWIs (a
   synchronisation's sync_patch: cao (), in_fileswitch (); da_place): the
   port's handlers don't have SA_NODEFER, so SIGILL is blocked here, and a
   patched instruction met inside such a SWI - another module on the SWI
   vector (EDTCore's swi_entry starts with MRS r0, SPSR, a UDF site) -
   made Linux end RISC OS with SIGILL. Let SIGILL, SIGSEGV and SIGBUS in
   around it, as fault_dump does for SIGSEGV and SIGBUS. */
static int
traps_in (unsigned old[2])
{
  unsigned set[2] = { 1u << (SIGILL_ - 1) | 1u << (SIGSEGV_ - 1) | 1u << (SIGBUS_ - 1), 0 };
  return lsys (SYS_rt_sigprocmask, 1, (unsigned) set, (unsigned) old, 8, 0, 0) == 0;
}

static void
traps_back (int masked, unsigned old[2])
{
  if (masked)
    lsys (SYS_rt_sigprocmask, 2, (unsigned) old, 0, 8, 0, 0);
}

static int
probe (int (*fn) (void *), unsigned *R, unsigned insn)
{
  struct pargs a = { R, insn, 0, 0 };
  return probe_call (fn, &a);
}

/* The privileged instructions handled when they trap (failing their
   condition, some processors trap anyway: skipped, as they should be) */
static int
priv_form (unsigned w)
{
  return (w & 0x0f000f10u) == 0x0e000f10u			/* MRC/MCR p15 */
	 || (w & 0x0fbf0fffu) == 0x010f0000u			/* MRS */
	 || (w & 0x0fb0f000u) == 0x0120f000u			/* MSR reg */
	 || (w & 0x0fb0f000u) == 0x0320f000u			/* MSR imm */
	 || (w & 0x0e400000u) == 0x08400000u			/* LDM/STM ^ */
	 || (w & 0x0c10f000u) == 0x0010f000u			/* <op>S pc */
	 || (w & 0x0fb00ff0u) == 0x01000090u;			/* SWP{B} */
}

/* ------------------------------------------------------------------------
   Mode checks. Natively the processor is always in user mode: RISC OS's
   mode is a byte in the port's vcpu, and each mode's r13, r14 and SPSR are
   kept there while another runs. If an instruction that changes mode is
   missed, or done differently (a processor that traps what another just
   runs), the mode and the stack disagree, and something fails later: a
   SWI handler running on a program's stack, say. So in SVC mode the stack
   must be in the SVC stack's megabyte (what the C library's veneers
   assume: sl = sp >> 20 << 20), checked at every SWI (header.s) and every
   trap. The first disagreement of each kind is kept for *NativeSupport
   and, with fault reports on, logged with the last 32 traps before it
   (signal, pc, the instruction or fault address, mode and stack before
   and after). Nothing is changed: it is only reported. */
extern unsigned ns_svc_base;
#define EV_N 32
static struct ev { unsigned sig, pc, insn, vm0, sp0, pc1, vm1, sp1; } evs[EV_N];
static unsigned evn;
static struct { unsigned n, swi, ret, spsr, sp; } bad_swi;
static struct { unsigned n, sig, pc, insn, sp, lr; } bad_trap;
static int in_bad;

static unsigned
vmode_now (void)
{
  unsigned v = get_tls ();
  return v ? *(volatile uint8_t *) (v + VCPU_CPSR) : 0;
}

static void
ev_dump (void)
{
  for (unsigned i = 0; i < EV_N; i++)
    {
      struct ev *e = &evs[(evn + i) % EV_N];
      if (!e->pc)
	continue;
      dbg ("fault-ev", e->sig << 16 | (e->vm0 & 0xff) << 8 | (e->vm1 & 0xff),
	   e->pc, e->insn);
      dbg ("fault-ev-sp", e->sp0, e->sp1, e->pc1);
    }
}

void ns_mode_bad (unsigned *r, unsigned ret);
void
ns_mode_bad (unsigned *r, unsigned ret)
{
  if (in_bad || (unsigned) r - 0x20800000u < 0x10000u)
    return;
  in_bad = 1;
  unsigned vcpu = get_tls ();
  unsigned spsr = vcpu ? *(volatile unsigned *) (vcpu + 24) : 0;
  unsigned swi = (spsr & CPSR_T) ? ~0u
		 : *(volatile unsigned *) (ret - 4) & 0x00ffffffu;
  unsigned sp = (unsigned) (r + 4);
  if (!bad_swi.n)
    {
      bad_swi.swi = swi;
      bad_swi.ret = ret;
      bad_swi.spsr = spsr;
      bad_swi.sp = sp;
    }
  bad_swi.n++;
  if (debug_faults && bad_swi.n <= 5)
    {
      dbg ("fault-mode-swi", swi, ret, spsr);
      dbg ("fault-mode-swi-sp", sp, ns_svc_base, vmode_now ());
      dbg ("fault-mode-swi-r0-2", r[0], r[1], r[2]);
      if (bad_swi.n == 1)
	ev_dump ();
    }
  in_bad = 0;
}

static void
mode_check_trap (int sig, unsigned *R, unsigned what)
{
  /* (not a fault in an emulation, NativeSupport's own: the handlers run
     on the port's signal stack, in the HAL's pages) */
  if (!ns_svc_base || in_bad || probe_sp || (vmode_now () & 0x1f) != 0x13
      || !((R[13] ^ ns_svc_base) >> 20) || R[13] - 0x20800000u < 0x10000u)
    return;
  in_bad = 1;
  if (!bad_trap.n)
    {
      bad_trap.sig = sig;
      bad_trap.pc = R[15];
      bad_trap.insn = what;
      bad_trap.sp = R[13];
      bad_trap.lr = R[14];
    }
  bad_trap.n++;
  if (debug_faults && bad_trap.n <= 5)
    {
      dbg ("fault-mode-trap", sig, R[15], what);
      dbg ("fault-mode-trap-sp", R[13], R[14], R[16]);
      if (bad_trap.n == 1)
	ev_dump ();
    }
  in_bad = 0;
}

/* (fault reports) the first time a mode's saved PSR has the T bit, which
   the port refuses when it's returned through: which mode, after what */
static void
spsr_t_check (const struct ev *e)
{
  static int seen;
  unsigned v = get_tls ();
  if (seen || !debug_faults || !v)
    return;
  static const unsigned char banks[] = { 16, 28, 40, 52, 64, 76 };
  for (unsigned i = 0; i < sizeof banks; i++)
    if (*(volatile unsigned *) (v + banks[i] + 8) & CPSR_T)
      {
	seen = 1;
	dbg ("fault-spsr-t", banks[i], *(volatile unsigned *) (v + banks[i] + 8),
	     *(volatile uint8_t *) (v + VCPU_CPSR));
	dbg ("fault-spsr-t-after", e->pc, e->insn, e->pc1);
	ev_dump ();
	return;
      }
}

static void ill_body (int sig, void *si, void *uc);
static void fault_body (int sig, void *si, void *uc);

/* The signal handlers: the event kept, the check, then the work */
static void
ill_handler (int sig, void *si, void *uc)
{
  unsigned *R = UC_REGS (uc);
  struct ev *e = &evs[evn++ % EV_N];
  e->sig = sig;
  e->pc = R[15];
  e->insn = (R[16] & CPSR_T) ? *(volatile unsigned short *) R[15]
			      : *(volatile unsigned *) R[15];
  e->vm0 = vmode_now ();
  e->sp0 = R[13];
  e->pc1 = e->vm1 = e->sp1 = 0;
  mode_check_trap (sig, R, e->insn);
  /* an undefined instruction in Thumb code (passed to the port) */
  static int thumb_logged;
  if (debug_faults && (R[16] & CPSR_T) && thumb_logged < 20)
    {
      thumb_logged++;
      dbg ("fault-thumb-ill", R[15], *(volatile unsigned short *) R[15]
	   | (unsigned) *(volatile unsigned short *) (R[15] + 2) << 16,
	   vmode_now () | R[16] << 8);
    }
  ill_body (sig, si, uc);
  e->pc1 = R[15];
  e->vm1 = vmode_now ();
  e->sp1 = R[13];
  spsr_t_check (e);
}

static void
fault_handler (int sig, void *si, void *uc)
{
  unsigned *R = UC_REGS (uc);
  struct ev *e = &evs[evn++ % EV_N];
  e->sig = sig;
  e->pc = R[15];
  e->insn = SI_ADDR (si);		/* for a fault: the address */
  e->vm0 = vmode_now ();
  e->sp0 = R[13];
  e->pc1 = e->vm1 = e->sp1 = 0;
  mode_check_trap (sig, R, e->insn);
  /* a jump to nowhere (the pc is what faulted, or in the first 32K): the
     last traps before it, once */
  static int wild_dumped;
  if (debug_faults && !wild_dumped
      && (e->insn == R[15] || R[15] < 0x8000 || e->insn < 0x1000))
    {
      wild_dumped = 1;
      dbg ("fault-wild", R[15], R[14], R[13]);
      ev_dump ();
    }
  fault_body (sig, si, uc);
  e->pc1 = R[15];
  e->vm1 = vmode_now ();
  e->sp1 = R[13];
  spsr_t_check (e);
}

static void
ill_body (int sig, void *si, void *uc)
{
  unsigned *R = UC_REGS (uc);
  unsigned pc = R[15];
  if (!(R[16] & CPSR_T))
    {
      unsigned w = *(volatile unsigned *) pc;
      /* An instruction whose condition fails does nothing. But the
	 architecture lets a processor raise "undefined instruction" for an
	 undefined one even when its condition fails, and some do (the
	 Snapdragon 8cx Gen 3's Cortex-A78C and X1C, and a Pi 4's Cortex-A72 for
	 some, 30 million in a day's use):
	 for one, the "LDMNE sp!, {..., pc}^" after the C library's
	 "TEQ pc, pc" (26- or 32-bit?), which on any 32-bit processor is
	 skipped. Skip it, as a processor that doesn't trap it would. */
      if ((w >> 28) != 15 && !cond_pass (w, R[16]))
	{
	  R[15] = pc + 4;
	  ns.cond_skipped++;
	  trap_count (skip_traps, pc, w);
	  return;
	}
      /* SETEND LE/BE (ARMv6; trapped by Linux kernels that don't emulate
	 it, as the image's doesn't): the E bit of the CPSR, which the
	 kernel keeps on the way back */
      if ((w & 0xfffffdffu) == 0xf1010000u)
	{
	  if (w & (1u << 9))
	    R[16] |= 1u << 9;
	  else
	    R[16] &= ~(1u << 9);
	  R[15] = pc + 4;
	  ns.emul_priv++;
	  dbg ("setend", pc, w, R[16]);
	  return;
	}
      /* The ARMv6 CP15 barriers (MCR p15, 0, Rt, c7, c10, 4/5 and c7, c5,
	 4), in any mode: user programs use them too (POV-Ray's GCC 4.7
	 atomics). Linux emulates them only when built with
	 CONFIG_ARMV8_DEPRECATED, which the Dev Kit's kernel isn't (Raspberry
	 Pi ones are). Done here, and an unconditional one in application
	 space replaced by the ARMv7 instruction, so it traps only once. */
      if ((w & 0x0fff0fdfu) == 0x0e070f9au || (w & 0x0fff0fffu) == 0x0e070f95u)
	{
	  unsigned op2 = (w >> 5) & 7, crm = w & 15;
	  unsigned nat = crm == 5 ? 0xf57ff06fu		/* ISB */
	    : op2 == 4 ? 0xf57ff04bu : 0xf57ff05bu;	/* DSB/DMB ISH */
	  if (crm == 5)
	    __asm__ volatile ("isb" ::: "memory");
	  else
	    __asm__ volatile ("dsb ish" ::: "memory");
	  if ((w >> 28) == 14 && pc >= 0x8000u && pc < 0x20000000u)
	    {
	      struct pargs a = { 0, nat, pc, w };
	      if (probe_call (p_rewrite, &a))
		ns.cp15_rewritten++;
	    }
	  ns.cp15_barriers++;
	  R[15] = pc + 4;
	  return;
	}
      struct site *s = site_at (pc, w);
      if (s && site_hits)
	{
	  unsigned k = s - sites;
	  site_hits[2 * k]++;
	  site_hits[2 * k + 1] = pc;
	}
      if (!s)
	trap_count (other_traps, pc, w);
      if (s)
	{
	  unsigned orig = s->orig;
	  if (!cond_pass (orig, R[16]))
	    {
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_CACHEFLUSH)
	    {
	      /* new code: every OS_SynchroniseCodeAreas, module load and
		 program start comes here, one area at a time (but not a
		 whole-cache synchronisation: everything, data too) */
	      unsigned old[2];
	      int masked = traps_in (old);
	      if (!ns.in_full)
		{
		  struct pargs a = { R, 0, R[0], R[1] };
		  probe_call (p_sync, &a);
		  ns.syncs++;
		}
	      else
		{
		  probe_call (p_sync_rma, 0);
		  ns.full_syncs++;
		}
	      traps_back (masked, old);
	      R[0] = lsys (R[7], R[0], R[1], R[2], 0, 0, 0);
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_DAADDR)
	    {
	      unsigned old[2];
	      int masked = traps_in (old);
	      unsigned a = da_place (R);
	      traps_back (masked, old);
	      if (debug_faults)
		{
		  unsigned w0 = 0;
		  for (int i = 0; i < 4 && da_name[i]; i++)
		    w0 |= (unsigned) (unsigned char) da_name[i] << (8 * i);
		  dbg ("fault-da-place", a, R[1], w0);
		}
	      da_name[0] = 0;
	      R[0] = a;
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_ABTDUMP)
	    {
	      unsigned vcpu = get_tls ();
	      unsigned m = *(volatile uint8_t *) (vcpu + VCPU_CPSR);
	      if ((m & 0x1f) != 0x10)
		{
		  unsigned ob = vcpu + bank (m), nb = vcpu + bank (0x17);
		  *(volatile unsigned *) ob = R[13];
		  *(volatile unsigned *) (ob + 4) = R[14];
		  R[13] = *(volatile unsigned *) nb;
		  R[14] = *(volatile unsigned *) (nb + 4);
		  *(volatile uint8_t *) (vcpu + VCPU_CPSR) = 0xd7;
		  ns.abtdump_sw++;
		  R[15] = pc + 16;
		  return;
		}
	      /* (USR: as the port would; can't happen here) */
	      R[13] -= 24;
	      unsigned *f = (unsigned *) R[13];
	      f[0] = R[0]; f[1] = R[1]; f[2] = R[2]; f[3] = R[3]; f[4] = R[12]; f[5] = R[14];
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_LUR)
	    {
	      /* the port's LoadUsrRegisters (r1 -> r0-r15, r3 = the PSR to
		 return with) refuses a PSR with the T bit, but returns with
		 an interworking load: an interrupted Thumb program's (Iris's
		 JavaScript), passed back by UnixLib's callback handler, goes
		 back with bit 0 of its pc set instead. Then "mov r9, lr". */
	      if ((R[3] & CPSR_T) && !(R[3] & CPSR_IT))
		{
		  R[3] &= ~CPSR_T;
		  *(volatile unsigned *) (R[1] + 60) |= 1;
		  ns.lur_thumb++;
		}
	      R[9] = R[14];
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_FULL_BEGIN)
	    {
	      ns.in_full++;
	      R[1] = 0;
	      R[15] = pc + 4;
	      return;
	    }
	  if (s->kind == KIND_FULL_END)
	    {
	      /* ldmfd sp!, {r0-r3, ip, pc}: the kernel's SVC stack */
	      unsigned *sp = (unsigned *) R[13];
	      if (ns.in_full)
		ns.in_full--;
	      R[0] = sp[0];
	      R[1] = sp[1];
	      R[2] = sp[2];
	      R[3] = sp[3];
	      R[12] = sp[4];
	      R[15] = sp[5] & ~1u;
	      if (sp[5] & 1)
		R[16] |= CPSR_T;
	      R[13] += 24;
	      return;
	    }
	  if (s->kind == KIND_GETDENTS)
	    {
	      if (R[7] == SYS_getdents64)
		R[0] = getdents_filtered (R[0], R[1], R[2]);
	      else
		R[0] = lsys (R[7], R[0], R[1], R[2], R[3], R[4], R[5]);
	      ns.getdents++;
	      R[15] = pc + 4;
	      return;
	    }
	  unsigned vm0 = *(volatile uint8_t *) (get_tls () + VCPU_CPSR);
	  dbg ("psr", pc, orig, vm0 | R[13] << 8);
	  if (probe (p_psr, R, orig))
	    {
	      ns.emul_psr++;
	      return;
	    }
	  if (probe (p_priv, R, orig))
	    {
	      ns.emul_priv++;
	      return;
	    }
	}
      else if (pc_emulated (pc) && priv_form (w))
	{
	  if (!cond_pass (w, R[16]))
	    {
	      R[15] = pc + 4;
	      return;
	    }
	  if ((w & 0x0fb00ff0u) == 0x01000090u ? probe (p_swp, R, w)
					       : probe (p_priv, R, w))
	    {
	      dbg ("priv", pc, w, R[13]);
	      ns.emul_priv++;
	      return;
	    }
	}
    }
  if (debug)
    {
      unsigned vcpu = get_tls ();
      dbg ("ill-chain", pc, (R[16] & CPSR_T) ? 0 : *(volatile unsigned *) pc,
	   (vcpu ? *(volatile uint8_t *) (vcpu + VCPU_CPSR) : 0) | R[16] << 8);
      dbg ("ill-regs", R[13], R[14], R[0]);
    }
  else if (debug_faults)
    {
      /* an undefined instruction passed on to RISC OS, other than the
	 floating point ones FPEmulator does (coprocessors 1 and 2): the
	 first 30, with the registers, code and stack */
      static int ills;
      unsigned w = (R[16] & CPSR_T) ? 0 : *(volatile unsigned *) pc;
      unsigned cp = (w >> 8) & 15;
      if (!((w & 0x0c000000u) == 0x0c000000u && (cp == 1 || cp == 2)) && ills < 30)
	{
	  unsigned vcpu = get_tls ();
	  ills++;
	  dbg ("fault-ill", pc, w,
	       (vcpu ? *(volatile uint8_t *) (vcpu + VCPU_CPSR) : 0) | R[16] << 8);
	  dbg ("fault-regs", R[14], R[13], 0);
	  fault_dump (R, 0);
	}
    }
  chain (&ns.old_ill, sig, si, uc);
}

/* ------------------------------------------------------------------------
   The test bed's ARMEABISupport (lite): SWIs &5AF40, &5AF41 and OS_Dynamic-
   Area 22 (as qemu/ patch item 5) */
#define PMP_MAX 16
#define ABH_MAX 16
static struct { int da, fd; unsigned base, pages; unsigned char *map; } pmp[PMP_MAX];	/* map: per page, 1 if mapped (OS_Memory 24) */
static struct { unsigned routine, r12; } abh[ABH_MAX];
static unsigned abh_regs, abh_return, abh_fault, abh_saved[17];
static int abh_active = -1, abh_sig, abh_code;

static unsigned
map_fixed (unsigned a, unsigned len, int prot, int flags, int fd, unsigned pg)
{
  return (unsigned) lsys (SYS_mmap2, a, len, prot, flags, fd, pg);
}

static int
pmp_find (unsigned da)
{
  for (int i = 0; i < PMP_MAX; i++)
    if (pmp[i].fd > 0 && pmp[i].da == (int) da)
      return i;
  return -1;
}

static int
pmp_unmap (int i, unsigned l, unsigned n)
{
  if (l >= pmp[i].pages || n > pmp[i].pages - l)
    return -22;
  unsigned a = pmp[i].base + (l << 12);
  if (map_fixed (a, n << 12, 0, 0x32, -1, 0) != a)
    return -22;
  if (pmp[i].map)
    memset (pmp[i].map + l, 0, n);
  return 0;
}

static int
pmp_map (int i, unsigned l, unsigned ph, unsigned n)
{
  if (l >= pmp[i].pages || n > pmp[i].pages - l || ph >= pmp[i].pages
      || n > pmp[i].pages - ph)
    return -22;
  unsigned a = pmp[i].base + (l << 12);
  if (map_fixed (a, n << 12, 7, 0x11, pmp[i].fd, ph) != a)
    return -12;
  if (pmp[i].map)
    memset (pmp[i].map + l, 1, n);
  return 0;
}

static int
pmp_op (unsigned *r)
{
  int i;
  switch (r[0])
    {
    case 0:
      if (pmp_find (r[1]) >= 0 || (r[2] & 0xfff) || !r[3])
	return -22;
      for (i = 0; i < PMP_MAX; i++)
	if (pmp[i].fd <= 0)
	  {
	    int fd = lsys3 (SYS_memfd_create, (unsigned) "RISC OS PMP", 1, 0);
	    if (fd < 0)
	      return fd;
	    int e = lsys3 (SYS_ftruncate, fd, r[3] << 12, 0);
	    if (e < 0)
	      {
		lsys3 (SYS_close, fd, 0, 0);
		return e;
	      }
	    pmp[i].da = r[1];
	    pmp[i].fd = fd;
	    pmp[i].base = r[2];
	    pmp[i].pages = r[3];
	    pmp[i].map = rma_claim (r[3]);
	    if (pmp[i].map)
	      memset (pmp[i].map, 0, r[3]);
	    return 0;
	  }
      return -28;
    case 1:
      if ((i = pmp_find (r[1])) < 0)
	return -2;
      pmp_unmap (i, 0, pmp[i].pages);
      lsys3 (SYS_close, pmp[i].fd, 0, 0);
      pmp[i].fd = 0;
      rma_free (pmp[i].map);
      pmp[i].map = 0;
      return 0;
    case 2:
      return (i = pmp_find (r[1])) < 0 ? -2 : pmp_map (i, r[2], r[3], r[4]);
    case 3:
      return (i = pmp_find (r[1])) < 0 ? -2 : pmp_unmap (i, r[2], r[3]);
    case 4:
      if ((i = pmp_find (r[1])) < 0)
	return -2;
      if (r[2] >= pmp[i].pages || r[3] > pmp[i].pages - r[2])
	return -22;
      return lsys (SYS_fallocate, pmp[i].fd, 3, r[2] << 12, r[2] >> 20,
		   r[3] << 12, r[3] >> 20);
    case 5:
      for (i = 0; i < ABH_MAX; i++)
	if (!abh[i].routine)
	  {
	    abh[i].routine = r[1];
	    abh[i].r12 = r[2];
	    abh_regs = r[3];
	    abh_return = r[4];
	    return 0;
	  }
      return -28;
    case 6:
      for (i = 0; i < ABH_MAX; i++)
	if (abh[i].routine == r[1] && abh[i].r12 == r[2])
	  {
	    abh[i].routine = 0;
	    return 0;
	  }
      return -2;
    }
  return -38;
}

static int
pmp_at (unsigned addr)
{
  for (int i = 0; i < PMP_MAX; i++)
    if (pmp[i].fd > 0 && addr >= pmp[i].base
	&& addr - pmp[i].base < pmp[i].pages << 12)
      return 1;
  return 0;
}

/* Call the next abort handler from 'from'; 0 if there's none left */
static int
abh_next (unsigned *R, int from)
{
  for (int i = from; i < ABH_MAX; i++)
    if (abh[i].routine)
      {
	abh_active = i;
	R[1] = abh_fault;
	R[11] = abh_regs;
	R[12] = abh[i].r12;
	R[14] = abh_return;
	R[15] = abh[i].routine & ~3u;
	R[16] &= ~CPSR_T;
	return 1;
      }
  abh_active = -1;
  return 0;
}

/* SWI &5AF41, NativeSupport_AbortReturn (the SWI vector): an abort handler returned. Back to the
   aborted code: r (the caller's r0-r12, return address) and SPSR_svc are
   set to what they were at the abort; then the port's MOVS pc, lr. If no
   handler took the abort, it is tried again with them out of the way, so
   that it goes to RISC OS. (Aborts in USR or SYS mode only: elsewhere sp
   and lr can't be put back this way.) */
static unsigned abh_vmode, abh_bypass;

static int
abh_return_swi (unsigned *r, unsigned *spsr)
{
  if (abh_active < 0)
    return 0;
  int took = (r[0] & 0xff) == 1;
  if (!took)
    {
      /* the next handler, if any, as if the abort happened now */
      for (int i = abh_active + 1; i < ABH_MAX; i++)
	if (abh[i].routine)
	  {
	    abh_active = i;
	    r[1] = abh_fault;
	    r[11] = abh_regs;
	    r[12] = abh[i].r12;
	    /* return: the routine, called with lr = the SWI &5AF41 */
	    r[13] = abh[i].routine & ~3u;
	    *spsr = (*spsr & ~0xffu & ~CPSR_T) | (abh_vmode & 0xdf);
	    /* the handler's lr: set in the aborted mode's bank below */
	    goto set_lr;
	  }
      abh_bypass = abh_fault;		/* none: RISC OS gets it */
    }
  abh_active = -1;
  for (int i = 0; i < 13; i++)
    r[i] = abh_saved[i];
  /* back to Thumb code (an abort in it: Iris's JavaScript): as the port
     does, the T bit isn't put in the SPSR - its mode switch refuses it -
     but the return address's bit 0 says Thumb (an interworking load) */
  /* Inside an IT block, though, the T bit stays in the SPSR: the mode
     switch stops at its T check, and fault_body returns through the
     signal frame, which puts ITSTATE back (an interworking load can't:
     the rest of the block would run unconditionally) */
  if ((abh_saved[16] & CPSR_T) && (abh_saved[16] & CPSR_IT))
    {
      r[13] = abh_saved[15] & ~1u;
      *spsr = (abh_saved[16] & ~0xffu) | (abh_vmode & 0xdf) | CPSR_T;
    }
  else
    {
      r[13] = abh_saved[15] | ((abh_saved[16] & CPSR_T) ? 1 : 0);
      *spsr = (abh_saved[16] & ~0xffu & ~CPSR_T) | (abh_vmode & 0xdf);
    }
  {
    unsigned *usr = (unsigned *) (get_tls () + 28);
    usr[0] = abh_saved[13];
    usr[1] = abh_saved[14];
  }
  return 1;
set_lr:
  {
    unsigned *usr = (unsigned *) (get_tls () + 28);
    usr[0] = abh_saved[13];
    usr[1] = abh_return;
  }
  return 1;
}

static int
abh_start (unsigned *R, int sig, int code, unsigned addr)
{
  if (abh_active >= 0 || !abh_regs || !pmp_at (addr))
    return 0;
  if (abh_bypass == addr)
    {
      abh_bypass = 0;
      return 0;
    }
  unsigned vcpu = get_tls ();
  abh_vmode = vcpu ? *(volatile uint8_t *) (vcpu + VCPU_CPSR) : 0x10;
  if ((abh_vmode & 0x1f) != 0x10 && (abh_vmode & 0x1f) != 0x1f)
    return 0;
  for (int i = 0; i < 16; i++)
    abh_saved[i] = R[i];
  abh_saved[16] = R[16];
  unsigned *blk = (unsigned *) abh_regs;
  for (int i = 0; i < 17; i++)
    blk[i] = abh_saved[i];
  /* (as the port's own register dumps: T in the PSR, the pc as it is) */
  abh_fault = addr;
  abh_sig = sig;
  abh_code = code;
  return abh_next (R, 0);
}

/* ------------------------------------------------------------------------
   OS_Memory 0 (qemu/ patch item 6): application space's "physical"
   addresses from the file mapped there for the task (its device and inode
   in /proc/self/maps), others' their logical address */
static char maps_buf[1024], line[512];

static unsigned
hex (const char **s)
{
  unsigned v = 0;
  for (;; (*s)++)
    {
      char c = **s;
      if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
      else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
      else return v;
    }
}

static unsigned
dec (const char **s)
{
  unsigned v = 0;
  while (**s >= '0' && **s <= '9')
    v = v * 10 + (*(*s)++ - '0');
  return v;
}

/* Linux's map of this process: the free address space below 2GB (where
   RISC OS for Linux puts dynamic areas, unless given --highmem) and the
   largest gap; with dump, every mapping of 1MB or more as fault-map lines
   (start, end, permissions as 4 characters) */
static void
maps_report (unsigned *free_lo, unsigned *gap_lo, unsigned *free_all, int dump)
{
  *free_lo = *gap_lo = *free_all = 0;
  int fd = lsys3 (SYS_open, (unsigned) "/proc/self/maps", 0x80000, 0);
  if (fd < 0)
    return;
  unsigned ll = 0, prev = 0;
  for (;;)
    {
      int n = lsys3 (SYS_read, fd, (unsigned) maps_buf, sizeof maps_buf);
      if (n <= 0)
	break;
      for (int i = 0; i < n; i++)
	{
	  if (maps_buf[i] != '\n')
	    {
	      if (ll < sizeof line - 1)
		line[ll++] = maps_buf[i];
	      continue;
	    }
	  line[ll] = 0;
	  ll = 0;
	  const char *t = line;
	  unsigned lo = hex (&t);
	  t++;
	  unsigned hi = hex (&t);
	  while (*t == ' ') t++;
	  if (lo > prev)
	    {
	      unsigned g = lo - prev;
	      *free_all += g;
	      unsigned glo = (lo < 0x80000000u ? lo : 0x80000000u);
	      if (glo > prev)
		{
		  *free_lo += glo - prev;
		  if (glo - prev > *gap_lo)
		    *gap_lo = glo - prev;
		}
	    }
	  if (hi > prev)
	    prev = hi;
	  if (dump && hi - lo >= 0x100000u)
	    {
	      unsigned perm = 0;
	      for (int k = 0; k < 4 && t[k]; k++)
		perm |= (unsigned) (unsigned char) t[k] << (8 * k);
	      dbg ("fault-map", lo, hi, perm);
	    }
	}
    }
  lsys3 (SYS_close, fd, 0, 0);
}

/* Where a new dynamic area goes. The port takes an address from Linux
   (mmap2 NULL, the area's maximum size, PROT_NONE); with --highmem,
   Linux's choice is the highest free one, above 2GB. Some programs can't
   use addresses above 2GB, so areas go below 2GB first (the highest free
   block there that fits, as Linux would choose), and above only if there's
   no room below, or for those NativeSupport$High names (comma-separated
   name prefixes, "*" for all). The name is the one OS_DynamicArea 0 was
   given (recorded by the SWI vector). */
static int
da_is_high (void)
{
  if (da_high[0] == '*')
    return 1;
  const char *p = da_high;
  while (*p)
    {
      int n = 0;
      while (p[n] && p[n] != ',')
	n++;
      if (n)
	{
	  int k = 0;
	  while (k < n && da_name[k] == p[k])
	    k++;
	  if (k == n)
	    return 1;
	}
      p += n;
      if (*p == ',')
	p++;
    }
  return 0;
}

/* the top of the highest free block below 2GB of at least size, or 0
   (not right up to 2GB: an area ending at &80000000 stopped RISC OS
   starting; without --highmem the port's areas end below &6E400000) */
#define LOW_CEIL	0x7f000000u
static unsigned
low_gap (unsigned size)
{
  int fd = lsys3 (SYS_open, (unsigned) "/proc/self/maps", 0x80000, 0);
  if (fd < 0)
    return 0;
  unsigned ll = 0, prev = 0x10000u, best = 0;
  for (;;)
    {
      int n = lsys3 (SYS_read, fd, (unsigned) maps_buf, sizeof maps_buf);
      if (n <= 0)
	break;
      for (int i = 0; i < n; i++)
	{
	  if (maps_buf[i] != '\n')
	    {
	      if (ll < sizeof line - 1)
		line[ll++] = maps_buf[i];
	      continue;
	    }
	  line[ll] = 0;
	  ll = 0;
	  const char *t = line;
	  unsigned lo = hex (&t);
	  t++;
	  unsigned hi = hex (&t);
	  unsigned top = lo < LOW_CEIL ? lo : LOW_CEIL;
	  if (top > prev && top - prev >= size)
	    best = top;
	  if (hi > prev)
	    prev = hi;
	}
    }
  if (prev < LOW_CEIL && LOW_CEIL - prev >= size)
    best = LOW_CEIL;
  lsys3 (SYS_close, fd, 0, 0);
  return best;
}

static unsigned
da_place (unsigned *R)
{
  unsigned size = R[1];
  if (!da_is_high () && size && size < 0x80000000u)
    {
      unsigned top = low_gap (size);
      if (top)
	{
	  unsigned a = lsys (R[7], top - size, size, R[2], R[3], R[4], R[5]);
	  if (a + size <= LOW_CEIL && a >= 0x10000u)
	    {
	      ns.da_low++;
	      return a;
	    }
	  if (a < 0xfffff000u)
	    lsys3 (SYS_munmap, a, size, 0);
	}
      ns.da_over++;
    }
  unsigned a = lsys (R[7], R[0], R[1], R[2], R[3], R[4], R[5]);
  if (a < 0xfffff000u && a >= 0x80000000u)
    ns.da_high++;
  return a;
}

static unsigned
app_phys (unsigned addr)
{
  static struct { unsigned dev, ino; } ids[256];
  static int nids;
  int fd = lsys3 (SYS_open, (unsigned) "/proc/self/maps", 0x80000, 0);
  if (fd < 0)
    return addr;
  unsigned id = 0, ll = 0;
  int found = 0;
  for (;;)
    {
      int n = lsys3 (SYS_read, fd, (unsigned) maps_buf, sizeof maps_buf);
      if (n <= 0)
	break;
      for (int i = 0; i < n && !found; i++)
	{
	  if (maps_buf[i] != '\n')
	    {
	      if (ll < sizeof line - 1)
		line[ll++] = maps_buf[i];
	      continue;
	    }
	  line[ll] = 0;
	  ll = 0;
	  const char *s = line;
	  unsigned lo = hex (&s);
	  s++;
	  unsigned hi = hex (&s);
	  if (addr < lo || addr >= hi)
	    continue;
	  while (*s == ' ') s++;
	  while (*s && *s != ' ') s++;		/* permissions */
	  while (*s == ' ') s++;
	  unsigned off = hex (&s);
	  while (*s == ' ') s++;
	  unsigned dev = hex (&s) << 8;
	  s++;
	  dev |= hex (&s);
	  while (*s == ' ') s++;
	  unsigned ino = dec (&s);
	  int k;
	  for (k = 0; k < nids; k++)
	    if (ids[k].dev == dev && ids[k].ino == ino)
	      break;
	  if (k == nids && nids < 256)
	    {
	      ids[nids].dev = dev;
	      ids[nids].ino = ino;
	      nids++;
	    }
	  id = (unsigned) (k + 1) << 24 | ((off + (addr - lo)) & 0xfff000);
	  found = 1;
	}
      if (found)
	break;
    }
  lsys3 (SYS_close, fd, 0, 0);
  return id | (addr & 0xfff);
}

static int
os_memory0 (unsigned *R)
{
  unsigned r0 = R[0];
  if ((r0 & 0xff) != 0 || (r0 & 0x700) != 0x200)
    return 0;
  unsigned *e = (unsigned *) R[1];
  for (unsigned n = 0; n < R[2]; n++, e += 3)
    {
      unsigned la = e[1];
      unsigned pa = la >= 0x8000 && la < 0x20000000 ? app_phys (la) : la;
      if (r0 & 0x800)
	e[0] = pa >> 12;
      if (r0 & 0x2000)
	e[2] = pa;
    }
  return 1;
}

static int
pmp_da22 (unsigned *R)
{
  int i = pmp_find (R[1]);
  if (i < 0)
    return 0;
  while (R[3])
    {
      unsigned *e = (unsigned *) R[2];
      if (e[1] == 0xffffffffu)
	pmp_unmap (i, e[0], 1);
      else
	pmp_map (i, e[0], e[1], 1);
      R[2] += 12;
      R[3]--;
    }
  return 1;
}

static unsigned self_lo, self_hi;


static int
overlaps (unsigned lo, unsigned hi, unsigned a, unsigned n)
{
  return lo < a + n && hi > a;
}

/* A squeezed program skipped (below): patched once it's unsqueezed, at
   the next synchronisation of any kind, in case its decompressor ends
   with a whole-cache one (only the RMA is scanned then) */
static unsigned sq_pending;

static int
aif_squeezed (void)
{
  return (*(volatile unsigned *) 0x8000u & 0xff000000u) == 0xeb000000u
    && *(volatile unsigned *) 0x8010u == 0xef000011u;
}

static void
sq_check (void)
{
  if (!sq_pending || aif_squeezed ())
    return;
  sq_pending = 0;
  unsigned ro = *(volatile unsigned *) 0x8014u, rw = *(volatile unsigned *) 0x8018u;
  if (*(volatile unsigned *) 0x8010u != 0xef000011u
      || ro + rw < 0x20u || ro + rw > 0x20000000u - 0x8000u)
    return;
  dbg ("fault-sq-late", 0x8000u, 0x8000u + ro + rw, 0);
  ns.patched_sync += patch_range (0x8000u, 0x8000u + ro + rw);
}

static int in_pmp_area (unsigned lo, unsigned hi);
static int kpmp_access (unsigned lo, unsigned hi);
static int rom_pmp_dirty = 1;	/* set when a dynamic area is made */

/* The end of the program at &8000 as its AIF header gives it (read-only
   and read-write areas), once it's unsqueezed; 0 if &8000 isn't an AIF
   program */
static unsigned
aif_end (void)
{
  unsigned ro = *(volatile unsigned *) 0x8014u, rw = *(volatile unsigned *) 0x8018u;
  if (*(volatile unsigned *) 0x8010u != 0xef000011u || aif_squeezed ()
      || ro < 0x20u || ro > 0x20000000u - 0x8000u
      || rw > 0x20000000u - 0x8000u - ro)
    return 0;
  return 0x8000u + ro + rw;
}

/* OS_SynchroniseCodeAreas's caller (header.s's SWI vector: its return
   address), and whether that is in FileSwitch's code */
extern unsigned ns_sync_caller;
static int
in_fileswitch (unsigned a)
{
  static unsigned fs_lo, fs_hi;
  if (!fs_lo)
    {
      regs m = {{ 18, (unsigned) "FileSwitch" }};
      if (swix_regs (0x1E, &m))			/* OS_Module 18 */
	return 0;
      fs_lo = m.r[3];
      fs_hi = fs_lo + *(volatile unsigned *) (fs_lo - 4);
    }
  return a >= fs_lo && a < fs_hi;
}

/* Who synchronised application space, and what was done: 'o' part of
   the program (patched), 'f' FileSwitch's (left), 'd' a program's own
   data outside its image (left), 'p' anything else (patched: BASIC's
   assembler, the C library's stubs and program loads). Shown by
   *NativeSupport; with NativeDebug the first 200 are logged as
   ns-syncwho lines. */
static void
syncwho_log (char what, unsigned caller, unsigned cao, unsigned lo, unsigned hi)
{
  char buf[96];
  const char *h = "ns-syncwho ";
  int n = 0;
  while (*h)
    buf[n++] = *h++;
  buf[n++] = what;
  unsigned v[4] = { lo, hi, caller, cao };
  for (int k = 0; k < 4; k++)
    {
      buf[n++] = ' ';
      for (int i = 28; i >= 0; i -= 4)
	buf[n++] = "0123456789abcdef"[(v[k] >> i) & 15];
    }
  buf[n++] = '\n';
  lsys3 (4, 2, (unsigned) buf, n);
}
static void
syncwho_note (char what, unsigned caller, unsigned cao, unsigned lo, unsigned hi)
{
  if (debug_faults && syncwho_logged < 200)
    {
      syncwho_logged++;
      syncwho_log (what, caller, cao, lo, hi);
    }
  for (unsigned i = 0; i < nsyncwho; i++)
    if (syncwho[i].caller == caller && syncwho[i].what == what)
      {
	syncwho[i].n++;
	syncwho[i].lo = lo, syncwho[i].hi = hi, syncwho[i].cao = cao;
	syncwho_cur = (int) i;
	return;
      }
  if (nsyncwho == SYNCWHO_MAX)
    {
      syncwho_lost++;
      return;
    }
  struct syncwho *w = &syncwho[nsyncwho];
  w->caller = caller, w->cao = cao, w->lo = lo, w->hi = hi, w->n = 1, w->what = what;
  w->sites = w->s_addr = w->s_orig = 0;
  syncwho_cur = (int) nsyncwho++;
}

/* The current active object: &8000 while a program loaded there runs,
   a module's address while a module (BASIC, say) is the application */
static unsigned
cao (void)
{
  regs r = {{ 15, 0, 0, 0 }};
  if (swix_regs (0x40, &r))			/* OS_ChangeEnvironment */
    return 0;
  return r.r[1];
}

static void
sync_patch (unsigned lo, unsigned hi)
{
  /* a range synchronised is code: in application space, the RMA or a
     dynamic area (SOManager's shared libraries are) */
  syncwho_cur = -1;
  if (hi <= lo)
    return;
  /* Up to 64 MB, but in application space a program's image, which can
     be bigger (the web browser's is 105 MB, 64.4 MB of it code): left
     unpatched, its UnixLib's MSR CPSR_c back to USR mode did nothing,
     and its callback handler went on in SVC mode until a SWI looped */
  if (hi - lo > 0x4000000u && !(hi <= 0x20000000u && lo <= 0x8000u && aif_end ()))
    {
      static int nlog;
      ns.sync_too_big++;
      if (nlog++ < 20)
	dbg ("fault-sync-big", lo, hi, *(volatile unsigned *) 0x8000u);
      return;
    }
  if (overlaps (lo, hi, ROM_LO, ROM_HI - ROM_LO)	/* the port's own */
      || overlaps (lo, hi, self_lo, self_hi - self_lo)	/* this module, */
      || overlaps (lo, hi, (unsigned) sites, SITES_MAX * sizeof *sites)
      || overlaps (lo, hi, (unsigned) stubs, STUBS_SIZE)	/* its tables */
      || overlaps (lo, hi, LOWSTUBS, LOWSTUBS_SIZE))
    return;
  if (nosyncpatch)
    return;
  /* In application space, only a program starting (its image at &8000):
     FileSwitch also synchronises any file loaded with OS_File's "this is
     code" flag (R3 bit 31), and a program that loads module files that
     way into a buffer and writes them out again (the DDE's ROM build)
     wrote out this module's patches: a 5.31-18 ROM built on RISC OS on
     Linux had 123 of them in 15 modules, and under QEMU it stopped at
     once. Code in a buffer that is run after all still works, through
     the traps. */
  if (hi <= 0x20000000u && !(lo <= 0x8000u && hi > 0x8014u))
    {
      /* Elsewhere in application space: what a program or BASIC wrote
	 and synchronises itself is code (BASIC's assembler: Iris's
	 AlignExOff assembles SWI OS_EnterOS, MRC and MSR CPSR_c, #&10
	 into a DIM block; left unpatched (0.20, 0.21), the MSR did nothing
	 natively, the task stayed in SVC mode and Iris stopped as it
	 started). Left: FileSwitch's (a file loaded with OS_File's
	 "this is code" flag, R3 bit 31, into a buffer) and a program's own
	 for memory outside its image: both may be data written out again
	 (the DDE's ROM build, whose tools ask themselves). A part of the
	 program's own image is always patched. */
      unsigned caller = ns_sync_caller, end = aif_end ();
      ns_sync_caller = 0;		/* (one SWI's: not a later sync's) */
      unsigned c = cao ();
      if (end && lo >= 0x8000u && hi <= end && c == 0x8000u)
	{
	  ns.sync_own++;
	  syncwho_note ('o', caller, c, lo, hi);
	}
      else if (in_fileswitch (caller))
	{
	  ns.sync_app_data++;
	  syncwho_note ('f', caller, c, lo, hi);
	  return;
	}
      else if (caller >= 0x8000u && caller < 0x20000000u)
	{
	  /* the program itself asked, for memory outside its image: its
	     own data (the DDE's ROM build: its tools put module code in
	     buffers and synchronise them; patched, a ROM built on RISC
	     OS on Linux had 123 of these patches in 30 modules' entry
	     veneers). Code there that is run after all works through the
	     traps, as in 0.20. */
	  ns.sync_app_prog++;
	  syncwho_note ('d', caller, c, lo, hi);
	  return;
	}
      else
	{
	  ns.sync_app_code++;
	  syncwho_note ('p', caller, c, lo, hi);
	}
    }
  /* Memory from GCCSDK ARMEABISupport's mmap (its mmap#N areas) is
     never a program's or library's code (SOManager's libraries are in
     its other pools): that's
     where JavaScriptCore (Iris) writes its JIT code, which is Thumb.
     Taken for ARM, some of it looked like MRS/MSR and the like and was
     patched to UDF - in Thumb, a different instruction: Iris's
     JavaScript looped for ever or went wrong (bad pointers). */
  if (in_pmp_area (lo, hi))
    {
      ns.sync_jit++;
      return;
    }
  {
    static int nlog;
    if (lo < 0x20000000u && nlog < 60)
      {
	nlog++;
	dbg ("fault-sync", lo, hi, *(volatile unsigned *) 0x8000u);
      }
  }
  /* A squeezed AIF program as loaded: its header's first word is still
     the BL to the decompressor (it becomes a NOP once unsqueezed, and the
     decompressor synchronises again). What's there now is compressed
     data - which can look like CPS and the like: patched, it unsqueezed
     to garbage (NetSurf). */
  if (skipsqueezed && lo <= 0x8000u && hi > 0x8014u && aif_squeezed ())
    {
      ns.squeezed_skipped++;
      sq_pending = 1;
      return;
    }
  /* the unsqueezed program's own synchronisation covers it */
  if (sq_pending && lo <= 0x8000u && hi > 0x8014u && !aif_squeezed ())
    sq_pending = 0;
  /* a program starting: its image only, not the rest of application
     space after it (a whole-slot synchronisation would scan its heap) */
  if (hi <= 0x20000000u && lo <= 0x8000u && hi > 0x8014u)
    {
      unsigned end = aif_end ();
      if (end && hi > end)
	hi = end;
      if (hi - lo > 0x4000000u)
	ns.sync_big++;
      {
	unsigned caller = ns_sync_caller;
	ns_sync_caller = 0;
	syncwho_note ('s', caller, cao (), lo, hi);
      }
    }
  ns.patched_sync += patch_range (lo, hi);
  syncwho_cur = -1;
}

/* A program's patched instructions written to a file. Copied out of its
   image as data (CMHG's veneer templates: in the DDE's ROM build every C
   module CMHG made had 4 of them), or code saved from where it is
   (BASIC's assembler, then *Save): when a program writes [lo, lo+len) to
   a file (OS_GBPB 1/2, OS_File 0/10), a word there that is UDF #n for a
   live site in application space gets the original word back. A copy
   keeps it; the site itself is put back only while the write is done
   (unpatch_copies returns those, repatch makes them sites again). Only a
   site still patched counts, so other data hardly ever matches. */
#define LIVE_MAX 256
static unsigned live_idx[LIVE_MAX];
static unsigned
unpatch_copies (unsigned lo, unsigned len)
{
  if (!app_sites || !len || lo < 0x8000u || lo >= 0x20000000u
      || len > 0x4000000u || 0x20000000u - lo < len)
    return 0;
  regs e = {{ 0 }};
  if (swix_regs (0x10, &e))			/* OS_GetEnv: r1 = the limit */
    return 0;
  unsigned hi = lo + len, nlive = 0;
  if (hi > e.r[1])
    return 0;
  int any = 0;
  for (unsigned a = (lo + 3) & ~3u; a + 4 <= hi; a += 4)
    {
      unsigned w = *(volatile unsigned *) a;
      if ((w & 0xfff000f0u) != 0xe7f000f0u)
	continue;
      int imm = udf_imm (w);
      if (imm < UDF_BASE || (unsigned) (imm - UDF_BASE) >= nsites)
	continue;
      struct site *t = &sites[imm - UDF_BASE];
      if (t->addr < 0x8000u || t->addr >= 0x20000000u
	  || *(volatile unsigned *) t->addr != w)
	continue;
      if (t->addr == a)
	{
	  if (nlive == LIVE_MAX)
	    continue;
	  live_idx[nlive++] = imm - UDF_BASE;
	}
      *(volatile unsigned *) a = t->orig;
      ns.unpatched_words++;
      any = 1;
    }
  if (any)
    ns.unpatched_writes++;
  return nlive;
}

static void
repatch (unsigned nlive)
{
  for (unsigned k = 0; k < nlive; k++)
    *(volatile unsigned *) sites[live_idx[k]].addr = udf (UDF_BASE + live_idx[k]);
}

/* The write, done here with the sites put back (and patched again after) */
static int
write_unpatched (unsigned swi, unsigned *r, unsigned nlive, unsigned vcpu,
		 unsigned *spsr)
{
  regs v;
  for (int k = 0; k < 10; k++)
    v.r[k] = r[k];
  unsigned saved = *(volatile unsigned *) (vcpu + 24);
  oserror *err = swix_regs (swi, &v);	/* (back through this vector) */
  *(volatile unsigned *) (vcpu + 24) = saved;
  repatch (nlive);
  for (int k = 0; k < 10; k++)
    r[k] = v.r[k];
  if (err)
    {
      r[0] = (unsigned) err;
      *spsr |= CPSR_V;
    }
  else
    *spsr &= ~CPSR_V;
  return 1;
}

/* The few SWIs answered here, from the SWI vector (ns_swi_vector,
   header.s, which passes the rest on): r[0..12] the caller's registers,
   r[13] the return address. 1 if done (V in SPSR_svc as set here). */
int ns_swi_c (unsigned *r);
int
ns_swi_c (unsigned *r)
{
  unsigned vcpu = get_tls ();
  unsigned *spsr = (unsigned *) (vcpu + 24);
  unsigned swi = *(volatile unsigned *) (r[13] - 4) & 0xffffff;
  if ((swi & 0xfdffff) == 0x71)
    swi = r[12];
  else if ((swi & 0xfdffff) == 0x6f)
    swi = r[10];
  switch (swi & 0xfdffff)
    {
    case 0x0C:			/* OS_GBPB 1, 2: write bytes */
    case 0x08:			/* OS_File 0, 10: save a block */
      {
	unsigned lo, len, n, saved;
	if ((swi & 0xfdffff) == 0x0C)
	  {
	    if (r[0] != 1 && r[0] != 2)
	      return 0;
	    lo = r[2], len = r[3];
	  }
	else
	  {
	    if ((r[0] != 0 && r[0] != 10) || r[5] <= r[4])
	      return 0;
	    lo = r[4], len = r[5] - r[4];
	  }
	if (!app_sites)
	  return 0;
	/* (unpatch_copies' OS_GetEnv would leave its own SPSR_svc in the
	   vcpu, and the write would go back in the wrong mode) */
	saved = *(volatile unsigned *) (vcpu + 24);
	n = unpatch_copies (lo, len);
	*(volatile unsigned *) (vcpu + 24) = saved;
	if (n)
	  return write_unpatched (swi & 0xfdffff, r, n, vcpu, spsr);
	return 0;
      }
    case NS_SWI_PMPOP:
      r[0] = pmp_op (r);
      ns.pmp_swis++;
      break;
    case NS_SWI_ABORTRETURN:
      if (!abh_return_swi (r, spsr))
	return 0;
      return 1;
    case 0x58:			/* OS_ReadSysInfo */
      if (r[0] != 9 || r[1] != 7 || !pi_model[0])
	return 0;
      r[0] = (unsigned) pi_model;
      break;
    case 0x68:			/* OS_Memory */
      if ((r[0] & 0xff) == 24)
	{
	  /* CheckMemoryAccess in a PMP area: answered from what is mapped
	     there (the kernel's page tables don't know: kpmp_logop) */
	  int f = kpmp_access (r[1], r[2]);
	  if (f < 0)
	    return 0;
	  r[1] = (unsigned) f;
	  ns.mem24++;
	  break;
	}
      if (!os_memory0 (r))
	return 0;
      ns.mem0++;
      break;
    case 0x3A:			/* OS_ValidateAddress, the same way */
      {
	/* as s.ArthurSWIs ValidateAddress_Code: valid if any "completely"
	   access flag is set, else if Service_ValidateAddress is claimed */
	int f = kpmp_access (r[0], r[1]);
	if (f < 0)
	  return 0;
	int ok = (f & 0xf) != 0;
	if (!ok)
	  {
	    unsigned saved = *(volatile unsigned *) (vcpu + 24);
	    regs v = {{ 0, 0x6D, r[0], r[1] }};
	    ok = !swix_regs (0x30, &v) && v.r[1] == 0;	/* OS_ServiceCall */
	    *(volatile unsigned *) (vcpu + 24) = saved;
	  }
	if (ok)
	  *spsr &= ~CPSR_C;
	else
	  *spsr |= CPSR_C;
	ns.mem24++;
      }
      break;
    case 0x54C80: case 0x54C81: case 0x54C82: case 0x54C83:
    case 0x54C84: case 0x54C85: case 0x54C86: case 0x54C87:
    case 0x54C88: case 0x54C89: case 0x54C8A: case 0x54C8B:
    case 0x54C8C: case 0x54C8D: case 0x54C8E: case 0x54C8F:
      {
	/* Reporter's SWIs without Reporter: GCCSDK's ARMEABISupport 1.05
	   reports an abort no handler of its claims with Report_TextS, its
	   text inline after the SWI. Unknown, the SWI returned to the text
	   and ran it (in SVC mode, its in-handler flag left set, so every
	   later abort was retried for ever: Iris stopped). Ignored here,
	   past the text for Report_TextS. */
	unsigned vcpu = get_tls ();
	unsigned saved = *(volatile unsigned *) (vcpu + 24);
	regs m = {{ 18, (unsigned) "Reporter" }};
	int have = !swix_regs (0x1E, &m);	/* OS_Module 18: look up */
	*(volatile unsigned *) (vcpu + 24) = saved;
	if (have)
	  return 0;
	if ((swi & 0xfdffff) == 0x54C81)
	  {
	    const char *t = (const char *) r[13];
	    while (*t)
	      t++;
	    r[13] = ((unsigned) t + 4) & ~3u;
	  }
	ns.reports++;
      }
      break;
    case 0x59D04:		/* ARMEABISupport_MMapOp: big maps, reported */
      if (debug_faults && r[0] == 0 && r[2] >= 0x400000u)
	{
	  static int nmap;
	  if (nmap < 200)
	    {
	      nmap++;
	      dbg ("fault-mmap", r[2], r[3], r[4]);
	    }
	}
      return 0;
    case 0x66:			/* OS_DynamicArea */
      if (r[0] == 0)
	{
	  /* "Abortable" (bit 16, RISC OS 5.30+): the kernel would pass the
	     area's aborts to its handler through AbortTrap, which the port's
	     abort path never calls. Without it ARMEABISupport 1.08 grows its
	     stacks from its own data abort handler, as on the port's 5.29. */
	  r[4] &= ~(1u << 16);
	  rom_pmp_dirty = 1;		/* (a PMP area, maybe: kpmp_access) */
	  /* creating one: its name, for where it goes (da_place) */
	  const char *nm = (const char *) r[8];
	  int i = 0;
	  if (nm && (unsigned) nm >= 0x8000u)
	    for (; i < (int) sizeof da_name - 1 && nm[i] >= 32; i++)
	      da_name[i] = nm[i];
	  da_name[i] = 0;
	  /* (a SWI from here changes the caller's saved PSR in the vcpu,
	     which the SWI's return uses: keep it) */
	  unsigned vcpu = get_tls ();
	  unsigned saved = *(volatile unsigned *) (vcpu + 24);
	  regs v = {{ (unsigned) "NativeSupport$High", (unsigned) da_high,
		      sizeof da_high - 1, 0, 0 }};
	  if (!swix_regs (OS_ReadVarVal, &v) && v.r[2] < sizeof da_high)
	    da_high[v.r[2]] = 0;
	  else
	    da_high[0] = 0;
	  *(volatile unsigned *) (vcpu + 24) = saved;
	  return 0;
	}
      if (r[0] != 22 || !pmp_da22 (r))
	return 0;
      break;
    default:
      return 0;
    }
  *spsr &= ~CPSR_V;
  return 1;
}

/* ------------------------------------------------------------------------
   Faults: the IOC timer, the lite PMP's abort handlers, DFAR and DFSR */
static int
emulate_ioc (unsigned *R, unsigned addr)
{
  static unsigned latched;
  if (addr < 0x03200000u || addr >= 0x03201000u || (R[16] & CPSR_T)
      || addr == R[15])
    return 0;
  unsigned insn = *(volatile unsigned *) R[15];
  if ((insn & 0x0f200000u) != 0x05000000u || (insn >> 28) != 0xe)
    return 0;
  unsigned rd = (insn >> 12) & 15, rn = (insn >> 16) & 15, off = insn & 0xfff;
  unsigned a = R[rn] + ((insn & (1u << 23)) ? off : -off);
  if (rd == 15 || rn == 15 || a != addr)
    return 0;
  unsigned reg = a & 0xff;
  if (!(insn & (1u << 20)))
    {
      if (reg == 0x4c)
	{
	  unsigned ts[2];
	  lsys3 (SYS_clock_gettime, 1, (unsigned) ts, 0);
	  /* counting down at 2 MHz from 19999 (100 Hz): a whole number of
	     periods each second, so only the nanoseconds matter */
	  latched = 19999 - ts[1] / 500 % 20000;
	}
    }
  else
    R[rd] = reg == 0x40 ? (latched & 0xff) : reg == 0x44 ? (latched >> 8) & 0xff : 0;
  R[15] += 4;
  ns.ioc++;
  return 1;
}

/* With faults reported: the registers, the code around the return address
   (and the faulting pc, if it can be read) and the top of the stack, so a
   fault can be traced to the code that caused it. Reads go through
   probe_call: a word that isn't there ends that line. */
struct dumpargs { const char *what; unsigned addr, words; };
static int
p_dump (void *a)
{
  struct dumpargs *d = a;
  for (unsigned i = 0; i < d->words; i += 2)
    {
      unsigned w0 = *(volatile unsigned *) (d->addr + 4 * i);
      unsigned w1 = *(volatile unsigned *) (d->addr + 4 * i + 4);
      dbg (d->what, d->addr + 4 * i, w0, w1);
    }
  return 1;
}

/* With faults reported: where each module is (base, length, name), to
   place the addresses in the dumps. Logged when NativeSupport starts and
   as each module starts (Service_ModulePreInit): never from a fault,
   when RISC OS may be in no state to be called. */
static void
log_module (unsigned base, unsigned len, unsigned vsites)
{
      char buf[96];
      int k = 0;
      const char *h = "module";
      while (*h)
	buf[k++] = *h++;
      unsigned v[2] = { base, len };
      for (int j = 0; j < 2; j++)
	{
	  buf[k++] = ' ';
	  for (int i = 28; i >= 0; i -= 4)
	    buf[k++] = "0123456789abcdef"[(v[j] >> i) & 15];
	}
      buf[k++] = ' ';
      unsigned t = *(volatile unsigned *) (base + 16);	/* title offset */
      const char *name = t && t < len ? (const char *) (base + t) : "?";
      for (int i = 0; i < 40 && name[i] >= ' '; i++)
	buf[k++] = name[i];
      if (vsites)
	{
	  const char *v = " veneer-psr ";
	  while (*v)
	    buf[k++] = *v++;
	  char d[12];
	  int m = 0;
	  do
	    d[m++] = '0' + vsites % 10;
	  while ((vsites /= 10) && m < 11);
	  while (m)
	    buf[k++] = d[--m];
	}
      buf[k++] = '\n';
      lsys3 (4, 2, (unsigned) buf, k);
}

static void
log_modules (void)
{
  regs r = {{ 12, 0, 0 }};
  for (int n = 0; n < 400; n++)
    {
      r.r[0] = 12;
      if (swix_regs (OS_Module, &r))
	break;
      unsigned base = r.r[3];
      if (base < 0x20C00000u)
	continue;			/* the ROM's own: not wanted */
      log_module (base, *(volatile unsigned *) (base - 4) - 4, 0);
    }
}

/* Whether [lo, lo + n words) is clear of the faulting address's page and
   those next to it: reading the page that faulted (a stack about to be
   grown, say) from inside the handler upsets what handles it next. */
static int
clear_of (unsigned lo, unsigned words, unsigned addr)
{
  unsigned hi = lo + 4 * words;
  unsigned a = addr & ~0xfffu;
  /* nothing to read below &8000 or at the top of the address space (a
     return address of &10 made "lr - 32" wrap round to &FFFFFFF0; that
     read faulted inside the fault handler, and Linux ended RISC OS) */
  if (lo < 0x8000u || hi < lo || hi > 0xfffff000u)
    return 0;
  return !addr || hi <= a - 0x1000 || lo >= a + 0x2000;
}

static void
fault_dump (unsigned *R, unsigned addr)
{

  dbg ("fault-r0-2", R[0], R[1], R[2]);
  dbg ("fault-r3-5", R[3], R[4], R[5]);
  dbg ("fault-r6-8", R[6], R[7], R[8]);
  dbg ("fault-r9-11", R[9], R[10], R[11]);
  dbg ("fault-r12-cpsr", R[12], R[16], 0);
  /* The port's handlers don't have SA_NODEFER: SIGSEGV is blocked in this
     one, and a fault in a read here would end RISC OS instead of failing
     the probe. Let SIGSEGV and SIGBUS in while dumping. */
  unsigned set[2] = { 1u << (SIGSEGV_ - 1) | 1u << (SIGBUS_ - 1), 0 }, old[2];
  int masked = lsys (SYS_rt_sigprocmask, 1, (unsigned) set, (unsigned) old, 8, 0, 0) == 0;
  struct dumpargs d = { "fault-code-lr", (R[14] & ~3u) - 32, 16 };
  if (clear_of (d.addr, d.words, addr))
    probe_call (p_dump, &d);
  d = (struct dumpargs) { "fault-code-pc", (R[15] & ~3u) - 32, 16 };
  if (clear_of (d.addr, d.words, addr))
    probe_call (p_dump, &d);
  d = (struct dumpargs) { "fault-stack", R[13] & ~3u, 24 };
  if (clear_of (d.addr, d.words, addr))
    probe_call (p_dump, &d);
  if (masked)
    lsys (SYS_rt_sigprocmask, 2, (unsigned) old, 0, 8, 0, 0);
}

static void
fault_body (int sig, void *si, void *uc)
{
  unsigned *R = UC_REGS (uc);
  unsigned addr = SI_ADDR (si);
  int code = SI_CODE (si);
  if (probe_sp && R[15] >= self_lo && R[15] < self_hi)
    {
      /* in an emulation (probe_call): it fails */
      R[15] = (unsigned) probe_recover;
      R[16] &= ~CPSR_T;
      ns.probe_faults++;
      return;
    }
  if (emulate_ioc (R, addr))
    return;
  /* The port's mode switches all end: strb r1, [r0, #13] (the new mode
     byte); ...; tst r1, #&220; strne r0, [r0, -r0] (T or E: stop); msr
     CPSR_fs, r3; ldmdb r2, {r0-r3, ip, pc}. A return to Thumb code (an
     interrupt or callback in Iris's JavaScript) arrives with T in the PSR,
     from more routes than one; the last load interworks, so instead of
     stopping: T out of r1 and the mode byte, bit 0 into the pc it loads. */
  if (R[15] == SETCPSR_TCHECK && addr == 0 && ns.tchk_ok
      && (R[1] & CPSR_T) && !(R[1] & 0x200u))
    {
      unsigned v = R[0];
      if (R[1] & CPSR_IT)
	{
	  /* Into the middle of an IT block (a Thumb-2 load or store in one
	     faulted, and RISC OS mapped the page): only an exception return
	     sets ITSTATE, so finish the mode switch here and return through
	     the signal frame, as the port's own handler does (HAL,
	     SetCPSR_Finish): the mode byte, the flags from r3, T and
	     ITSTATE from r1, then ldmdb r2, {r0-r3, ip, pc}. An
	     interworking load ran the rest of the block unconditionally
	     (the web browser's and Iris's WebKit, Thumb-2) */
	  unsigned *t = (unsigned *) R[2];
	  *(volatile uint8_t *) (v + VCPU_CPSR) = R[1] & 0xffu & ~CPSR_T;
	  R[16] = (R[16] & ~(CPSR_APSR | CPSR_IT | CPSR_T))
		  | (R[3] & CPSR_APSR) | (R[1] & (CPSR_IT | CPSR_T));
	  R[0] = t[-6];
	  R[1] = t[-5];
	  R[2] = t[-4];
	  R[3] = t[-3];
	  R[12] = t[-2];
	  R[15] = t[-1] & ~1u;
	  ns.it_returns++;
	  return;
	}
      R[1] &= ~CPSR_T;
      *(volatile uint8_t *) (v + VCPU_CPSR) &= ~CPSR_T;
      *(volatile unsigned *) (R[2] - 4) |= 1;
      R[15] += 4;
      ns.thumb_returns++;
      return;
    }
  if (abh_start (R, sig, code, addr))
    {
      ns.aborts++;
      return;
    }
  /* DFSR: the write bit, from the ESR a 64-bit kernel gives (data
     abort: bit 6) or the FSR a 32-bit one does (bit 11); the status from
     the kind of fault */
  unsigned err = UC_ERROR (uc), ec = err >> 26;
  unsigned wnr = (ec == 0x24 || ec == 0x25) ? (err >> 6) & 1 : (err >> 11) & 1;
  ns.last_dfar = addr;
  ns.last_dfsr = wnr << 11
		 | (sig == SIGBUS_ ? (code == 1 ? 0x1 : 0x8) : code == 2 ? 0xf : 0x7);
  dbg ("fault", addr, R[15], ns.last_dfsr | sig << 16);
  if (debug_faults)
    {
      dbg ("fault-regs", R[14], R[13], *(volatile uint8_t *) (get_tls () + VCPU_CPSR));
      /* the kernel's handler variables: UndHan, PAbHan, DAbHan (both ROMs: ZeroPage &20510000) */
      dbg ("fault-hans", *(volatile unsigned *) 0x20510120u, *(volatile unsigned *) 0x20510124u, *(volatile unsigned *) 0x20510128u);
      fault_dump (R, addr);
    }
  chain (sig == SIGBUS_ ? &ns.old_bus : &ns.old_segv, sig, si, uc);
}

/* ------------------------------------------------------------------------
   The ROM's PMP stubs, done here (qemu/ patch item 6) */
#define KPMP_MAX 64
#define KPMP_PHYS_BASE 0x00400000u
#define DANODE_NUMBER 4
#define DANODE_BASE 8
#define DANODE_FLAGS 12
#define DANODE_SIZE 16
#define DANODE_MAXSIZE 20
#define DANODE_TITLE 32
#define DANODE_PMPSIZE 52
#define DANODE_PMPMAXSIZE 56
static struct kpmp
{
  int da, fd;
  unsigned node, base, lpages, pmax, claimed, mapped;
  int mmap;			/* named mmap#N (ARMEABISupport's mmap) */
  int *phys, *log, *pool;
  unsigned *flags;
} kp[KPMP_MAX];
static unsigned kpmp_stub[4], kpmp_err, kpmp_nextphys = KPMP_PHYS_BASE;

#define RD(a) (*(volatile unsigned *) (a))

/* Where a BL at a goes */
static unsigned
bl_target (unsigned a)
{
  unsigned w = RD (a);
  if ((w & 0xff000000u) != 0xeb000000u)
    return 0;
  return a + 8 + ((int) (w << 8) >> 6);
}

/* The kernel sites patched, found by their code rather than by address,
   so that newer ROMs (RISC OS Open's 5.31 merged into the port) work too.
   Each must match exactly once in the OS image, or it isn't used. */
static void
find_kernel_sites (void)
{
  static const unsigned imb[6] = { 0xe92d500fu, 0xe3a01000u, 0xe3e02003u,
				   0xe1a0e00fu, 0xe59ff418u, 0xe8bd900fu };
  unsigned da = 0, nda = 0, ab = 0, nab = 0, im = 0, nim = 0;
  for (unsigned a = 0x20810000u + 64; a < 0x20C00000u - 64; a += 4)
    {
      unsigned w = RD (a);
      if ((w & 0xff000000u) == 0xeb000000u
	  && RD (a - 32) == 0xe3a00000u && RD (a - 28) == 0xe1a01005u
	  && RD (a - 12) == 0xe3e04000u && RD (a - 4) == 0xe3a070c0u)
	da = a, nda++;
      if (w == 0xe92d500fu)
	{
	  if (RD (a + 4) == 0xe3a010d7u && bl_target (a + 8) == SETCPSR_C_UNSTACK
	      && RD (a + 12) == 0xe321f0d7u && RD (a - 0x2c) == 0x1a000009u)
	    ab = a, nab++;
	  int i = 1;
	  while (i < 6 && RD (a + 4 * i) == imb[i])
	    i++;
	  if (i == 6)
	    im = a, nim++;
	}
    }
  DAADDR_BL = nda == 1 ? da : 0;
  ABTDUMP_SW = nab == 1 ? ab : 0;
  IMB_FULL = nim == 1 ? im : 0;
}

static void
kpmp_free (struct kpmp *p)
{
  if (p->fd > 0)
    lsys3 (SYS_close, p->fd, 0, 0);
  rma_free (p->phys);
  rma_free (p->log);
  rma_free (p->pool);
  rma_free (p->flags);
  memset (p, 0, sizeof *p);
}

static unsigned
kpmp_find_node (int da)
{
  unsigned base = SYSHEAP_BASE;
  if (RD (base) != 0x70616548u)		/* "Heap" */
    return 0;
  unsigned end = base + RD (base + 12);
  for (unsigned n = base + 16; n + 60 <= end; n += 4)
    {
      volatile unsigned *w = (volatile unsigned *) n;
      if (w[1] == (unsigned) da && (w[3] & (1u << 20)) && w[2]
	  && !(w[2] & 0xfff) && w[5] && !(w[5] & 0xfff) && w[8])
	return n;
    }
  return 0;
}

static int *
words (unsigned n, int fill)
{
  int *p = rma_claim ((n ? n : 1) * 4);
  if (p)
    memset (p, fill, (n ? n : 1) * 4);
  return p;
}

static struct kpmp *
kpmp_get (int da)
{
  struct kpmp *p, *spare = NULL;
  for (int i = 0; i < KPMP_MAX; i++)
    {
      p = &kp[i];
      if (!p->node)
	{
	  if (!spare)
	    spare = p;
	  continue;
	}
      if (p->da != da)
	continue;
      if (RD (p->node + DANODE_NUMBER) == (unsigned) da
	  && RD (p->node + DANODE_BASE) == p->base
	  && (RD (p->node + DANODE_FLAGS) & (1u << 20)))
	return p;
      kpmp_free (p);
      if (!spare)
	spare = p;
    }
  if (!spare)
    return NULL;
  unsigned node = kpmp_find_node (da);
  if (!node)
    return NULL;
  p = spare;
  p->da = da;
  p->node = node;
  p->base = RD (node + DANODE_BASE);
  p->lpages = RD (node + DANODE_MAXSIZE) >> 12;
  p->pmax = RD (node + DANODE_PMPMAXSIZE);
  {
    const char *t = (const char *) RD (node + DANODE_TITLE);
    p->mmap = t && (unsigned) t >= 0x8000u && t[0] == 'm' && t[1] == 'm'
	      && t[2] == 'a' && t[3] == 'p' && t[4] == '#';
  }
  p->fd = lsys3 (SYS_memfd_create, (unsigned) "RISC OS PMP", 1, 0);
  p->phys = words (p->pmax, 0xff);
  p->log = words (p->pmax, 0xff);
  p->pool = words (p->lpages, 0xff);
  p->flags = (unsigned *) words (p->pmax, 0);
  if (p->fd < 0 || lsys3 (SYS_ftruncate, p->fd, p->pmax << 12, 0) < 0
      || !p->phys || !p->log || !p->pool || !p->flags)
    {
      kpmp_free (p);
      return NULL;
    }
  return p;
}

static void
kpmp_sync (struct kpmp *p)
{
  /* the whole area: the port checks addresses against base + size */
  RD (p->node + DANODE_SIZE) = p->lpages << 12;
  RD (p->node + DANODE_PMPSIZE) = p->claimed;
  RD (p->node + DANODE_PMPMAXSIZE) = p->pmax;
}

static void
kpmp_unmap_log (struct kpmp *p, unsigned l)
{
  int q = p->pool[l];
  if (q < 0)
    return;
  map_fixed (p->base + (l << 12), 4096, 0, 0x32, -1, 0);
  p->pool[l] = -1;
  p->log[q] = -1;
  p->mapped--;
}

static int
kpmp_physop (struct kpmp *p, unsigned *e)
{
  unsigned q = e[0];
  if (q >= p->pmax)
    return 0;
  if (e[1] == 0xffffffffu)
    {
      if (p->phys[q] >= 0)
	{
	  if (p->log[q] >= 0)
	    kpmp_unmap_log (p, p->log[q]);
	  lsys (SYS_fallocate, p->fd, 3, q << 12, q >> 20, 4096, 0);
	  p->phys[q] = -1;
	  p->flags[q] = 0;
	  p->claimed--;
	}
    }
  else if (p->phys[q] < 0)
    {
      p->phys[q] = e[1] == 0xfffffffeu ? (int) kpmp_nextphys++ : (int) e[1];
      p->flags[q] = e[2];
      p->claimed++;
    }
  return 1;
}

static int
kpmp_logop (struct kpmp *p, unsigned *e)
{
  unsigned l = e[0], q = e[1];
  if (l >= p->lpages)
    return 0;
  if (q == 0xffffffffu)
    {
      kpmp_unmap_log (p, l);
      return 1;
    }
  if (q >= p->pmax || p->phys[q] < 0)
    return 0;
  if (p->pool[l] == (int) q)
    return 1;
  if (p->log[q] >= 0)
    kpmp_unmap_log (p, p->log[q]);
  kpmp_unmap_log (p, l);
  int prot = (e[2] & (1u << 15)) ? 0 : 7;
  unsigned a = p->base + (l << 12);
  if (map_fixed (a, 4096, prot, 0x11, p->fd, q) != a)
    return 0;
  p->pool[l] = q;
  p->log[q] = l;
  p->flags[q] = e[2];
  p->mapped++;
  return 1;
}

static int
kpmp_resize (struct kpmp *p, int change, unsigned *done)
{
  long long n = (long long) p->pmax + change;
  if (n < 0)
    n = 0;
  while (n < p->pmax && p->phys[p->pmax - 1] < 0 && p->pmax > n)
    p->pmax--;
  *done = 0;
  if (n > p->pmax)
    {
      unsigned old = p->pmax;
      if (n > 0x7ffff)
	n = 0x7ffff;		/* (a 2GB pool) */
      int *ph = words (n, 0xff), *lg = words (n, 0xff), *fl = words (n, 0);
      if (!ph || !lg || !fl
	  || lsys3 (SYS_ftruncate, p->fd, (unsigned) n << 12, 0) < 0)
	{
	  rma_free (ph);
	  rma_free (lg);
	  rma_free (fl);
	  return 0;
	}
      memcpy (ph, p->phys, old * 4);
      memcpy (lg, p->log, old * 4);
      memcpy (fl, p->flags, old * 4);
      rma_free (p->phys);
      rma_free (p->log);
      rma_free (p->flags);
      p->phys = ph;
      p->log = lg;
      p->flags = (unsigned *) fl;
      p->pmax = n;
      *done = n - old;
    }
  return 1;
}

static void
kpmp_getpages (struct kpmp *p, unsigned *e)
{
  int q = -1;
  if (e[0] != 0xffffffffu)
    q = e[0] < p->pmax ? (int) e[0] : -1;
  else if (e[2] != 0xffffffffu)
    q = e[2] < p->lpages ? p->pool[e[2]] : -1;
  else if (e[1] != 0xffffffffu)
    for (unsigned i = 0; i < p->pmax; i++)
      if (p->phys[i] == (int) e[1])
	{
	  q = i;
	  break;
	}
  if (q < 0 || p->phys[q] < 0)
    {
      if (q >= 0)
	{
	  e[0] = q;
	  e[1] = 0xffffffffu;
	  e[2] = 0xffffffffu;
	}
      e[3] = 0;
      return;
    }
  e[0] = q;
  e[1] = p->phys[q];
  e[2] = p->log[q];
  e[3] = (RD (p->node + DANODE_FLAGS) & 0xff) | (1u << 20);
}

/* OS_Memory 24 / OS_ValidateAddress in a PMP (physical memory pool) area,
   answered as RISC OS's kernel does from its page tables (s.MemInfo
   CheckMemoryAccess with CMA_CheckL2PT): page by page, mapped or not, with
   the area's access privilege. RISC OS on Linux has no page tables to
   check: a PMP area's pages are mapped from a Linux memory file, either by
   the ROM (c.LinuxC do_PMP_*: 5.31 and later) or here (kpmp_*, the older
   ROM's stubs). The Linux build of the kernel skips that check (it is
   under PagedMemory), so it says the whole area is there, with its
   internal flags CMA_CheckL2PT/CMA_DecodeAP left in bits 26-31; and the
   older ROM says none of it is. Both are wrong for debuggers (EDT) and
   anything else checking addresses: ARMEABISupport 1.08's stacks, its
   mmaps, SOManager's shared libraries. */

/* The CMA access bits (completely: 0-3) for access privilege 0-3, as
   the kernel's MMU_PPLAccess table gives them */
static const unsigned char ap_acc[4] = { 0xf, 0xd, 0xc, 0x5 };

/* The ROM's PMP area nodes (DANode_PMP at +48; c.LinuxC's struct pmp_rec
   after the last pool word: bytes, pcap, lpages, log, pool) */
#define DANODE_PMP 48
#define PMPREC_LPAGES 8
#define PMPREC_POOL 16
#define ROM_PMP_MAX 32
static unsigned rom_pmp_nodes[ROM_PMP_MAX], rom_pmp_n;

static int
rom_pmp_node_ok (unsigned n)
{
  return n && (RD (n + DANODE_FLAGS) & (1u << 20)) && RD (n + DANODE_BASE)
	 && !(RD (n + DANODE_BASE) & 0xfff) && RD (n + DANODE_MAXSIZE)
	 && !(RD (n + DANODE_MAXSIZE) & 0xfff);
}

/* The ROM PMP area node whose logical range has lo, or 0. The nodes are
   found in the system heap once, and again after a dynamic area is made
   (OS_DynamicArea 0, below), so other addresses cost a short loop. */
static unsigned
rom_pmp_node (unsigned lo)
{
  if (rom_pmp_dirty)
    {
      rom_pmp_dirty = 0;
      rom_pmp_n = 0;
      unsigned base = SYSHEAP_BASE;
      if (RD (base) != 0x70616548u)	/* "Heap" */
	return 0;
      unsigned end = base + RD (base + 12);
      for (unsigned n = base + 16; n + 60 <= end && rom_pmp_n < ROM_PMP_MAX;
	   n += 4)
	{
	  volatile unsigned *w = (volatile unsigned *) n;
	  if ((w[3] & (1u << 20)) && w[2] && !(w[2] & 0xfff) && w[5]
	      && !(w[5] & 0xfff) && w[8] && w[1] < 0x100000u)
	    rom_pmp_nodes[rom_pmp_n++] = n;
	}
    }
  for (unsigned i = 0; i < rom_pmp_n; i++)
    {
      unsigned n = rom_pmp_nodes[i];
      if (rom_pmp_node_ok (n) && lo >= RD (n + DANODE_BASE)
	  && lo - RD (n + DANODE_BASE) < RD (n + DANODE_MAXSIZE))
	return n;
    }
  return 0;
}

/* Whole-range flags ANDed and part-of-range flags ORed over the pages of
   lo..hi (inclusive end hi), as CMA_AddRange does; pool(l) gives each
   page's pool page, or < 0 if unmapped */
static unsigned
cma_pages (unsigned base, unsigned lo, unsigned hi, unsigned ap,
	   int (*pool) (void *, unsigned), void *ctx)
{
  unsigned acc = ap_acc[ap & 3], whole = 0xf, part = 0, any = 0, all = 1;
  for (unsigned l = (lo - base) >> 12; l <= (hi - base) >> 12; l++)
    if (pool (ctx, l) >= 0)
      part |= acc, any = 1;
    else
      whole = 0, all = 0;
  /* executable where readable (no XN on RISC OS on Linux): unmapped
     pages are XN, as the kernel's CMA_Done makes them */
  return (all ? whole : 0) | (part << 4) | (any ? 0 : 0xc00) | (all ? 0 : 0xc000);
}

static int
kp_pool (void *ctx, unsigned l)
{
  struct kpmp *p = ctx;
  int q = p->pool[l];
  return q >= 0 && !(p->flags[q] & (1u << 15)) ? q : -1;
}

static int
lite_pool (void *ctx, unsigned l)
{
  int i = (int) ctx;
  return pmp[i].map && l < pmp[i].pages && pmp[i].map[l] ? 0 : -1;
}

static int
rom_pool (void *ctx, unsigned l)
{
  unsigned rec = (unsigned) ctx;
  if (!rec || l >= RD (rec + PMPREC_LPAGES))
    return -1;
  return ((volatile int *) RD (rec + PMPREC_POOL))[l];
}

/* The OS_Memory 24 flags for lo..hi (hi exclusive) wholly inside one PMP
   area, or -1 for the kernel to answer */
static int
kpmp_access (unsigned lo, unsigned hi)
{
  hi = hi > lo ? hi - 1 : lo;
  for (int i = 0; i < PMP_MAX; i++)	/* the test bed's ARMEABISupport's */
    if (pmp[i].fd > 0 && lo >= pmp[i].base
	&& hi - pmp[i].base < pmp[i].pages << 12)
      return cma_pages (pmp[i].base, lo, hi, 0, lite_pool, (void *) i);
  for (int i = 0; i < KPMP_MAX; i++)
    {
      struct kpmp *p = &kp[i];
      if (p->node && lo >= p->base && hi - p->base < p->lpages << 12)
	return cma_pages (p->base, lo, hi, RD (p->node + DANODE_FLAGS) & 0xf,
			  kp_pool, p);
    }
  unsigned n = rom_pmp_node (lo);
  if (!n)
    return -1;
  unsigned b = RD (n + DANODE_BASE), max = RD (n + DANODE_MAXSIZE);
  if (hi - b >= max)
    return -1;
  unsigned phys = RD (n + DANODE_PMP), rec = 0;
  if (phys && phys < 0xfffff000u)
    {
      rec = RD (phys + 4 * RD (n + DANODE_PMPMAXSIZE));
      if (rec == 0xffffffffu)
	rec = 0;
    }
  return cma_pages (b, lo, hi, RD (n + DANODE_FLAGS) & 0xf, rom_pool,
		    (void *) rec);
}

/* lo..hi in one of ARMEABISupport's mmap areas ("mmap#N"; not its other
   pools: SOManager's shared libraries are in those) */
static int
in_pmp_area (unsigned lo, unsigned hi)
{
  for (int i = 0; i < KPMP_MAX; i++)
    if (kp[i].node && kp[i].mmap && lo >= kp[i].base
	&& lo - kp[i].base < kp[i].lpages << 12)
      return 1;
  /* On a ROM that makes PMP areas itself (5.31: c.LinuxC), kp[] has
     none of them: the ROM's own nodes, by their title. Without this,
     JavaScriptCore's JIT code (Thumb-2) in mmap#N was patched as ARM
     (the web browser's MiniBrowser: links dead; Iris 1.039: its web
     process aborted in JSC on bbc.co.uk) */
  unsigned n = rom_pmp_node (lo);
  if (n)
    {
      const char *t = (const char *) RD (n + DANODE_TITLE);
      if (t && (unsigned) t >= 0x8000u && t[0] == 'm' && t[1] == 'm'
	  && t[2] == 'a' && t[3] == 'p' && t[4] == '#')
	return 1;
    }
  (void) hi;
  return 0;
}

/* From kpmp_N (header.s): r = the caller's r0-r12. 0 if done, else r[0]
   is set to the error. */
int kpmp_call (int op, unsigned *r);
int
kpmp_call (int op, unsigned *r)
{
  struct kpmp *p = kpmp_get (r[1]);
  int ok = p != NULL;
  ns.kpmp_ops++;
  if (ok && op == 2)
    {
      unsigned done = 0;
      ok = kpmp_resize (p, (int) r[2], &done);
      r[2] = done;
    }
  else if (ok)
    {
      unsigned r2 = r[2], r3 = r[3];
      while (r[3])
	{
	  unsigned *e = (unsigned *) r[2];
	  if (op == 3)
	    {
	      kpmp_getpages (p, e);
	      r[2] += 16;
	      r[3]--;
	      continue;
	    }
	  if (!(op == 0 ? kpmp_physop (p, e) : kpmp_logop (p, e)))
	    {
	      ok = 0;
	      break;
	    }
	  r[2] += 12;
	  r[3]--;
	}
      if (op == 3)
	{
	  r[2] = r2;
	  r[3] = r3;
	}
    }
  if (p)
    kpmp_sync (p);
  if (ok)
    return 0;
  r[0] = kpmp_err;
  return 1;
}

/* ------------------------------------------------------------------------
   Starting */
ERROR_BLOCK (bad_rom, 0x823800, "NativeSupport: this isn't the ROM it knows (RISC OS 5.29 for Linux)");
ERROR_BLOCK (no_memory, 0x823801, "NativeSupport: not enough memory");
ERROR_BLOCK (no_kill, 0x823802, "NativeSupport can't be removed while RISC OS runs natively");
/* (0x823803: header.s, a NativeSupport SWI that reached the module's own
   SWI handler, not the SWI vector) */

extern void kpmp_0 (void), kpmp_1 (void), kpmp_2 (void), kpmp_3 (void);
extern char module_start[];

static int
mem_eq (unsigned a, const char *s, unsigned n)
{
  for (unsigned i = 0; i < n; i++)
    if (*(volatile char *) (a + i) != s[i])
      return 0;
  return 1;
}

/* The ROM: its OS image is mapped from 0x20810000 to 0x20C00000 */
#define ROMOS_LO 0x20810000u
#define ROMOS_HI 0x20C00000u

static void
find_kpmp_stubs (void)
{
  /* (no pointers in initialised data: only the GOT is relocated) */
  static const char names[4][24] = {
    "DynArea_PMP_PhysOp\r\n", "DynArea_PMP_LogOp\r\n",
    "DynArea_PMP_Resize\r\n", "DynArea_PMP_GetPages\r\n",
  };
  static const char bad[] = "\xea\x01\x00\x00" "BadParm";
  for (int n = 0; n < 4; n++)
    {
      unsigned len = strlen (names[n]);
      for (unsigned a = ROMOS_LO + 8; a + len <= ROMOS_HI; a += 4)
	if (*(volatile char *) a == 'D' && mem_eq (a, names[n], len)
	    && RD (a - 8) == 0xe52de004u)	/* push {lr} */
	  {
	    kpmp_stub[n] = a - 8;
	    break;
	  }
    }
  for (unsigned a = ROMOS_LO; a + sizeof bad <= ROMOS_HI; a += 4)
    if (*(volatile char *) a == '\xea' && mem_eq (a, bad, sizeof bad))
      {
	kpmp_err = a;
	break;
      }
}

static void
patch_kpmp_stubs (void)
{
  for (int n = 0; n < 4; n++)
    {
      unsigned a = kpmp_stub[n];
      void (*fn) (void) = n == 0 ? kpmp_0 : n == 1 ? kpmp_1 : n == 2 ? kpmp_2 : kpmp_3;
      if (!a || !kpmp_err)
	continue;
      if (!rom_writable (a, a + 8, 1))
	continue;
      RD (a) = 0xe51ff004u;			/* ldr pc, [pc, #-4] */
      RD (a + 4) = (unsigned) fn;
      rom_writable (a, a + 8, 0);
      sync_code (a, a + 8);
    }
}

/* IXFS's getdents64: "mov r7, #217" then, a few instructions on, "bl
   __PVirt_LinuxSyscall": the BL is patched */
static void
patch_getdents (void)
{
  for (unsigned a = ROMOS_LO; a < ROMOS_HI - 32; a += 4)
    {
      if (RD (a) != 0xe3a070d9u)
	continue;
      for (unsigned b = a + 4; b < a + 20; b += 4)
	{
	  unsigned w = RD (b);
	  if ((w & 0x0f000000u) != 0x0b000000u)
	    continue;
	  int off = (int) (w << 8) >> 6;
	  if (b + 8 + off != ns.sys_entry)
	    continue;
	  if (rom_writable (b, b + 4, 1))
	    {
	      site_add ((unsigned *) b, KIND_GETDENTS);
	      rom_writable (b, b + 4, 0);
	      sync_code (b, b + 4);
	      ns.patched_rom++;
	    }
	  break;
	}
    }
}

/* The kernel's (and HAL's) cacheflush calls: "ldr r7, =0xF0002" then,
   a few instructions on, "bl __PVirt_LinuxSyscall" */
static void
patch_cacheflush (unsigned lo, unsigned hi)
{
  for (unsigned a = lo; a < hi - 16; a += 4)
    {
      unsigned w = RD (a);
      if ((w & 0x0f7ff000u) != 0x051f7000u)
	continue;
      unsigned off = w & 0xfff;
      unsigned lit = a + 8 + ((w & (1u << 23)) ? off : -off);
      if (lit < lo || lit >= hi || RD (lit) != 0xf0002u)
	continue;
      for (unsigned b = a + 4; b < a + 16; b += 4)
	{
	  unsigned x = RD (b);
	  if ((x & 0x0f000000u) != 0x0b000000u)
	    continue;
	  int o = (int) (x << 8) >> 6;
	  if (b + 8 + o != ns.sys_entry)
	    break;
	  if (rom_writable (b, b + 4, 1))
	    {
	      site_add ((unsigned *) b, KIND_CACHEFLUSH);
	      rom_writable (b, b + 4, 0);
	      sync_code (b, b + 4);
	      ns.patched_rom++;
	      ns.flush_sites++;
	    }
	  break;
	}
    }
}

/* The kernel's do_IMB_Full (a whole-cache synchronisation: Linux's
   cacheflush over everything, data and screen memory too, which mustn't be
   scanned for code):
     stmfd sp!, {r0-r3, ip, lr}; mov r1, #0; mvn r2, #3; mov lr, pc;
     ldr pc, [pc, #&418]; ldmfd sp!, {r0-r3, ip, pc}
   Its mov and ldmfd mark the cacheflush calls between as whole ones. */
static void
patch_imb_full (void)
{
  static const unsigned sig[6] = { 0xe92d500fu, 0xe3a01000u, 0xe3e02003u,
				   0xe1a0e00fu, 0xe59ff418u, 0xe8bd900fu };
  for (int i = 0; i < 6; i++)
    if (!IMB_FULL || RD (IMB_FULL + 4 * i) != sig[i])
      return;
  if (!rom_writable (IMB_FULL, IMB_FULL + 24, 1))
    return;
  site_add ((unsigned *) (IMB_FULL + 4), KIND_FULL_BEGIN);
  site_add ((unsigned *) (IMB_FULL + 20), KIND_FULL_END);
  rom_writable (IMB_FULL, IMB_FULL + 24, 0);
  sync_code (IMB_FULL, IMB_FULL + 24);
  ns.patched_rom += 2;
  ns.imb_full = 1;
}

/* The ROM modules emulated as disc code (the sound modules), and the
   modules already loaded from disc */
static int
title_is (unsigned base, const char *t)
{
  unsigned off = RD (base + 16);
  return off && mem_eq (base + off, t, strlen (t) + 1);
}

static void
patch_modules (unsigned self)
{
  static const char sound[5][16] = {
    "SoundDMA", "SoundChannels", "SoundScheduler", "StringLib", "Percussion"
  };
  regs r = {{ 12, 0, 0 }};
  for (;;)
    {
      r.r[0] = 12;		/* r1, r2: the next module and incarnation */
      if (swix_regs (OS_Module, &r))
	break;
      unsigned base = r.r[3];
      unsigned len = RD (base - 4) - 4;
      if (base >= ROMOS_LO && base < ROMOS_HI)
	{
	  for (unsigned i = 0; i < sizeof sound / sizeof sound[0]; i++)
	    if (len < 0x100000 && title_is (base, sound[i]) && ns.emul_n < 8)
	      {
		ns.emul_lo[ns.emul_n] = base;
		ns.emul_hi[ns.emul_n] = base + len;
		ns.emul_n++;
		if (rom_writable (base, base + len, 1))
		  {
		    ns.patched_rom += patch_range (base, base + len);
		    rom_writable (base, base + len, 0);
		    sync_code (base, base + len);
		  }
	      }
	}
      else if (base != self && len < 0x1000000)
	{
	  ns.patched += patch_range (base, base + len);
	  sync_code (base, base + len);
	}
    }
  if (traptest && rom_writable (ROMOS_LO, ROMOS_HI, 1))
    {
      /* test: make the ROM's "LDMNE sp!, {...}^" after TEQ pc, pc trap,
	 as an A78C does, to try the way back from it */
      for (unsigned *p = (unsigned *) ROMOS_LO + 1;
	   p < (unsigned *) ROMOS_HI; p++)
	if (p[-1] == 0xe13f000fu && (p[0] & 0xffff0000u) == 0x18fd0000u
	    && (p[0] & (1u << 22)))
	  ns.patched_rom += site_add (p, KIND_PSR);
      rom_writable (ROMOS_LO, ROMOS_HI, 0);
      sync_code (ROMOS_LO, ROMOS_HI);
    }
}

static unsigned
read_midr (void)
{
  char b[64];
  int fd = lsys3 (SYS_open,
		  (unsigned) "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1",
		  0x80000, 0);
  unsigned v = 0x410fd083u;			/* Cortex-A72 */
  if (fd >= 0)
    {
      int n = lsys3 (SYS_read, fd, (unsigned) b, sizeof b - 1);
      lsys3 (SYS_close, fd, 0, 0);
      if (n > 2)
	{
	  b[n] = 0;
	  const char *s = b + (b[0] == '0' && b[1] == 'x' ? 2 : 0);
	  unsigned long long x = 0;
	  for (; *s; s++)
	    {
	      char c = *s;
	      if (c >= '0' && c <= '9') x = x * 16 + (c - '0');
	      else if (c >= 'a' && c <= 'f') x = x * 16 + (c - 'a' + 10);
	      else break;
	    }
	  if (x)
	    v = (unsigned) x;
	}
    }
  return v;
}

static int
set_handler (int sig, sighandler *h, struct ksigaction *old)
{
  if (lsys (SYS_rt_sigaction, sig, 0, (unsigned) old, 8, 0, 0) < 0
      || old->restorer + 4 != ns.sys_entry)
    return 0;
  struct ksigaction a = *old;
  a.handler = (unsigned) h;
  return lsys (SYS_rt_sigaction, sig, (unsigned) &a, 0, 8, 0, 0) == 0;
}

extern void ns_swi_vector (void);
extern unsigned ns_old_swi_vector, ns_movs_pc_lr;

/* The port's PVOps HAL device (type &30A, as its InitPVOps finds it): its
   routines from +64 (PatchCode, FastSWI, SWI, LinuxSyscall, GetCPSR,
   GetSPSR, LoadSaveBlock, LoadUsr, LoadUsrRegisters, MOVS_PC_LR...) */
static int
claim_swi_vector (void)
{
  regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
  if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2])
    return 0;
  unsigned dev = h.r[2];
  if (RD (dev + 76) != ns.sys_entry)		/* its LinuxSyscall: ours? */
    return 0;
  ns_movs_pc_lr = RD (dev + 100);
  /* the handler now (the vcpu's vectors: +8), set before the claim: an
     interrupt can call a SWI as soon as it's made */
  unsigned *vectors = (unsigned *) RD (get_tls () + 8);
  ns_old_swi_vector = vectors[1];
  regs v = {{ 0x102, (unsigned) ns_swi_vector }};	/* claim the SWI vector */
  if (swix_regs (0x69 /* OS_ClaimProcessorVector */, &v))
    return 0;
  return v.r[1] == ns_old_swi_vector;
}

/* The hardware platform name (OS_ReadSysInfo 9, item 7): RISC OS on a
   Raspberry Pi gives the board's name; the port gives none. Running
   natively on a Raspberry Pi, the name Linux's device tree has for the
   board (e.g. "Raspberry Pi 4 Model B Rev 1.4"); on anything else, nothing
   changes. */
static void
read_pi_model (void)
{
  int fd = lsys3 (SYS_open, (unsigned) "/proc/device-tree/model", 0x80000, 0);
  if (fd < 0)
    return;
  int n = lsys3 (SYS_read, fd, (unsigned) pi_model, sizeof pi_model - 1);
  lsys3 (SYS_close, fd, 0, 0);
  if (n < 0)
    n = 0;
  pi_model[n] = 0;
  static const char pfx[] = "Raspberry Pi";
  for (unsigned i = 0; i < sizeof pfx - 1; i++)
    if (pi_model[i] != pfx[i])
      {
	pi_model[0] = 0;
	return;
      }
}

/* The fast SWI entry of riscos-linux's kernels (prctl PR_RISCOS_SWI): the
   ROM's SWIs go to its SIGSYS handler straight from Linux's system call
   entry, with the registers it reads written on its signal stack, instead
   of through seccomp and a signal (about 2 microseconds a SWI on a Pi 4).
   Other kernels refuse it, and SWIs go the usual way. */
#define PR_RISCOS_SWI	0x524f5301u

/* The fast FPA entry (prctl PR_RISCOS_UND): FPA instructions go to
   ns_und_fast (header.s) straight from Linux's undefined instruction
   trap, instead of through a SIGILL, this module's SIGILL handler and the
   port's, and a sigreturn. ns_und_fast needs the ROM's table of the vcpu's
   mode banks, found in its SWI_handler ("ADD lr, lr, #table - . - 12;
   LDRB lr, [pc, lr]") and checked. -1: not in this kernel or this ROM. */
#define PR_RISCOS_UND	0x524f5302u
extern char ns_und_fast[];
extern unsigned ns_vmode_offsets;

static unsigned
arm_imm (unsigned w)
{
  unsigned r = ((w >> 8) & 15) * 2, v = w & 255;
  return r ? (v >> r) | (v << (32 - r)) : v;
}

static int
find_vmode_offsets (void)
{
  unsigned h = ns.swi_handler;
  if (!h)
    return 0;
  for (unsigned i = 0; i < 12; i++)
    {
      unsigned a = h + i * 4;
      if ((RD (a) & 0xfffff000u) == 0xe28ee000u && RD (a + 4) == 0xe7dfe00eu)
	{
	  const volatile uint8_t *t = (const volatile uint8_t *) (a + 4 + 8 + arm_imm (RD (a)));
	  /* usr, svc, und, abt: the vcpu's banks at 28, 16, 64, 76 */
	  if (t[0x10] == 28 && t[0x13] == 16 && t[0x1b] == 64 && t[0x17] == 76)
	    {
	      ns_vmode_offsets = (unsigned) t;
	      return 1;
	    }
	  return 0;
	}
    }
  return 0;
}

static void
fastfpa_set (int on)
{
  if (ns.fastfpa < 0)
    return;
  if (on && !ns_vmode_offsets && !find_vmode_offsets ())
    {
      ns.fastfpa = -1;
      return;
    }
  if (lsys (SYS_prctl, PR_RISCOS_UND, on ? (unsigned) ns_und_fast : 0, 0, 0, 0, 0) == 0)
    ns.fastfpa = on;
  else if (on)
    ns.fastfpa = -1;
}

static void
fastswi_set (int on)
{
  if (ns.fastswi < 0)
    return;
  if (lsys (SYS_prctl, PR_RISCOS_SWI, on ? ns.swi_handler : 0, ns.sys_entry + 4, 0, 0, 0) == 0)
    ns.fastswi = on;
  else if (on)
    ns.fastswi = -1;
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance;
  if (ns.active)
    return NULL;
  /* Under QEMU? It answers NativeSupport_PMPOp itself (99: no such
     operation). */
  regs r = {{ 99 }};
  if (!swix_regs (NS_SWI_PMPOP, &r))
    return NULL;
  /* The port's Linux system call: after signal_exit, the restorer of the
     ROM's signal handlers ("mov r7, #173; swi 0; bx lr") */
  struct ksigaction a;
  if (lsys_swi (SYS_rt_sigaction, SIGSYS_, 0, (unsigned) &a, 8) < 0
      || RD (a.restorer) != 0xe3a070adu || RD (a.restorer + 4) != 0xef000000u)
    return ERR (bad_rom);
  ns.sys_entry = a.restorer + 4;
  ns.swi_handler = a.handler;
  ns.psr_ok = RD (SETCPSR_FS_UNSTACK) == 0xe12cf001u
	      && RD (SETCPSR_C_UNSTACK) == 0xe10f3000u
	      && RD (SETCPSR_C_UNSTACK + 4) == 0xe28ec004u;
  ns.lur_ok = RD (LOADUSRREGISTERS) == 0xe1a0900eu;
  find_kernel_sites ();
  ns.daaddr_ok = DAADDR_BL != 0;
  ns.abtdump_ok = ABTDUMP_SW != 0;
  ns.tchk_ok = RD (SETCPSR_TCHECK - 4) == 0xe3110e22u		/* tst r1, #&220 */
	       && RD (SETCPSR_TCHECK) == 0x17000000u
	       && RD (SETCPSR_TCHECK + 8) == 0xe912900fu;	/* ldmdb r2, {.., pc} */
  if (!ns.psr_ok)
    return ERR (bad_rom);
  ns.midr = read_midr ();
  read_pi_model ();
  {
    char val[16] = "";
    regs v = {{ (unsigned) "NativeSupport$Debug", (unsigned) val, sizeof val - 1, 0, 0 }};
    if (!swix_regs (OS_ReadVarVal, &v) && v.r[2] < sizeof val)
      {
	val[v.r[2]] = 0;
	debug_faults = 1;
	debug = !(val[0] == 'f' && val[1] == 'a');
	log_modules ();
      }
  }
  {
    regs v = {{ (unsigned) "NativeSupport$NoStubs", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    nostubs = v.r[2] != 0;
  }
  /* the SVC stack's megabyte, for the mode checks: we're in SVC mode on
     it now (a module starting) */
  {
    unsigned here;
    if ((vmode_now () & 0x1f) == 0x13)
      ns_svc_base = (unsigned) &here & ~0xfffffu;
  }
  {
    regs w = {{ (unsigned) "NativeSupport$NoSyncPatch", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &w);
    nosyncpatch = w.r[2] != 0;
  }
  {
    regs v = {{ (unsigned) "NativeSupport$TrapTest", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    traptest = v.r[2] != 0;
  }

  ns.sctlr = 0x00c5187du;
  /* everything that can fail before anything is changed */
  {
    regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
    struct ksigaction o;
    if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2]
	|| RD (h.r[2] + 76) != ns.sys_entry)
      return ERR (bad_rom);
    static const int sigs[3] = { SIGILL_, SIGSEGV_, SIGBUS_ };
    for (int i = 0; i < 3; i++)
      if (lsys (SYS_rt_sigaction, sigs[i], 0, (unsigned) &o, 8, 0, 0) < 0
	  || o.restorer + 4 != ns.sys_entry)
	return ERR (bad_rom);
  }
  sites = rma_claim (SITES_MAX * sizeof *sites);
  reset_cs = mono_cs ();
  site_hits = rma_claim (SITES_MAX * 8);
  if (site_hits)
    for (unsigned i = 0; i < SITES_MAX * 2; i++)
      site_hits[i] = 0;
  stubs = rma_claim (STUBS_SIZE);
  if (!sites)
    {
      rma_free (stubs);
      stubs = 0;
      return ERR (no_memory);
    }
  /* the low stub page (none if &2000 can't be had), written only through
     mprotect */
  if (map_fixed (LOWSTUBS, LOWSTUBS_SIZE, 5, 0x32, -1, 0) != LOWSTUBS)
    nlow = ~0u;
  /* the handlers first: the code synchronisations after patching go
     through a patched instruction */
  int ok = set_handler (SIGILL_, ill_handler, &ns.old_ill);
  ok = ok && set_handler (SIGSEGV_, fault_handler, &ns.old_segv);
  ok = ok && set_handler (SIGBUS_, fault_handler, &ns.old_bus);
  dbg ("fault-sigflags", ns.old_ill.flags, ns.old_segv.flags, ns.old_bus.flags);
  if (!ok || !claim_swi_vector ())
    {
      /* (nothing patched yet: put the handlers back) */
      if (ns.old_ill.restorer)
	lsys (SYS_rt_sigaction, SIGILL_, (unsigned) &ns.old_ill, 0, 8, 0, 0);
      if (ns.old_segv.restorer)
	lsys (SYS_rt_sigaction, SIGSEGV_, (unsigned) &ns.old_segv, 0, 8, 0, 0);
      if (ns.old_bus.restorer)
	lsys (SYS_rt_sigaction, SIGBUS_, (unsigned) &ns.old_bus, 0, 8, 0, 0);
      if (nlow != ~0u)
	lsys3 (SYS_munmap, LOWSTUBS, LOWSTUBS_SIZE, 0);
      rma_free (sites);
      rma_free (stubs);
      sites = 0;
      stubs = 0;
      return ERR (bad_rom);
    }
  find_kpmp_stubs ();
  patch_kpmp_stubs ();
  patch_getdents ();
  {
    regs d = {{ 2, 1 }};		/* OS_DynamicArea 2: the RMA */
    d.r[0] = 2;
    d.r[1] = 1;
    if (!swix_regs (OS_DynamicArea, &d))
      {
	rma_lo = d.r[3];
	rma_hi = d.r[3] + d.r[5];
      }
  }
  /* (this module isn't patched: its own code) */
  unsigned self = (unsigned) module_start;
  self_lo = self;
  self_hi = self + RD (self - 4);
  (void) pw;
  patch_modules (self);
  patch_cacheflush (0x20800000u, 0x20804000u);	/* the HAL */
  if (ns.lur_ok && !lur_stub ()
      && rom_writable (LOADUSRREGISTERS, LOADUSRREGISTERS + 4, 1))
    {
      site_add ((unsigned *) LOADUSRREGISTERS, KIND_LUR);
      rom_writable (LOADUSRREGISTERS, LOADUSRREGISTERS + 4, 0);
      sync_code (LOADUSRREGISTERS, LOADUSRREGISTERS + 4);
    }
  /* (dynamic area placement: experimental, only with NativeSupport$DAPlace
     set when this module starts) */
  regs dp = {{ (unsigned) "NativeSupport$DAPlace", 0, -1, 0, 0 }};
  swix_regs (OS_ReadVarVal, &dp);
  if (dp.r[2] && ns.daaddr_ok && rom_writable (DAADDR_BL, DAADDR_BL + 4, 1))
    {
      site_add ((unsigned *) DAADDR_BL, KIND_DAADDR);
      rom_writable (DAADDR_BL, DAADDR_BL + 4, 0);
      sync_code (DAADDR_BL, DAADDR_BL + 4);
    }
  if (ns.abtdump_ok && rom_writable (ABTDUMP_SW, ABTDUMP_SW + 4, 1))
    {
      site_add ((unsigned *) ABTDUMP_SW, KIND_ABTDUMP);
      rom_writable (ABTDUMP_SW, ABTDUMP_SW + 4, 0);
      sync_code (ABTDUMP_SW, ABTDUMP_SW + 4);
    }
  patch_cacheflush (ROMOS_LO, ROMOS_HI);
  patch_imb_full ();
  ns.active = 1;
  /* (NativeSupport$NoFastSWI set when it starts: SWIs the usual way) */
  {
    regs v = {{ (unsigned) "NativeSupport$NoFastSWI", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    if (!v.r[2])
      fastswi_set (1);
  }
  /* (NativeSupport$NoFastFPA: FPA instructions through SIGILL) */
  {
    regs v = {{ (unsigned) "NativeSupport$NoFastFPA", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    if (!v.r[2])
      fastfpa_set (1);
  }
  return NULL;
}

/* Never while active (not even "fatal", as *RMLoad of another copy kills
   the old one): Linux's signal handlers and the SWI vector point here, and
   the code it patched relies on it. (RISC OS for Linux ends by leaving the
   process, without finalising modules.) */
oserror *
mod_final (int fatal)
{
  (void) fatal;
  if (ns.active)
    return ERR (no_kill);
  return NULL;
}

/* GCCSDK ARMEABISupport 1.05 (03 Jan 2023), as loaded: its mmap's new
   allocator sized to the request when that's over 100MB (aeabi_size,
   aeabi_after in header.s). Octane's zlib in Iris 1.0.29 (which needs
   1.05) maps 129MB at once. */
extern char aeabi_size[], aeabi_after[];
#define AEABI105_SIZE	0x444cu		/* mov r1, #&6400000 */
#define AEABI105_AFTER	0x4454u		/* cmp r0, #0 (after the BL) */
static int
aeabi105_fix (unsigned base, unsigned len)
{
  static const char help[] = "ARMEABISupport\t1.05 (03 Jan 2023)";
  if (len < AEABI105_AFTER + 4)
    return 0;
  unsigned ho = RD (base + 0x14);
  if (!ho || ho >= len - sizeof help)
    return 0;
  for (unsigned i = 0; i < sizeof help - 1; i++)
    if (((const char *) base)[ho + i] != help[i])
      return 0;
  unsigned *s = (unsigned *) (base + AEABI105_SIZE);
  unsigned *a = (unsigned *) (base + AEABI105_AFTER);
  if (*s != 0xe3a01519u || *a != 0xe3500000u || (s[1] >> 24) != 0xebu)
    return 0;
  int ds = ((int) aeabi_size - (int) s - 8) >> 2;
  int da = ((int) aeabi_after - (int) a - 8) >> 2;
  if (ds < -0x800000 || ds >= 0x800000 || da < -0x800000 || da >= 0x800000)
    return 0;
  *s = 0xeb000000u | ((unsigned) ds & 0xffffff);
  *a = 0xeb000000u | ((unsigned) da & 0xffffff);
  return 1;
}

void
mod_service (regs *r)
{
  /* Service_ModulePreInit: r0 = the module, r2 its length (also for a
     ROM module started again, *RMReInit: the ROM's were done at the
     start) */
  /* Service_DynamicAreaCreate/Remove: with fault reports on, where each
     dynamic area is and the address space it reserves (its maximum size):
     RISC OS for Linux has only what's free below 2GB for them */
  if ((r->r[1] == 0x90 || r->r[1] == 0x91) && debug_faults)
    {
      regs d = {{ 2, r->r[2] }};
      if (!swix_regs (0x66, &d))		/* OS_DynamicArea 2: read */
	{
	  const unsigned char *nm = (const unsigned char *) d.r[8];
	  unsigned w0 = 0, w1 = 0;
	  for (int i = 0; i < 8 && nm && nm[i] >= 32; i++)
	    {
	      if (i < 4)
		w0 |= (unsigned) nm[i] << (8 * i);
	      else
		w1 |= (unsigned) nm[i] << (8 * (i - 4));
	    }
	  dbg (r->r[1] == 0x90 ? "fault-da-create" : "fault-da-remove",
	       d.r[3], d.r[5], r->r[2]);
	  /* and RISC OS's free memory (the free pool, area 6) and the total
	     size of all areas, against the 1GB the port says it has */
	  regs fp = {{ 2, 6 }};
	  unsigned freep = swix_regs (0x66, &fp) ? ~0u : fp.r[2], used = 0;
	  regs e = {{ 3, ~0u }};
	  for (int k = 0; k < 512 && !swix_regs (0x66, &e) && e.r[1] != ~0u; k++)
	    {
	      regs q = {{ 2, e.r[1] }};
	      if (!swix_regs (0x66, &q))
		used += q.r[2];
	      e.r[0] = 3;
	    }
	  dbg ("fault-da-name", w0, w1, freep);
	  dbg ("fault-da-mem", freep, used, 0);
	}
      return;
    }
  if (r->r[1] == 0xB9 && ns.active && (r->r[0] < ROM_LO || r->r[0] >= ROM_HI))
    {
      unsigned v0 = veneer_psr;
      ns.patched += patch_range (r->r[0], r->r[0] + r->r[2] - 4);
      if (aeabi105_fix (r->r[0], r->r[2]))
	{
	  ns.aeabi105++;
	  if (debug_faults)
	    dbg ("fault-aeabi105", r->r[0], 0, 0);
	}
      if (debug_faults)
	log_module (r->r[0], r->r[2], veneer_psr - v0);
    }
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number; (void) r;
  return NULL;
}

static void
line_out (const char *label, unsigned v)
{
  write0 (label);
  write_dec (v);
  newline ();
}

/* " (Title)" for a current active object that is a module (BASIC) */
static const char *
where_cao (unsigned cao)
{
  static char b[44];
  regs r = {{ 12, 0, 0 }};
  if (cao < 0x20000000u)
    return "";
  for (int n = 0; n < 400 && !swix_regs (0x1E, &r); n++)	/* OS_Module 12 */
    {
      unsigned base = r.r[3], len = *(unsigned *) (base - 4);
      if (cao >= base && cao - base < len && len < 0x400000u)
	{
	  const char *t = (const char *) base + *(unsigned *) (base + 16);
	  int i = 0;
	  b[i++] = ' ', b[i++] = '(';
	  while (i < 40 && *t > ' ')
	    b[i++] = *t++;
	  b[i++] = ')', b[i] = 0;
	  return b;
	}
      r.r[0] = 12;
    }
  return "";
}

/* " (Module+offset)" for an address in a module's code */
static void
where_out (unsigned pc)
{
  regs r = {{ 12, 0, 0 }};
  for (int n = 0; n < 400 && !swix_regs (0x1E, &r); n++)	/* OS_Module 12 */
    {
      unsigned base = r.r[3], len = *(unsigned *) (base - 4);
      if (pc >= base && pc - base < len && len < 0x400000u)
	{
	  const char *t = (const char *) base + *(unsigned *) (base + 16);
	  char b[40];
	  int i = 0;
	  while (i < 31 && t[i] > ' ')
	    b[i] = t[i], i++;
	  b[i] = 0;
	  write0 (" (");
	  write0 (b);
	  write0 ("+&");
	  write_hex8 (pc - base);
	  write0 (")");
	  return;
	}
      r.r[0] = 12;
    }
}

/* the six commonest of a trap table, most first (not cleared) */
static void
list_traps (const char *label, struct other_trap *t)
{
  int done[6], nd = 0;
  for (int r = 0; r < 6; r++)
    {
      int bi = -1;
      unsigned best = 0;
      for (int k = 0; k < OTHER_N; k++)
	{
	  int seen = 0;
	  for (int j = 0; j < nd; j++)
	    seen |= done[j] == k;
	  if (!seen && t[k].n > best)
	    best = t[k].n, bi = k;
	}
      if (bi < 0)
	break;
      done[nd++] = bi;
      write0 (label);
      write_hex8 (t[bi].pc);
      write0 (" ");
      write_hex8 (t[bi].insn);
      write0 (" n ");
      write_dec (best);
      where_out (t[bi].pc);
      newline ();
    }
}

static unsigned
mono_cs (void)
{
  regs r = {{ 0 }};
  swix_regs (0x42, &r);		/* OS_ReadMonotonicTime */
  return r.r[0];
}

/* *NativeSupport Reset: start the counts again */
static void
counts_reset (void)
{
  for (unsigned i = 0; site_hits && i < 2 * nsites; i++)
    site_hits[i] = 0;
  for (int k = 0; k < OTHER_N; k++)
    other_traps[k].n = skip_traps[k].n = 0, other_traps[k].pc = skip_traps[k].pc = 0;
  ns.cond_skipped = ns.emul_psr = ns.emul_priv = ns.getdents = ns.mem0 = ns.mem24 = 0;
  ns.syncs = ns.full_syncs = ns.aborts = ns.ioc = ns.cp15_barriers = 0;
  ns.patched_sync = ns.imb_full = ns.lur_thumb = ns.thumb_returns = 0;
  ns.it_returns = 0;
  reset_cs = mono_cs ();
}

/* the busiest sites and other traps, most first */
static void
hot_out (void)
{
  unsigned done[10];
  int nd = 0;
  if (site_hits)
    for (int r = 0; r < 10; r++)
      {
	unsigned best = 0, bi = ~0u;
	for (unsigned i = 0; i < nsites; i++)
	  {
	    int seen = 0;
	    for (int j = 0; j < nd; j++)
	      seen |= done[j] == i;
	    if (!seen && site_hits[2 * i] > best)
	      best = site_hits[2 * i], bi = i;
	  }
	if (bi == ~0u)
	  break;
	done[nd++] = bi;
	write0 ("Busiest site: ");
	write_hex8 (site_hits[2 * bi + 1]);
	write0 (" ");
	write_hex8 (sites[bi].orig);
	write0 (" kind ");
	write_dec (sites[bi].kind);
	write0 (" hits ");
	write_dec (best);
	where_out (site_hits[2 * bi + 1]);
	newline ();
      }
  list_traps ("Other trap: ", other_traps);
  list_traps ("Skipped trap (condition failed): ", skip_traps);
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index;
  if (!ns.active)
    {
      write0 ("NativeSupport: not needed (RISC OS isn't running natively)");
      newline ();
      return NULL;
    }
  /* *NativeSupport Reset */
  if (argc)
    {
      while (*tail == ' ')
	tail++;
      const char *w = "reset";
      int i = 0;
      while (w[i] && (tail[i] | 0x20) == w[i])
	i++;
      if (!w[i] && (unsigned char) tail[i] <= ' ')
	{
	  counts_reset ();
	  write0 ("NativeSupport: counts started again");
	  newline ();
	  return NULL;
	}
    }
  /* *NativeSupport FastSWI On|Off, *NativeSupport FastFPA On|Off */
  if (argc)
    {
      /* characters, not pointers: a table of pointers isn't relocated in
	 this module (with three words GCC made one, and nothing matched) */
      static const char words[3][10] = { "fastswi", "fastfpa", "syncpatch" };
      for (int k = 0; k < 3; k++)
	{
	  const char *w = words[k];
	  int i = 0;
	  while (w[i] && (tail[i] | 0x20) == w[i])
	    i++;
	  if (w[i] || (unsigned char) tail[i] > ' ')
	    continue;
	  const char *o = tail + i;
	  while (*o == ' ')
	    o++;
	  int on = (o[0] | 0x20) == 'o' && (o[1] | 0x20) == 'n';
	  int off = (o[0] | 0x20) == 'o' && (o[1] | 0x20) == 'f';
	  if (k == 0)
	    {
	      if (on || off)
		fastswi_set (on);
	      write0 ("Fast SWI entry: ");
	      write0 (ns.fastswi > 0 ? "on" : ns.fastswi ? "not in this Linux kernel" : "off");
	    }
	  else if (k == 2)
	    {
	      if (on || off)
		nosyncpatch = off;
	      write0 ("Patching new code: ");
	      write0 (nosyncpatch ? "off" : "on");
	    }
	  else
	    {
	      if (on || off)
		fastfpa_set (on);
	      write0 ("Fast FPA entry: ");
	      write0 (ns.fastfpa > 0 ? "on" : ns.fastfpa ? "not in this Linux kernel or ROM" : "off");
	    }
	  newline ();
	  return NULL;
	}
    }
  write0 ("RISC OS for Linux, native. Processor ");
  write_hex8 (ns.midr);
  newline ();
  line_out ("Seconds counted (since start-up or *NativeSupport Reset): ", (mono_cs () - reset_cs) / 100);
  write0 ("Fast SWI entry (*NativeSupport FastSWI On|Off): ");
  write0 (ns.fastswi > 0 ? "on" : ns.fastswi ? "not in this Linux kernel" : "off");
  newline ();
  write0 ("Fast FPA entry (*NativeSupport FastFPA On|Off): ");
  write0 (ns.fastfpa > 0 ? "on" : ns.fastfpa ? "not in this Linux kernel or ROM" : "off");
  newline ();
  {
    unsigned fl, gl, fa;
    maps_report (&fl, &gl, &fa, debug_faults);
    line_out ("Free address space below 2GB (MB): ", fl >> 20);
    line_out ("Largest free block below 2GB (MB): ", gl >> 20);
    line_out ("Free address space in all (MB): ", fa >> 20);
  }
  if (pi_model[0])
    {
      write0 ("Hardware platform (from Linux): ");
      write0 (pi_model);
      newline ();
    }
  line_out ("Dynamic areas placed below 2GB: ", ns.da_low);
  line_out ("Dynamic areas above 2GB (no room below, or NativeSupport$High): ", ns.da_high);
  line_out ("Instructions patched in modules: ", ns.patched);
  line_out ("Instructions patched in the ROM: ", ns.patched_rom);
  line_out ("Instructions patched in new code: ", ns.patched_sync);
  line_out ("Code synchronisations: ", ns.syncs);
  line_out ("Whole-cache synchronisations (RMA scanned): ", ns.full_syncs);
  line_out ("MRS done without a signal (sites): ", ns.stubbed);
  line_out ("PSR instructions done: ", ns.emul_psr);
  line_out ("Other privileged instructions done: ", ns.emul_priv);
  line_out ("Directory reads: ", ns.getdents);
  line_out ("OS_Memory 0 calls: ", ns.mem0);
  line_out ("Access checks answered for PMP areas (OS_Memory 24, OS_ValidateAddress): ", ns.mem24);
  line_out ("PMP (lite) SWIs: ", ns.pmp_swis);
  line_out ("PMP operations: ", ns.kpmp_ops);
  line_out ("Reporter SWIs ignored: ", ns.reports);
  line_out ("Synchronisations in mmap areas (JIT) left: ", ns.sync_jit);
  line_out ("Synchronisations in application space of files loaded as code (FileSwitch), left: ", ns.sync_app_data);
  line_out ("Synchronisations in application space a program asked for, outside its image (its data), left: ", ns.sync_app_prog);
  line_out ("Synchronisations in application space of code written there (BASIC, the C library), patched: ", ns.sync_app_code);
  line_out ("Synchronisations of a part of the program, patched: ", ns.sync_own);
  line_out ("Patched instructions copied as data, put back as written to a file: ", ns.unpatched_words);
  line_out ("Writes to files that had them: ", ns.unpatched_writes);
  if (nsyncwho)
    {
      write0 ("Who synchronised application space:");
      newline ();
      for (unsigned i = 0; i < nsyncwho; i++)
	{
	  struct syncwho *w = &syncwho[i];
	  write0 (w->what == 'f' ? "  left (FileSwitch) " : w->what == 'd' ? "  left (data)       " : w->what == 'o' ? "  own               " : w->what == 's' ? "  program start     " : "  patched           ");
	  write_dec (w->n);
	  write0 (" x, from &");
	  write_hex8 (w->caller);
	  if (!w->caller)
	    write0 (" (not through the SWI vector)");
	  else if (w->caller < 0x20000000u)
	    write0 (" (application space)");
	  else
	    where_out (w->caller);
	  write0 (", CAO &");
	  write_hex8 (w->cao);
	  write0 (where_cao (w->cao));
	  write0 (", last &");
	  write_hex8 (w->lo);
	  write0 ("-&");
	  write_hex8 (w->hi);
	  if (w->sites)
	    {
	      write0 (", made ");
	      write_dec (w->sites);
	      write0 (" sites, first &");
	      write_hex8 (w->s_addr);
	      write0 (" &");
	      write_hex8 (w->s_orig);
	    }
	  newline ();
	}
      if (syncwho_lost)
	line_out ("  (other callers not kept): ", syncwho_lost);
    }
  line_out ("Programs over 64 MB patched: ", ns.sync_big);
  line_out ("Synchronisations over 64 MB, not a program, left: ", ns.sync_too_big);
  write0 ("Patching new code (*NativeSupport SyncPatch On|Off): ");
  write0 (nosyncpatch ? "off" : "on");
  newline ();
  line_out ("ARMEABISupport 1.05 mmaps over 100MB allowed: ", ns.aeabi105);
  line_out ("Aborts passed to handlers: ", ns.aborts);
  line_out ("IOC timer reads: ", ns.ioc);
  line_out ("Faults in emulations (failed): ", ns.probe_faults);
  line_out ("Sites or stubs not made (tables full): ", ns.full);
  line_out ("Trapped instructions whose condition failed (skipped): ", ns.cond_skipped);
  line_out ("CP15 barriers emulated: ", ns.cp15_barriers);
  line_out ("Abort register dumps switched back without a stack: ", ns.abtdump_sw);
  line_out ("CP15 barriers replaced by ARMv7 ones: ", ns.cp15_rewritten);
  line_out ("Conditional mode instructions patched in veneers: ", veneer_psr);
  line_out ("User-register LDM/STM ^ patched: ", userbank_sites);
  write0 ("Returns to user mode (LoadUsrRegisters): ");
  write0 (ns.lur_stubbed ? "without a signal (stub)" : "trapped");
  newline ();
  line_out ("Returns to Thumb code through LoadUsrRegisters (trapped): ", ns.lur_thumb);
  line_out ("Returns to Thumb code through the mode switch: ", ns.thumb_returns);
  line_out ("Returns into Thumb-2 IT blocks (through the signal frame): ", ns.it_returns);
  line_out ("Squeezed programs not patched until unsqueezed: ", ns.squeezed_skipped);
  line_out ("Whole-cache synchronisation seen: ", ns.imb_full);
  write0 ("SVC stack megabyte: &");
  write_hex8 (ns_svc_base);
  newline ();
  line_out ("SWIs in SVC mode not on the SVC stack: ", bad_swi.n);
  if (bad_swi.n)
    {
      write0 ("  first: SWI &");
      write_hex8 (bad_swi.swi);
      write0 (" from &");
      write_hex8 (bad_swi.ret - 4);
      write0 (", SPSR &");
      write_hex8 (bad_swi.spsr);
      write0 (", sp &");
      write_hex8 (bad_swi.sp);
      newline ();
    }
  line_out ("Traps in SVC mode not on the SVC stack: ", bad_trap.n);
  hot_out ();
  if (bad_trap.n)
    {
      write0 ("  first: signal ");
      write_dec (bad_trap.sig);
      write0 (" at &");
      write_hex8 (bad_trap.pc);
      write0 (" (&");
      write_hex8 (bad_trap.insn);
      write0 ("), sp &");
      write_hex8 (bad_trap.sp);
      write0 (", lr &");
      write_hex8 (bad_trap.lr);
      newline ();
    }
  return NULL;
}
