/* NativeSupport: mode checks and the other privileged instructions, emulated when they trap.
   Part of native.c (included there; not compiled on its own). */
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

/* (0.45) A site whose instruction couldn't be done (emulate_psr and
   emulate_priv both failed: MRS/MSR SPSR in user mode, a fault) goes to
   RISC OS as an undefined instruction. With the UDF there, an undefined
   instruction handler, an error report or a debugger saw &E7Fxxxxx, not
   the instruction (!ArmDebug32: &E7F11AF1 for MRS R14, SPSR). So the
   original word is put back first, and stays for SUSPEND_CS; the first
   SWI after that makes it the site again (ns_fixups_run, from the SWI
   vector), if the word is still there, so that the code works when it
   next runs in a privileged mode. */
#define SUSPEND_MAX 8
#define SUSPEND_CS 1000u
static struct { unsigned idx, when; } suspended[SUSPEND_MAX];
static unsigned nsuspended, sites_suspended;
extern unsigned ns_fixups;

/* (Linux's clock, not a SWI: this is called in a signal handler and on
   the SWI vector) */
static unsigned
linux_cs (void)
{
  unsigned ts[2] = { 0, 0 };
  lsys3 (SYS_clock_gettime, 1, (unsigned) ts, 0);
  return ts[0] * 100 + ts[1] / 10000000;
}

static int
p_poke (void *a)
{
  struct pargs *p = a;
  volatile unsigned *q = (volatile unsigned *) p->lo;
  int rom = p->lo - ROMOS_LO < ROMOS_HI - ROMOS_LO;
  if (*q != p->hi)
    return 0;
  if (rom && !rom_writable (p->lo, p->lo + 4, 1))
    return 0;
  *q = p->insn;
  if (rom)
    rom_writable (p->lo, p->lo + 4, 0);
  lsys3 (0xf0002, p->lo, p->lo + 4, 0);		/* cacheflush */
  return 1;
}

static void
site_suspend (struct site *s)
{
  unsigned idx = s - sites;
  struct pargs a = { 0, s->orig, s->addr, udf (UDF_BASE + idx) };
  if (nsuspended == SUSPEND_MAX || !probe_call (p_poke, &a))
    return;
  suspended[nsuspended].idx = idx;
  suspended[nsuspended].when = linux_cs ();
  nsuspended++;
  sites_suspended++;
  ns_fixups = 1;
}

void ns_fixups_run (void);
void
ns_fixups_run (void)
{
  /* (0.46) the clock is a Linux call, and this runs on every SWI while a
     site is put back (10 s): look at it on one SWI in 256 */
  static unsigned calls;
  if (++calls & 255)
    return;
  unsigned now = linux_cs (), k = 0;
  for (unsigned i = 0; i < nsuspended; i++)
    {
      struct site *s = &sites[suspended[i].idx];
      if (now - suspended[i].when < SUSPEND_CS)
	{
	  suspended[k++] = suspended[i];
	  continue;
	}
      struct pargs a = { 0, udf (UDF_BASE + suspended[i].idx), s->addr, s->orig };
      probe_call (p_poke, &a);		/* (not if it's something else now) */
    }
  nsuspended = k;
  ns_fixups = k != 0;
}

static void ill_body (int sig, void *si, void *uc);
static void fault_body (int sig, void *si, void *uc);

/* (0.46) An emulated LDM/STM ^ or SWP that stops on a memory fault is a
   data abort, as the instruction's own fault would be, not an undefined
   instruction: the fault the emulation took (kept by fault_body) goes to
   the port's handler with the instruction's own registers. */
static unsigned pfault_addr;
static int pfault_code, pfault_sig;

static int
abort_instead (unsigned w, unsigned faults_before, void *si, void *uc)
{
  if (ns.probe_faults == faults_before || !pfault_sig
      || !((w & 0x0e400000u) == 0x08400000u || (w & 0x0fb00ff0u) == 0x01000090u))
    return 0;
  unsigned si2[32];
  memcpy (si2, si, sizeof si2);
  si2[0] = pfault_sig;
  si2[1] = 0;
  si2[2] = pfault_code;
  si2[3] = pfault_addr;
  ns.emul_aborts++;
  chain (pfault_sig == SIGBUS_ ? &ns.old_bus : &ns.old_segv, pfault_sig, si2, uc);
  return 1;
}

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
	  cond_nop (pc, w);
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
	  unsigned pf = ns.probe_faults;
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
	  /* it can't be done here (a privileged instruction in user mode,
	     say): RISC OS gets it as an undefined instruction - the
	     instruction itself, not the site's UDF */
	  /* (0.46) not when the emulation stopped on a memory fault (an
	     LDM or STM ^ on a bad address): that instruction can be done,
	     and put back it would run unemulated the next time */
	  if (abort_instead (orig, pf, si, uc))
	    return;
	  if (s->kind == KIND_PSR && s->addr == pc && ns.probe_faults == pf)
	    site_suspend (s);
	}
      else if (pc_emulated (pc) && priv_form (w))
	{
	  if (!cond_pass (w, R[16]))
	    {
	      R[15] = pc + 4;
	      return;
	    }
	  unsigned pf = ns.probe_faults;
	  if ((w & 0x0fb00ff0u) == 0x01000090u ? probe (p_swp, R, w)
					       : probe (p_priv, R, w))
	    {
	      dbg ("priv", pc, w, R[13]);
	      ns.emul_priv++;
	      return;
	    }
	  if (abort_instead (w, pf, si, uc))
	    return;
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

