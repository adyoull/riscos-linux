/* NativeSupport: faults: the IOC timer, the lite PMP's abort handlers, DFAR and DFSR.
   Part of native.c (included there; not compiled on its own). */
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
      pfault_addr = addr;
      pfault_code = code;
      pfault_sig = sig;
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

