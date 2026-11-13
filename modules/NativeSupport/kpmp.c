/* NativeSupport: the ROM's PMP stubs, done here (ROMs before 5.31-3).
   Part of native.c (included there; not compiled on its own). */
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
#define DANODE_WORKSPACE 24
#define DANODE_HANDLER 28
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

/* Whether n, found by looking through the system heap for words like a
   dynamic area node's, is the node of a dynamic area: the kernel's own
   answer for its number (OS_DynamicArea 2) has the same base, flags,
   handler, workspace and title. Any heap block can hold such words: one
   taken for a PMP area's node gave kpmp_access a "DANode_PMP" that isn't
   a pointer, and from the next dynamic area made on (0.30, 0.31) RISC OS
   aborted in NativeSupport at every address check and the desktop
   stopped (EDT's report). (Not the maximum size: for a PMP area r5 is
   its pool's.) A SWI from here changes the caller's saved PSR in the
   vcpu: it's kept. */
static int
da_node_real (unsigned n)
{
  unsigned vcpu = get_tls ();
  unsigned saved = *(volatile unsigned *) (vcpu + 24);
  regs d = {{ 2, RD (n + DANODE_NUMBER) }};
  int bad = swix_regs (0x66, &d) != 0;	/* OS_DynamicArea 2 */
  *(volatile unsigned *) (vcpu + 24) = saved;
  return !bad && d.r[3] == RD (n + DANODE_BASE) && d.r[4] == RD (n + DANODE_FLAGS)
	 && d.r[6] == RD (n + DANODE_HANDLER) && d.r[7] == RD (n + DANODE_WORKSPACE)
	 && d.r[8] == RD (n + DANODE_TITLE);
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
	  && !(w[2] & 0xfff) && w[5] && !(w[5] & 0xfff) && w[8]
	  && da_node_real (n))
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
	      && !(w[5] & 0xfff) && w[8] && w[1] < 0x100000u
	      && da_node_real (n))
	    rom_pmp_nodes[rom_pmp_n++] = n;
	}
    }
  for (unsigned i = 0; i < rom_pmp_n; i++)
    {
      unsigned n = rom_pmp_nodes[i];
      /* checked again with RISC OS before it's used: an area removed
	 since (a UnixLib program's, as it ends) leaves its node freed in
	 the heap, words and all (crossdev's report: 0.31 then aborted at
	 the next module load) */
      if (rom_pmp_node_ok (n) && lo >= RD (n + DANODE_BASE)
	  && lo - RD (n + DANODE_BASE) < RD (n + DANODE_MAXSIZE))
	{
	  if (da_node_real (n))
	    return n;
	  rom_pmp_dirty = 1;
	  return 0;
	}
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

