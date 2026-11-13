/* NativeSupport: OS_SynchroniseCodeAreas: patching code as it is made, putting the original words back in what is written to files, and the SWI vector.
   Part of native.c (included there; not compiled on its own). */
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

/* SWIs without a trap (0.34; ns_swis_tmpl in header.s): the stub area, 16
   4KB pages. The first has the fast SWI entry's counting hook (at
   SWIS_FIRST). Then pairs: a data page, whose last 40 bytes are the
   template's working words, and a code page starting with its common code,
   then stubs of 16 bytes from SWIS_STUB0 (all within a pc-relative
   load's 4KB of the working words). Code and the data it writes are in
   different pages: QEMU's emulator throws a page's translated code away at
   every write to it, which made a SWI three times as slow there. */
#define SWIS_PAGE	0x1000u
#define SWIS_PAGES	20u
#define SWIS_HELD	15u		/* the hook's page and the pairs: where
					   the ROM holds interrupts back */
#define SWIS_FIRST	0x100u
#define SWIS_STUB0	0xc0u
#define SWIS_PER_PAGE	((0xfc0u - SWIS_STUB0) / 16)
#define SWIS_PAIRS	((SWIS_HELD - 1) / 2)
#define SWIS_MAX	(SWIS_PAIRS * SWIS_PER_PAGE)
/* (0.45) Each stub's SWI word and its branch back to the site (what the
   kernel reads, and where the SWI returns) are in the pages after those,
   8 bytes a stub, outside the held range: an interrupt arriving at the
   branch back is taken there like any other. In the held range it stayed
   pending, with Linux's signals blocked, until the program's next SWI. */
static struct
{
  int state;			/* 1: on; 0: off; -1: not in this ROM */
  unsigned blk;			/* the ROM's PVirt_SWIStubs block */
  unsigned area;		/* the stub area (4KB-aligned) */
  unsigned *table;		/* the hook's {site, count} */
  unsigned *queue;		/* put, taken, then 64 sites */
  unsigned *site, *word;	/* per stub: its site and the SWI there */
  unsigned n;			/* stubs made (never freed: a return
				   address can be in one) */
  unsigned patched, reused, range, excluded, full, unpatched;
} sw;

/* the code page of pair p (its data page is the one before) */
static unsigned
swis_code (unsigned p)
{
  return sw.area + (2 + 2 * p) * SWIS_PAGE;
}

static unsigned
swis_stub (unsigned i)
{
  return swis_code (i / SWIS_PER_PAGE) + SWIS_STUB0 + (i % SWIS_PER_PAGE) * 16;
}

/* stub i's SWI word (then its branch back) */
static unsigned
swis_exit (unsigned i)
{
  return sw.area + SWIS_HELD * SWIS_PAGE + i * 8;
}

/* A SWI made through a stub returns to the stub's last word (a branch to
   the site + 4): what code that looks at its caller's return address (the
   FileSwitch check below) wants is the site's own */
static unsigned
swis_ret (unsigned a)
{
  unsigned e = sw.area + SWIS_HELD * SWIS_PAGE;
  if (!sw.area || a - e >= SWIS_MAX * 8 || ((a - e) & 7) != 4)
    return a;
  unsigned i = (a - e) / 8;
  return i < sw.n ? sw.site[i] + 4 : a;
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

/* (0.45) NativeSupport$Leave: module titles, comma-separated, whose code
   is left unpatched. The patching can't tell an instruction from data
   that has the same bits: a debugger's, assembler's or emulator's tables
   of instructions can be changed by it. Such a module can be named here
   (set before it's loaded); its own privileged instructions are then done
   only when the processor traps them. */
static unsigned modules_left;
static int
leave_module (unsigned base, unsigned len)
{
  static char list[256];
  unsigned vcpu = get_tls ();
  unsigned saved = *(volatile unsigned *) (vcpu + 24);
  regs v = {{ (unsigned) "NativeSupport$Leave", (unsigned) list, sizeof list - 1, 0, 0 }};
  int have = !swix_regs (OS_ReadVarVal, &v) && v.r[2] && v.r[2] < sizeof list;
  *(volatile unsigned *) (vcpu + 24) = saved;
  if (!have || len < 0x34)
    return 0;
  list[v.r[2]] = 0;
  unsigned to = *(volatile unsigned *) (base + 16);
  if (!to || to >= len)
    return 0;
  const char *t = (const char *) (base + to);
  for (const char *p = list; *p;)
    {
      int n = 0;
      while (p[n] && p[n] != ',')
	n++;
      int k = 0;
      while (k < n && t[k] > ' ' && ((t[k] ^ p[k]) & ~0x20) == 0)
	k++;
      if (n && k == n && t[k] <= ' ')
	return 1;
      p += n;
      if (*p == ',')
	p++;
    }
  return 0;
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
  /* a module being loaded (the kernel synchronises its block), named in
     NativeSupport$Leave */
  if (lo >= 0x20000000u && hi - lo >= 0x38
      && (leave_module (lo, hi - lo) || leave_module (lo + 4, hi - lo - 4)))
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
      unsigned caller = swis_ret (ns_sync_caller), end = aif_end ();
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
  /* (0.48) Or one that UnSqueezeAIF (the ROM's module, for programs
     squeezed by old versions of squeeze) is having unsqueezed: it puts
     "ldr pc, [pc, #-4]" and its own address at &800C to get control back,
     so the header doesn't read as squeezed any more, and that squeeze's
     decompressor synchronises the whole of application space before it
     has finished. FileSwitch synchronises the program afterwards. */
  if (skipsqueezed && lo <= 0x8000u && hi > 0x8014u
      && *(volatile unsigned *) 0x800cu == 0xe51ff004u
      && *(volatile unsigned *) 0x8010u - ROM_LO < ROM_HI - ROM_LO)
    {
      ns.squeezed_skipped++;
      return;
    }
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
	unsigned caller = swis_ret (ns_sync_caller);
	ns_sync_caller = 0;
	syncwho_note ('s', caller, cao (), lo, hi);
      }
    }
  ns.patched_sync += patch_range (lo, hi);
  syncwho_cur = -1;
}

/* Patched words written to a file (0.45: rewritten). A program copies
   code out of its image as data (CMHG's veneer templates: in the DDE's
   ROM build every C module CMHG made had 4 of them), or saves code from
   where it is (BASIC's assembler, then *Save; a module's code from the
   RMA): when [lo, lo+len) is written to a file (OS_GBPB 1/2, OS_File
   0/10) and has words of ours, the write is made from a copy with the
   original instructions put back. Nothing in memory is changed (before
   0.45 the words were put back in place for the write, and a copy kept
   the original for good: code that ran meanwhile ran unpatched, and other
   data that matched was changed in the program's own buffer).
   Ours: a UDF at its site's own address; a copy of one (the site still
   patched) with the words either side the same as the site's - a piece
   of code, not data that happens to match; outside application space, a
   branch to the MRS stub made for that address, and a never-run
   instruction made a NOP there. Not the ROM (its SWI stubs and the like
   stay). */
static unsigned fix_nomem, fix_errors;

static int
p_rd (void *a)
{
  unsigned *q = a;
  q[1] = *(volatile unsigned *) q[0];
  return 1;
}

static int
safe_rd (unsigned addr, unsigned *v)
{
  unsigned q[2] = { addr, 0 };
  if (addr & 3 || !probe_call (p_rd, q))
    return 0;
  *v = q[1];
  return 1;
}

/* w at a, in [lo, hi) being written: 1 and its original if it's ours */
static int
orig_word (unsigned a, unsigned w, unsigned lo, unsigned hi, unsigned *orig)
{
  if ((w & 0xfff000f0u) == 0xe7f000f0u)
    {
      int imm = udf_imm (w);
      if (imm < UDF_BASE || (unsigned) (imm - UDF_BASE) >= nsites)
	return 0;
      struct site *t = &sites[imm - UDF_BASE];
      unsigned x, y, z;
      if (t->addr == a)
	{
	  *orig = t->orig;
	  return 1;
	}
      /* (0.46) the words either side come from memory when they are
	 outside what is being written (a copy at the first or last word
	 of a write: one made in pieces left the UDF in the file); they
	 are only compared, and read safely */
      unsigned before, after;
      if (a - 4 >= lo)
	before = *(volatile unsigned *) (a - 4);
      else if (!safe_rd (a - 4, &before))
	return 0;
      if (a + 8 <= hi)
	after = *(volatile unsigned *) (a + 4);
      else if (!safe_rd (a + 4, &after))
	return 0;
      if (!safe_rd (t->addr, &x) || x != w
	  || !safe_rd (t->addr - 4, &y) || y != before
	  || !safe_rd (t->addr + 4, &z) || z != after)
	return 0;
      *orig = t->orig;
      return 1;
    }
  if (a < 0x20000000u)
    return 0;
  if ((w & 0x0f000000u) == 0x0a000000u && stubs)
    {
      unsigned to = a + 8 + ((int) (w << 8) >> 6);
      unsigned off = to - (unsigned) stubs;
      if (off < nstub * 4 && !(off % 28))
	{
	  const unsigned *st = (const unsigned *) to;
	  if ((st[0] & 0xffff0fffu) == 0xee1d0f70u
	      && st[6] == branch_to ((unsigned) (st + 6), a + 4, 14))
	    {
	      *orig = (w & 0xf0000000u) | 0x010f0000u | (st[0] & 0xf000u);
	      return 1;
	    }
	}
      return 0;
    }
  if (w == 0xe1a00000u)
    for (unsigned i = 0; i < nnops; i++)
      if (nop_addr[i] == a)
	{
	  *orig = nop_orig[i];
	  return 1;
	}
  return 0;
}

struct fixargs { unsigned lo, hi, n; char *buf; };

/* how many words of ours [lo, hi) has; with buf, the copy made and fixed */
static int
p_fix (void *arg)
{
  struct fixargs *f = arg;
  unsigned o;
  f->n = 0;
  if (f->buf)
    memcpy (f->buf, (const void *) f->lo, f->hi - f->lo);
  for (unsigned a = (f->lo + 3) & ~3u; a + 4 <= f->hi; a += 4)
    {
      unsigned w = *(volatile unsigned *) a;
      if ((w & 0xfff000f0u) != 0xe7f000f0u
	  && (a < 0x20000000u
	      || ((w & 0x0f000000u) != 0x0a000000u && w != 0xe1a00000u)))
	continue;
      if (!orig_word (a, w, f->lo, f->hi, &o))
	continue;
      if (f->buf)
	*(unsigned *) (f->buf + (a - f->lo)) = o;
      f->n++;
    }
  return 1;
}

extern char ns_generr[];

/* The write of [lo, lo+len), from a fixed copy if it has words of ours.
   0: nothing to do (the SWI goes on as it came, or - a plain SWI's error
   - as OS_GenerateError); 1: done here */
static int
write_fixed (unsigned swi, unsigned *r, unsigned lo, unsigned len,
	     unsigned vcpu, unsigned *spsr, int xswi)
{
  if (!nsites || !len || lo < 0x8000u || len > 0x4000000u || lo + len < lo
      || overlaps (lo, lo + len, ROM_LO, ROM_HI - ROM_LO)
      || overlaps (lo, lo + len, self_lo, self_hi - self_lo))
    return 0;
  struct fixargs f = { lo, lo + len, 0, 0 };
  if (!probe_call (p_fix, &f) || !f.n)
    return 0;
  /* (SWIs from here change the caller's saved PSR in the vcpu, which the
     SWI's return uses: kept) */
  unsigned saved = *(volatile unsigned *) (vcpu + 24);
  char *m = rma_claim (len + 8);
  *(volatile unsigned *) (vcpu + 24) = saved;
  if (!m)
    {
      fix_nomem++;
      return 0;
    }
  f.buf = m + (lo & 3);
  regs v;
  for (int k = 0; k < 10; k++)
    v.r[k] = r[k];
  oserror *err = NULL;
  int ok = probe_call (p_fix, &f) && f.n;
  if (ok)
    {
      if (swi == 0x0C)
	v.r[2] = (unsigned) f.buf;
      else
	v.r[4] = (unsigned) f.buf, v.r[5] = (unsigned) f.buf + len;
      err = swix_regs (swi, &v);	/* (back through this vector) */
    }
  rma_free (m);
  *(volatile unsigned *) (vcpu + 24) = saved;
  if (!ok)
    return 0;
  ns.unpatched_words += f.n;
  ns.unpatched_writes++;
  if (err)
    {
      fix_errors++;
      r[0] = (unsigned) err;
      if (!xswi)
	{
	  /* a plain SWI: its error is raised, as the kernel would have:
	     the SWI goes on as OS_GenerateError (ns_generr, header.s) */
	  r[13] = (unsigned) ns_generr + 4;
	  return 0;
	}
      *spsr |= CPSR_V;
      return 1;
    }
  if (swi == 0x0C)
    {
      /* r2: the byte after the last one written, r3: those not written
	 (C set if any), r4: the file's pointer */
      r[2] = lo + (v.r[2] - (unsigned) f.buf);
      r[3] = v.r[3];
      r[4] = v.r[4];
      if (v.r[3])
	*spsr |= CPSR_C;
      else
	*spsr &= ~CPSR_C;
    }
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
	unsigned lo, len;
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
	return write_fixed (swi & 0xfdffff, r, lo, len, vcpu, spsr,
			    (swi & 0x20000) != 0);
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
      if (r[0] == 1)
	rom_pmp_dirty = 1;		/* removing one: look again next time */
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

