/* NativeSupport: SWIs without a trap.
   Part of native.c (included there; not compiled on its own). */
/* ------------------------------------------------------------------------
   SWIs without a trap (0.34)

   Natively a SWI is a Linux trap: about 0.75 microseconds on a Pi 4 with
   the fast entry, where RISC OS itself takes about 0.2. The ROM's busiest
   SWI sites (found by the fast entry's hook: SWIS_THRESH traps from the
   same word) are made branches to stubs (header.s, ns_swis_tmpl) that go
   to RISC OS's SWI vector as the port's own SWI entry does, in user mode,
   without Linux. The stubs are in the ROM image's fill (see swis_init).
   Only in the ROM's OS image (never copied as data, as a
   program's image can be: see patch_range), ARM code, and not OS_WriteS
   (its string follows the SWI), OS_EnterOS or OS_BreakPt. Needs the ROM's
   PVirt_SWIStubs block (5.31-20): its interrupt handler holds interrupts
   back in the stub area, as in its own PVOps routines.
   *NativeSupport SWIStubs On|Off; NativeSupport$NoSWIStubs (set when it
   starts): off. */
#define SWIS_THRESH	16u
#define SWIS_TABLE	4096u
#define SWIS_MAGIC	0x53495753u		/* "SWIS" */
extern char ns_swis_tmpl[], ns_swis_common[], ns_swis_vmode[], ns_swis_coll[], ns_swis_tmpl_end[];
extern char ns_swih_tmpl[], ns_swih_lo[], ns_swih_hi[], ns_swih_table[],
  ns_swih_thresh[], ns_swih_queue[], ns_swih_rom[], ns_swih_tmpl_end[];
extern unsigned ns_swiq, ns_swis_area, ns_swis_fixed;

static void
cacheflush (unsigned lo, unsigned hi)
{
  lsys3 (0xf0002, lo, hi, 0);
}

/* The ROM's block's lo and hi (in its HAL's data; in a test ROM, in its
   code's fill: written through mprotect), with interrupts off */
static void
swis_blk_set (unsigned lo, unsigned hi)
{
  int ro = sw.blk >= 0x20808000u;
  if (ro && !rom_writable (sw.blk, sw.blk + 12, 1))
    return;
  unsigned vcpu = get_tls ();
  volatile uint8_t *cpsr = (volatile uint8_t *) (vcpu + 13);
  int was_on = !(*cpsr & 0x80);
  regs r = {{ 0 }};
  if (was_on)
    swix_regs (0x14, &r);		/* OS_IntOff */
  *(volatile unsigned *) (sw.blk + 4) = lo;
  *(volatile unsigned *) (sw.blk + 8) = hi;
  if (was_on)
    swix_regs (0x13, &r);		/* OS_IntOn */
  if (ro)
    rom_writable (sw.blk, sw.blk + 12, 0);
}

static void
swis_init (void)
{
  sw.state = -1;
  regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
  if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2])
    return;
  unsigned b = RD (h.r[2] + 140);		/* PVirt_SWIStubs */
  if (b - 0x20800000u >= 0x400000u || (b & 3) || RD (b) != SWIS_MAGIC)
    return;
  if (!ns_vmode_offsets && !find_vmode_offsets ())
    return;
  /* The area: in the ROM image's own fill (&FF words after its last
     module: 480KB in 5.31-19), the highest 64KB there, within a branch
     of every SWI in the ROM (the RMA is too far) and an address nothing
     else is given. Made writable for good. */
  unsigned area = 0, run = 0;
  for (unsigned a = ROMOS_HI - SWIS_PAGE; a >= ROMOS_LO; a -= 4)
    {
      if (RD (a) != 0xffffffffu)
	{
	  run = 0;
	  continue;
	}
      run += 4;
      if (run >= SWIS_PAGES * SWIS_PAGE && !(a & (SWIS_PAGE - 1)))
	{
	  area = a;
	  break;
	}
    }
  if (!area || !rom_writable (area, area + SWIS_PAGES * SWIS_PAGE, 1))
    return;
  sw.table = rma_claim (SWIS_TABLE * 8);
  sw.queue = rma_claim (66 * 4);
  sw.site = rma_claim (SWIS_MAX * 4);
  sw.word = rma_claim (SWIS_MAX * 4);
  if (!sw.table || !sw.queue || !sw.site || !sw.word)
    return;
  sw.blk = b;
  sw.area = area;
  for (unsigned i = 0; i < SWIS_TABLE * 2; i++)
    sw.table[i] = 0;
  for (unsigned i = 0; i < 66; i++)
    sw.queue[i] = 0;
  /* each pair: the template so that its common code starts the code
     page, its working words ending the data page */
  unsigned tl = ns_swis_tmpl_end - ns_swis_tmpl;
  unsigned cl = ns_swis_common - ns_swis_tmpl;
  for (unsigned p = 0; p < SWIS_PAIRS; p++)
    {
      char *t = (char *) (swis_code (p) - cl);
      for (unsigned i = 0; i < tl; i++)
	t[i] = ns_swis_tmpl[i];
      *(unsigned *) (t + (ns_swis_vmode - ns_swis_tmpl)) = ns_vmode_offsets;
    }
  char *hk = (char *) (sw.area + SWIS_FIRST);
  unsigned hl = ns_swih_tmpl_end - ns_swih_tmpl;
  for (unsigned i = 0; i < hl; i++)
    hk[i] = ns_swih_tmpl[i];
  *(unsigned *) (hk + (ns_swih_lo - ns_swih_tmpl)) = ROMOS_LO;
  *(unsigned *) (hk + (ns_swih_hi - ns_swih_tmpl)) = ROMOS_HI;
  *(unsigned *) (hk + (ns_swih_table - ns_swih_tmpl)) = (unsigned) sw.table;
  *(unsigned *) (hk + (ns_swih_thresh - ns_swih_tmpl)) = SWIS_THRESH;
  *(unsigned *) (hk + (ns_swih_queue - ns_swih_tmpl)) = (unsigned) sw.queue;
  *(unsigned *) (hk + (ns_swih_rom - ns_swih_tmpl)) = ns.swi_handler;
  cacheflush (sw.area, sw.area + SWIS_PAGES * SWIS_PAGE);
  swis_blk_set (sw.area, sw.area + SWIS_HELD * SWIS_PAGE);
  ns_swis_area = sw.area;		/* (header.s: swis_fixlr) */
  sw.state = 0;
}

/* A busy site (from the queue): its SWI made a branch to a stub */
static void
swis_patch (unsigned site)
{
  unsigned w = RD (site);
  if ((w & 0x0f000000u) != 0x0f000000u || (w >> 28) == 15)
    return;				/* (not a SWI now: given one, or
					   changed) */
  unsigned n = w & 0xfdffffu;
  if (n == 0x01 || n == 0x16 || n == 0x17	/* OS_WriteS, OS_EnterOS, OS_BreakPt */
      || site - ROMOS_LO >= ROMOS_HI - ROMOS_LO)
    {
      sw.excluded++;
      return;
    }
  unsigned i, s = 0;
  for (i = 0; i < sw.n; i++)
    if (sw.site[i] == site && sw.word[i] == w)
      {
	s = swis_stub (i);
	sw.reused++;
	break;
      }
  if (!s)
    {
      if (sw.n >= SWIS_MAX)
	{
	  sw.full++;
	  return;
	}
      s = swis_stub (sw.n);
      unsigned e = swis_exit (sw.n);
      unsigned common = s & ~(SWIS_PAGE - 1);
      unsigned words = common - (ns_swis_common - ns_swis_tmpl);
      unsigned back = branch_to (e + 4, site + 4, 14);
      if (!back || !branch_to (site, s, 14))
	{
	  sw.range++;
	  return;
	}
      volatile unsigned *p = (volatile unsigned *) s;
      volatile unsigned *x = (volatile unsigned *) e;
      x[0] = w;							/* swi n */
      x[1] = back;						/* b site + 4 */
      p[0] = 0xe50fe000u | ((s + 8) - words);			/* str lr, scr_lr */
      p[1] = 0xeb000000u | (((common - (s + 12)) >> 2) & 0xffffffu);	/* bl common */
      p[2] = e + 4;						/* the SWI's return */
      p[3] = 0xe7f000f0u;					/* (never run) */
      cacheflush (s, s + 16);
      cacheflush (e, e + 8);
      sw.site[sw.n] = site;
      sw.word[sw.n] = w;
      sw.n++;
    }
  unsigned br = branch_to (site, s, w >> 28);
  if (!rom_writable (site, site + 4, 1))
    return;
  *(volatile unsigned *) site = br;
  rom_writable (site, site + 4, 0);
  cacheflush (site, site + 4);
  sw.patched++;
}

/* From ns_swi_vector (header.s), when the queue has sites: ret is this
   SWI's return address, whose site isn't patched now */
void ns_swiq_drain (unsigned ret);
void
ns_swiq_drain (unsigned ret)
{
  static int busy;
  unsigned *q = sw.queue;
  if (busy || !q)
    return;
  busy = 1;
  while (q[1] != q[0])
    {
      unsigned site = q[2 + (q[1] & 63)];
      if (site == ret - 4)
	break;
      q[1]++;
      if (sw.state > 0)
	swis_patch (site);
    }
  busy = 0;
}

/* *NativeSupport SWIStubs Off: every site back as it was (the stubs stay:
   a return address can be in one) */
static void
swis_unpatch_all (void)
{
  for (unsigned i = 0; i < sw.n; i++)
    {
      unsigned site = sw.site[i], s = swis_stub (i);
      if (RD (site) != branch_to (site, s, sw.word[i] >> 28)
	  || !rom_writable (site, site + 4, 1))
	continue;
      *(volatile unsigned *) site = sw.word[i];
      rom_writable (site, site + 4, 0);
      cacheflush (site, site + 4);
      sw.unpatched++;
    }
}

static void
swis_set (int on)
{
  if (sw.state < 0 || on == sw.state)
    return;
  if (!on)
    swis_unpatch_all ();
  else
    sw.queue[1] = sw.queue[0];
  sw.state = on;
  ns_swiq = on ? (unsigned) sw.queue : 0;
  if (on)
    for (unsigned i = 0, n = sw.n; i < n; i++)
      swis_patch (sw.site[i]);		/* their stubs again, at once */
  if (ns.fastswi > 0)
    fastswi_set (1);			/* the hook, or the ROM's handler */
}

/* 0.39: stub calls that began while another in the same page pair was
   between its common code's start and its jump to the SWI vector (each
   pair's working words; never more than 0 unless they're shared) */
static unsigned
swis_overlaps (void)
{
  unsigned n = 0, cl = ns_swis_common - ns_swis_tmpl;
  unsigned off = ns_swis_coll - ns_swis_tmpl;
  if (sw.state < 0)
    return 0;
  for (unsigned p = 0; p < SWIS_PAIRS; p++)
    n += RD (swis_code (p) - cl + off);
  return n;
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
  site_hash = rma_claim (SITE_HASH * sizeof *site_hash);
  for (unsigned i = 0; site_hash && i < SITE_HASH; i++)
    site_hash[i] = 0;
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
  /* (NativeSupport$NoCondNOP set when it starts: the never-run LDMNE ^
     after TEQ pc, pc keep trapping, and are skipped each time) */
  {
    regs v = {{ (unsigned) "NativeSupport$NoCondNOP", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    nocondnop = v.r[2] != 0;
  }
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
  swis_init ();
  {
    regs v = {{ (unsigned) "NativeSupport$NoSWIStubs", 0, -1, 0, 0 }};
    swix_regs (OS_ReadVarVal, &v);
    if (!v.r[2])
      swis_set (1);
  }
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
  if (r->r[1] == 0xB9 && ns.active && (r->r[0] < ROM_LO || r->r[0] >= ROM_HI)
      && !(leave_module (r->r[0], r->r[2] - 4) && ++modules_left))
    {
      unsigned v0 = veneer_psr;
      if (module_32bit (r->r[0], r->r[2] - 4))
	cond_nop_range (r->r[0], r->r[0] + r->r[2] - 4);
      ns.patched += patch_range (r->r[0], r->r[0] + r->r[2] - 4);
      /* (0.44) what was changed here reaches the instruction cache: the
	 kernel's own synchronisation of the module came before this
	 service call (a module made from memory may have had none) */
      cacheflush (r->r[0], r->r[0] + r->r[2]);
      if (aeabi105_fix (r->r[0], r->r[2]))
	{
	  cacheflush (r->r[0] + AEABI105_SIZE, r->r[0] + AEABI105_AFTER + 4);
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
      static const char words[4][10] = { "fastswi", "fastfpa", "syncpatch", "swistubs" };
      for (int k = 0; k < 4; k++)
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
	  else if (k == 3)
	    {
	      if (on || off)
		swis_set (on);
	      write0 ("SWIs without a trap: ");
	      write0 (sw.state > 0 ? "on" : sw.state ? "not in this ROM" : "off");
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
  write0 ("SWIs without a trap (*NativeSupport SWIStubs On|Off): ");
  write0 (sw.state > 0 ? "on" : sw.state ? "not in this ROM" : "off");
  newline ();
  if (sw.state >= 0)
    {
      line_out ("ROM SWI sites given stubs: ", sw.n);
      line_out ("Stub calls overlapping another (should be 0): ", swis_overlaps ());
      line_out ("Sites given their stub while their SWI was on its way, sent through it (should be 0): ", ns_swis_fixed);
      line_out ("Patched (stubs made or used again): ", sw.patched);
      line_out ("Busy sites found: ", sw.queue[0]);
      line_out ("Busy sites left, out of the stubs' reach: ", sw.range);
      line_out ("Busy sites left, OS_WriteS etc.: ", sw.excluded);
      line_out ("Busy sites left, no stubs left: ", sw.full);
      line_out ("Stub area: ", sw.area);
      /* the sites that trap most (a patched site stops counting) */
      unsigned done[6], nd = 0;
      for (int r = 0; r < 6; r++)
	{
	  unsigned best = 0, bi = ~0u;
	  for (unsigned i = 0; i < SWIS_TABLE; i++)
	    {
	      int seen = 0;
	      for (unsigned j = 0; j < nd; j++)
		seen |= done[j] == i;
	      if (!seen && sw.table[2 * i] && sw.table[2 * i + 1] > best)
		best = sw.table[2 * i + 1], bi = i;
	    }
	  if (bi == ~0u)
	    break;
	  done[nd++] = bi;
	  unsigned site = sw.table[2 * bi], st = 0;
	  for (unsigned k = 0; k < sw.n; k++)
	    st |= sw.site[k] == site;
	  write0 ("SWI site trapping most: ");
	  write_hex8 (site);
	  write0 (st ? " (stub) " : " ");
	  write_hex8 (*(unsigned *) site);
	  write0 (" traps ");
	  write_dec (best);
	  where_out (site);
	  newline ();
	}
    }
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
  line_out ("Writes left as they were (no memory for the copy): ", fix_nomem);
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
  line_out ("  of them passed on as data aborts: ", ns.emul_aborts);
  line_out ("Sites in use (of 16384): ", nsites);
  line_out ("Sites used again (the same instruction at the same address): ", sites_reused);
  line_out ("Instructions that couldn't be done, shown to RISC OS as themselves: ", sites_suspended);
  line_out ("Modules left unpatched (NativeSupport$Leave): ", modules_left);
  line_out ("Sites or stubs not made (tables full): ", ns.full);
  line_out ("Trapped instructions whose condition failed (skipped): ", ns.cond_skipped);
  line_out ("Never-run privileged instructions (TEQ pc, pc; LDMNE ^) made NOPs: ", ns.cond_nopped);
  line_out ("MRS reading only the flags (_swix) left to run: ", mrs_flags_left);
  line_out ("Words loaded as literals left (data, not instructions): ", literals_left);
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
