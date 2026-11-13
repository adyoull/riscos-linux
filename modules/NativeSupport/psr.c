/* NativeSupport: MRS/MSR CPSR and CPS, done through the port's SetCPSR routines; the SIGILL handler.
   Part of native.c (included there; not compiled on its own). */
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

#include "privform.c"

