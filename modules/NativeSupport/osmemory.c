/* NativeSupport: OS_Memory 0, the memory map report and where dynamic areas go.
   Part of native.c (included there; not compiled on its own). */
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
	  /* (0.44: 255 at most, then the oldest again: the 256th's
	     number didn't fit its 8 bits and came out as 0, and later
	     ones as the first's) */
	  if (k == nids && nids < 255)
	    {
	      ids[nids].dev = dev;
	      ids[nids].ino = ino;
	      nids++;
	    }
	  else if (k == nids)
	    {
	      static unsigned next;
	      k = next++ % 255;
	      ids[k].dev = dev;
	      ids[k].ino = ino;
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

