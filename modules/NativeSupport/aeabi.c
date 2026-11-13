/* NativeSupport: the test bed's ARMEABISupport (lite): SWIs &5AF40, &5AF41 and OS_DynamicArea 22.
   Part of native.c (included there; not compiled on its own). */
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

