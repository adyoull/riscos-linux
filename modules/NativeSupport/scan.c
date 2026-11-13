/* NativeSupport: which words of a piece of code are privileged instructions to rewrite (and which are data), the MRS stubs, and the patching of a range.
   Part of native.c (included there; not compiled on its own). */

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
static unsigned mrs_flags_left;		/* _swix's flags-only MRS, not patched */

/* (0.47) A word that an instruction near it loads as a literal is data:
     ldr  rX, [pc, #n]      (the word itself)
     ldf/stf/ldc [pc, #n]   (a double's or extended's first, second or
			     third word)
   A compiler's literal pool sits in the code, between AL instructions,
   which the rules below take for the sign of an instruction. ChartDraw and
   MultiPlot have 256000.0 there (draw units): its first word, &410F4000,
   is MRSMI r4, CPSR. Patched to a UDF, the number loaded was something
   else, and each stopped as it started with "Floating point exception:
   invalid operation". Both ways are looked at, as far as such a load can
   reach (4K; coprocessor loads 1K). */
static unsigned literals_left;
static int
literal_word (const unsigned *p, const unsigned *lo, const unsigned *hi)
{
  const unsigned *a = p - 1026 > lo && p - 1026 < p ? p - 1026 : lo;
  const unsigned *b = p + 1026 < hi && p + 1026 > p ? p + 1026 : hi;
  for (const unsigned *q = a; q < b; q++)
    {
      unsigned x = *q;
      if ((x >> 28) == 15 || q == p)
	continue;
      if ((x & 0x0f7f0000u) == 0x051f0000u)		/* LDR rX, [pc, #+-n] */
	{
	  unsigned off = x & 0xfff;
	  const unsigned *t = (const unsigned *) ((unsigned) (q + 2) + (x & 0x800000u ? off : 0u - off));
	  if (t == p && !(off & 3))
	    return 1;
	}
      else if ((x & 0x0f1f0000u) == 0x0d1f0000u)	/* LDC [pc, #+-n] (LDF) */
	{
	  unsigned off = (x & 0xff) * 4;
	  const unsigned *t = (const unsigned *) ((unsigned) (q + 2) + (x & 0x800000u ? off : 0u - off));
	  if (p >= t && p <= t + 2)
	    return 1;
	}
    }
  return 0;
}

static int psr_insn_1 (const unsigned *p, const unsigned *lo, const unsigned *hi);
static int
psr_insn (const unsigned *p, const unsigned *lo, const unsigned *hi)
{
  if (!psr_insn_1 (p, lo, hi))
    return 0;
  if (literal_word (p, lo, hi))
    {
      literals_left++;
      if (debug_faults)
	dbg ("fault-literal-left", (unsigned) p, *p, p - lo);
      return 0;
    }
  return 1;
}

static int
psr_insn_1 (const unsigned *p, const unsigned *lo, const unsigned *hi)
{
  unsigned w = *p, cond = w >> 28;
  if ((w & 0xfff1fe20u) == 0xf1000000u)			/* CPS */
    {
      /* (next to an ordinary AL instruction, as real code is: data
	 matches CPS easily) */
      if (!((w & 0x000e0000u) != 0 && (!(w & 0x20000u) || valid_mode (w))))
	return 0;
      /* (0.48) and a CPS the processor would take: interrupts enabled or
	 disabled name at least one of A, I, F; a mode change alone names
	 none; without a mode change the mode bits are zero. OvationPro's
	 image has &F10C0098 and &F10C0099 in a table (CPSID with mode
	 bits and no mode change): patched while it was being unsqueezed,
	 they spoiled the compressed data and it stopped as it started,
	 "undefined instruction at &8070". */
      {
	unsigned imod = (w >> 18) & 3, m = (w >> 17) & 1, aif = (w >> 6) & 7;
	if (imod == 1 || (imod >= 2 ? aif == 0 : (!m || aif != 0))
	    || (!m && (w & 0x1f)))
	  return 0;
      }
      for (int d = -2; d <= 2; d++)
	if (d && p + d >= lo && p + d < hi && (p[d] >> 28) == 14)
	  return 1;
      return 0;
    }
  if (cond == 15)
    return 0;
  /* The C libraries' _swix (UnixLib's, after its SWI): the flags the SWI
     returned, read the way that works on 26- and 32-bit processors:
       mov ip, #0; mrs ip, CPSR; teq ip, #0; ...; andne ip, ip, #&F0000000
     Only the flags (NZCV) are kept, and those are the processor's own in
     any mode, so the real MRS is right: left alone, it doesn't trap. (A
     UDF there trapped at every _swix: 7.6 million times in one ROBench
     run on a Pi 4, a signal each.) */
  if ((w & 0xffff0fffu) == 0xe10f0000u && p > lo && p + 1 < hi)
    {
      unsigned rd = (w >> 12) & 15;
      if (rd != 15 && p[-1] == (0xe3a00000u | (rd << 12))
	  && p[1] == (0xe3300000u | (rd << 16)))
	for (int d = 2; d <= 7 && p + d < hi; d++)
	  if ((p[d] & 0x0fffffffu) == (0x0200020fu | (rd << 16) | (rd << 12)))
	    {
	      mrs_flags_left++;
	      return 0;
	    }
    }
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

/* A skipped trap that would be skipped every time: the 26/32-bit test
   "TEQ pc, pc; LDMNE sp!, {..., pc}^" in modules' SWI and service exits
   (IXSupport's, BufferManager's, the Toolbox's: 5 million traps in a
   35-minute session on a Pi 4, about 5 us each). On a 32-bit processor
   TEQ pc, pc always sets Z, so the LDMNE never runs: it becomes a NOP and
   traps no more. Only in the ROM and in modules that say they're 32-bit
   (not 26-bit ones, which Aemulor runs as on a 26-bit processor, nor code
   made at run time, nor application space, whose code a program can copy
   out as data and write to a file). */
#define ROMOS_LO 0x20810000u
#define ROMOS_HI 0x20C00000u
static unsigned rma_lo, rma_hi;		/* the RMA (its largest size) */
#define NOPS_MAX 1024
static unsigned nop_addr[NOPS_MAX], nop_orig[NOPS_MAX], nnops;
static int nocondnop;			/* NativeSupport$NoCondNOP */
static int priv_form (unsigned w);
/* a privileged instruction with condition NE straight after TEQ pc, pc,
   outside application space: never run */
/* an NE instruction that leaves the flags and pc alone (so the one after
   it still follows the TEQ pc, pc): LDMNE/LDRNE not of pc, a data
   operation without S not to pc, STMNE/STRNE */
static int
ne_passes (unsigned x)
{
  if ((x >> 28) != 1)
    return 0;
  unsigned op = (x >> 25) & 7;
  if (op == 4)					/* LDM/STM */
    return !((x >> 20) & 1) || !(x & (1u << 15));
  if (op == 2 || op == 3)			/* LDR/STR */
    return (x & (1u << 4)) && op == 3 ? 0 : !((x >> 20) & 1) || ((x >> 12) & 15) != 15;
  if (op == 0 || op == 1)			/* data processing */
    return !((x >> 20) & 1) && ((x >> 12) & 15) != 15
	   && (op == 1 || (x & 0x90u) != 0x90u);
  return 0;
}

static int
cond_never (const unsigned *p)
{
  if (nocondnop || (unsigned) p < 0x20000000u || (p[0] >> 28) != 1
      || !priv_form (p[0]))
    return 0;
  /* straight after TEQ pc, pc, or after one or two other NE
     instructions that follow it:
       teq pc, pc; ldmnefd sp!, {r1-r9, lr}; orrnes pc, lr, #V_bit */
  for (int d = 1; d <= 3; d++)
    {
      if (p[-d] == 0xe13f000fu)
	return 1;
      if (!ne_passes (p[-d]))
	return 0;
    }
  return 0;
}

/* found as code is patched (the ROM's and modules' ranges, made writable
   by the caller): made NOPs before they ever trap */
static unsigned
cond_nop_range (unsigned lo, unsigned hi)
{
  unsigned n = 0;
  for (unsigned *p = (unsigned *) (lo & ~3u) + 1; p < (unsigned *) (hi & ~3u); p++)
    if (cond_never (p))
      {
	/* (outside the ROM: kept, so that a module's code saved to a
	   file gets the instruction back - write_fixed) */
	if ((unsigned) p - ROM_LO >= ROM_HI - ROM_LO)
	  {
	    unsigned k;
	    for (k = 0; k < nnops && nop_addr[k] != (unsigned) p; k++)
	      ;
	    if (k == nnops && nnops < NOPS_MAX)
	      nnops++;
	    if (k < NOPS_MAX)
	      nop_addr[k] = (unsigned) p, nop_orig[k] = *p;
	  }
	*p = 0xe1a00000u;				/* MOV r0, r0 */
	n++;
      }
  ns.cond_nopped += n;
  return n;
}

/* A module that says it's 32-bit (its flags word, bit 0). Only those are
   changed: a 26-bit module (Aemulor runs them, emulating a 26-bit
   processor) takes the LDMNE ^ after TEQ pc, pc, as on a 26-bit machine,
   and other code made at run time (Aemulor's, a JIT's) is left alone. */
static int
module_32bit (unsigned base, unsigned len)
{
  if (len <= 0x34)
    return 0;
  unsigned off = *(volatile unsigned *) (base + 0x30);
  return off >= 0x34 && off < len && !(off & 3)
	 && (*(volatile unsigned *) (base + off) & 1);
}

/* one in the ROM that trapped all the same */
static void
cond_nop (unsigned pc, unsigned w)
{
  (void) w;
  if (!cond_never ((const unsigned *) pc) || pc - ROMOS_LO >= ROMOS_HI - ROMOS_LO
      || !rom_writable (pc, pc + 4, 1))
    return;
  *(volatile unsigned *) pc = 0xe1a00000u;		/* MOV r0, r0 */
  rom_writable (pc, pc + 4, 0);
  lsys3 (0xf0002, pc, pc + 4, 0);			/* cacheflush */
  ns.cond_nopped++;
}

static unsigned
patch_range (unsigned lo, unsigned hi)
{
  unsigned n = 0;
  unsigned *a = (unsigned *) (lo & ~3u), *b = (unsigned *) (hi & ~3u);
  for (unsigned *p = a; p < b; p++)
    if (userbank_insn (p, a, b) && !literal_word (p, a, b))
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

