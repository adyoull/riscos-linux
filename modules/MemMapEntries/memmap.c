/* MemMapEntries: OS_ReadMemMapEntries (&52), OS_SetMemMapEntries (&53) and
   OS_FindMemMapEntries (&60) for RISC OS for Linux.

   On other machines these three give programs RISC OS's page map: which
   page of memory is at a logical address, and a way to put a page at
   another address. Programs with their own virtual memory use them
   (Photodesk finds the pages of its dynamic area and reorders them
   without copying). The Linux port's kernel has no page map - memory is
   Linux's - so there they are "SWI not known" (Kernel/s/Linux:
   "Unimplemented paged memory SWIs"), and such programs stop.

   Here they are done with Linux's own page moving. A dynamic area's
   memory in the port is an anonymous mapping; mremap() moves its pages to
   another address without copying, which is what OS_SetMemMapEntries
   does. What Linux hasn't got is a number for a page, so this module
   gives them out: a page is numbered when OS_FindMemMapEntries first asks
   about its address, and the number stays with that page's memory
   wherever it is moved.

   - OS_FindMemMapEntries: for each address in the list, the number of
     the page there (a new number the first time) and its protection
     level; -1 and protection 3 if nothing is mapped there.
   - OS_ReadMemMapEntries: for each page number, where it is (-1 if it has
     been put out of the way) and its protection level.
   - OS_SetMemMapEntries: each page in the list is moved to its address,
     or, with address -1, out of the way (kept, mapped nowhere RISC OS
     looks). The whole list is done as one change, as the pages of a real
     page map exist apart from where they are mapped: all the pages are
     lifted out first and then put down, so a list that swaps pages
     about works in any order. A page that was at one of the addresses
     and isn't itself moved by the list is put out of the way, not lost.
     An address a page is moved from is left as fresh (zero) memory.

   Protection levels are remembered, not enforced: a page has level 0
   until OS_SetMemMapEntries gives it another, and that is what the other
   two then report, but every page stays readable and writable. (The port
   runs everything - RISC OS's kernel and modules too - in Linux user
   mode, so "no user access" would lock out the module that asked for
   it.) A program that sets a level and reads it back sees what it set;
   one that counts on a fault from a protected page won't get it.

   Not done: application space (it is mapped
   again at every task switch, which would undo any move: refused); an
   error from a SWI called without the X bit is returned with V set but
   not raised.

   The SWIs are kernel SWIs, so they are taken on the SWI vector
   (OS_ClaimProcessorVector), as NativeSupport takes the few it answers;
   every other SWI goes straight on. Linux is called through the port's
   PVOps device's LinuxSyscall entry, never through a SWI from in here.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "cmodule.h"

#define PAGE 4096u
#define RD(a) (*(volatile unsigned *) (a))

#define SYS_munmap	91
#define SYS_mprotect	125
#define SYS_mremap	163
#define SYS_mmap2	192
#define SYS_mincore	219
#define PROT_NONE	0
#define PROT_RWX	7
#define MAP_PRIVATE	0x02
#define MAP_FIXED	0x10
#define MAP_ANONYMOUS	0x20
#define MAP_NORESERVE	0x4000
#define MREMAP_MAYMOVE	1
#define MREMAP_FIXED	2

#define APP_LO	0x8000u			/* application space: refused */
#define APP_HI	0x20000000u

extern void mm_swi_vector (void);
extern unsigned mm_old_swi_vector, mm_movs_pc_lr;

static unsigned sys_entry;		/* the port's LinuxSyscall routine */
static int claimed, dormant;

static struct
{
  unsigned finds, reads, sets, found, moved, parked, refused, calls_failed, forgotten, looks;
} count;

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
  register unsigned ip asm ("ip") = sys_entry;
  asm volatile ("blx	ip"
		: "+r" (r0), "+r" (ip)
		: "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5), "r" (r7)
		: "lr", "memory", "cc");
  return (int) r0;
}

static int
bad (int r)				/* a Linux error (-1 to -4095) */
{
  return (unsigned) r >= 0xfffff001u;
}

/* memory of the module's own, from Linux (anywhere it likes) */
static void *
lalloc (unsigned bytes)
{
  int r = lsys (SYS_mmap2, 0, bytes, 3, MAP_PRIVATE | MAP_ANONYMOUS, -1u, 0);
  return bad (r) ? NULL : (void *) r;
}

static void
lfree (void *p, unsigned bytes)
{
  if (p)
    lsys (SYS_munmap, (unsigned) p, bytes, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------------
   The pages. where[n]: the address page n is at; PARKED | its address
   when it is out of the way; GONE when its memory has gone. mark[n]: the
   OS_SetMemMapEntries call that last moved it. at[]: a hash table from an
   address to the page there (the address's page number + 1; 0 empty, 1 a
   slot emptied). */
#define PARKED	1u
#define GONE	0xfffffffeu

static unsigned *where, *mark, *first, *level, npages, cap_pages;	/* first[n]: where it was found; level[n]: its protection level, as last set */
static int untracked;			/* pages numbered since the areas were last looked at */
static struct slot { unsigned key, page; } *at;
static unsigned at_size, at_used;	/* at_used counts emptied slots too */
static unsigned serial;

static unsigned
hash (unsigned key)
{
  return (key * 2654435761u) >> 7;
}

static struct slot *
at_find (unsigned addr)
{
  if (!at)
    return NULL;
  unsigned key = (addr >> 12) + 2;
  for (unsigned i = hash (key);; i++)
    {
      struct slot *s = &at[i & (at_size - 1)];
      if (s->key == key)
	return s;
      if (s->key == 0)
	return NULL;
    }
}

static void
at_put_raw (unsigned key, unsigned page)
{
  for (unsigned i = hash (key);; i++)
    {
      struct slot *s = &at[i & (at_size - 1)];
      if (s->key == 0)
	{
	  s->key = key;
	  s->page = page;
	  at_used++;
	  return;
	}
    }
}

static int
at_grow (void)
{
  unsigned live = 0;
  for (unsigned i = 0; i < at_size; i++)
    live += at[i].key >= 2;
  unsigned n = at_size ? at_size : 4096;
  while (n < live * 4 + 4096)
    n *= 2;
  struct slot *old = at, *t = lalloc (n * sizeof *t);
  unsigned old_size = at_size;
  if (!t)
    return 0;
  at = t;
  at_size = n;
  at_used = 0;
  for (unsigned i = 0; i < old_size; i++)
    if (old[i].key >= 2)
      at_put_raw (old[i].key, old[i].page);
  lfree (old, old_size * sizeof *old);
  return 1;
}

static int
at_put (unsigned addr, unsigned page)
{
  struct slot *s = at_find (addr);
  if (s)
    {
      s->page = page;
      return 1;
    }
  if (at_used * 2 >= at_size && !at_grow ())
    return 0;
  at_put_raw ((addr >> 12) + 2, page);
  return 1;
}

static void
at_remove (unsigned addr)
{
  struct slot *s = at_find (addr);
  if (s)
    s->key = 1;
}

/* the page at an address, or -1 */
static unsigned
page_at (unsigned addr)
{
  struct slot *s = at_find (addr);
  return s ? s->page : -1u;
}

static unsigned
new_page (unsigned addr)
{
  if (npages == cap_pages)
    {
      unsigned n = cap_pages ? cap_pages * 2 : 16384;
      unsigned *w = lalloc (n * 4 * sizeof *w);
      if (!w)
	return -1u;
      memcpy (w, where, npages * sizeof *w);
      memcpy (w + n, mark, npages * sizeof *w);
      memcpy (w + 2 * n, first, npages * sizeof *w);
      memcpy (w + 3 * n, level, npages * sizeof *w);
      lfree (where, cap_pages * 4 * sizeof *w);
      where = w;
      mark = w + n;
      first = w + 2 * n;
      level = w + 3 * n;
      cap_pages = n;
    }
  if (!at_put (addr, npages))
    return -1u;
  where[npages] = first[npages] = addr;
  level[npages] = 0;
  mark[npages] = 0;
  untracked = 1;
  return npages++;
}

static int
mapped (unsigned addr)
{
  unsigned char v;
  return lsys (SYS_mincore, addr, PAGE, (unsigned) &v, 0, 0, 0) == 0;
}

/* ------------------------------------------------------------------------
   Moving pages */

/* n pages from one address to another, both whole pages; what was at the
   destination goes. keep_source: the address moved from is left as fresh
   memory (mremap leaves nothing there; MREMAP_DONTUNMAP would do it in
   one call, but the test bed's QEMU doesn't know the memory is still
   mapped after it). */
static int
move_1 (unsigned from, unsigned to, unsigned n, int keep_source)
{
  int r = lsys (SYS_mremap, from, n * PAGE, n * PAGE, MREMAP_MAYMOVE | MREMAP_FIXED, to, 0);
  if (bad (r))
    return 0;
  if (keep_source)
    lsys (SYS_mmap2, from, n * PAGE, PROT_RWX, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1u, 0);
  return 1;
}

/* The same; a run that Linux won't move in one (mremap takes one mapping
   at a time, and pages moved before are mappings of their own) goes page
   by page. */
static int
move (unsigned from, unsigned to, unsigned n, int keep_source)
{
  if (move_1 (from, to, n, keep_source))
    return 1;
  if (n == 1)
    return 0;
  for (unsigned i = 0; i < n; i++)
    if (!move_1 (from + i * PAGE, to + i * PAGE, 1, keep_source))
      return 0;
  return 1;
}

/* address space of n pages where nothing is, or 0 */
static unsigned
space (unsigned n)
{
  int r = lsys (SYS_mmap2, 0, n * PAGE, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1u, 0);
  return bad (r) ? 0 : (unsigned) r;
}

/* Keeps hold of n pages of the staging space that pages have just been
   moved out of. mremap leaves nothing there, and Linux would give that
   address space to the next thing that asks: the module's own tables,
   which are then lost when the staging space is given back whole. */
static void
hold (unsigned addr, unsigned n)
{
  lsys (SYS_mmap2, addr, n * PAGE, PROT_NONE,
	MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1u, 0);
}

/* puts a page that is at addr out of the way */
static int
park (unsigned page, unsigned addr, int keep_source)
{
  unsigned p = space (1);
  if (!p || !move (addr, p, 1, keep_source))
    return 0;
  where[page] = p | PARKED;
  count.parked++;
  return 1;
}

ERROR_BLOCK (bad_page, 0x1C3, "Bad page number (MemMapEntries)");
ERROR_BLOCK (app_space, 0x1C4, "MemMapEntries can't move pages of application space");
ERROR_BLOCK (no_room, 0x1C5, "MemMapEntries: no room for the page move");
ERROR_BLOCK (move_failed, 0x1C6, "MemMapEntries: Linux wouldn't move the pages");

struct entry { unsigned page, addr, ppl; };

static oserror *
do_find (struct entry *e)
{
  count.finds++;
  for (; e->page != -1u; e++)	/* (the kernel stops at the first -1 too) */
    {
      unsigned a = e->addr & ~(PAGE - 1);
      unsigned p = page_at (a);
      if (p == -1u && a >= APP_LO && mapped (a))
	p = new_page (a);
      e->page = p;
      e->ppl = p == -1u ? 3 : level[p];
      count.found += p != -1u;
    }
  return NULL;
}

static oserror *
do_read (struct entry *e)
{
  count.reads++;
  for (; e->page != -1u; e++)
    {
      unsigned w = e->page < npages ? where[e->page] : GONE;
      if (w == GONE)
	return ERR (bad_page);
      e->addr = w & PARKED ? -1u : w;
      e->ppl = level[e->page];
    }
  return NULL;
}

static oserror *
do_set (struct entry *list)
{
  count.sets++;
  unsigned n = 0;
  for (struct entry *e = list; e->page != -1u; e++)
    n++;
  if (!n)
    return NULL;
  /* look at it all first: nothing is moved if any of it is wrong */
  if (++serial == 0)
    serial = 1;
  for (unsigned i = 0; i < n; i++)
    {
      struct entry *e = &list[i];
      if (e->page >= npages || where[e->page] == GONE)
	return ERR (bad_page);
      unsigned to = e->addr, from = where[e->page];
      if ((to != -1u && (to & (PAGE - 1))) || (to >= APP_LO && to < APP_HI)
	  || (!(from & PARKED) && from >= APP_LO && from < APP_HI))
	{
	  count.refused++;
	  return ERR (app_space);
	}
    }
  unsigned stage = space (n);
  unsigned staged = n;			/* (n shrinks if a move fails) */
  if (!stage)
    return ERR (no_room);
  /* the levels asked for: remembered (the entries' own words are used as
     scratch below, and put back from these at the end) */
  for (unsigned i = 0; i < n; i++)
    level[list[i].page] = list[i].ppl;
  /* lift each page out to the staging space (runs of pages at a time),
     but not one that is where it is to be already, or a second mention */
  oserror *err = NULL;
  for (unsigned i = 0; i < n;)
    {
      struct entry *e = &list[i];
      unsigned from = where[e->page];
      if (mark[e->page] == serial || from == e->addr
	  || ((from & PARKED) && e->addr == -1u))
	{
	  if (mark[e->page] != serial)
	    mark[e->page] = serial, e->ppl = -1u;	/* stays: skip below */
	  else
	    e->ppl = -1u;
	  i++;
	  continue;
	}
      unsigned run = 1, real = from & ~PARKED;
      mark[e->page] = serial;
      if (!(from & PARKED))
	while (i + run < n && list[i + run].page < npages
	       && mark[list[i + run].page] != serial
	       && where[list[i + run].page] == from + run * PAGE
	       && where[list[i + run].page] != list[i + run].addr)
	  mark[list[i + run].page] = serial, run++;
      if (!move (real, stage + i * PAGE, run, !(from & PARKED)))
	{
	  err = ERR (move_failed);
	  count.calls_failed++;
	  /* what is lifted so far is put back below, where it was */
	  for (unsigned j = i; j < i + run; j++)
	    list[j].ppl = -1u;
	  n = i;
	  break;
	}
      for (unsigned j = 0; j < run; j++)
	{
	  if (!(from & PARKED))
	    at_remove (from + j * PAGE);
	  where[list[i + j].page] = (stage + (i + j) * PAGE) | PARKED;
	  list[i + j].ppl = err ? -1u : from + j * PAGE;	/* (for undoing) */
	}
      if (from & PARKED)
	lsys (SYS_munmap, real, PAGE, 0, 0, 0, 0);
      i += run;
    }
  /* a page at one of the addresses that the list doesn't move: out of the way */
  if (!err)
    for (unsigned i = 0; i < n; i++)
      if (list[i].addr != -1u)
	{
	  unsigned o = page_at (list[i].addr);
	  if (o != -1u && mark[o] != serial)
	    {
	      at_remove (list[i].addr);
	      if (!park (o, list[i].addr, 1))
		where[o] = GONE;
	    }
	}
  /* put each down */
  for (unsigned i = 0; i < n;)
    {
      struct entry *e = &list[i];
      if (!(where[e->page] & PARKED) || (where[e->page] & ~PARKED) != stage + i * PAGE)
	{
	  i++;				/* one that stayed */
	  continue;
	}
      unsigned to = err ? e->ppl : e->addr, run = 1;
      if (to == -1u)
	{
	  if (!park (e->page, stage + i * PAGE, 0))
	    where[e->page] = GONE;
	  hold (stage + i * PAGE, 1);
	  i++;
	  continue;
	}
      while (i + run < n
	     && (where[list[i + run].page] & PARKED)
	     && (where[list[i + run].page] & ~PARKED) == stage + (i + run) * PAGE
	     && (err ? list[i + run].ppl : list[i + run].addr) == to + run * PAGE)
	run++;
      int ok = move (stage + i * PAGE, to, run, 0);
      hold (stage + i * PAGE, run);
      if (ok)
	for (unsigned j = 0; j < run; j++)
	  {
	    where[list[i + j].page] = to + j * PAGE;
	    at_put (to + j * PAGE, list[i + j].page);
	    count.moved++;
	  }
      else
	{
	  for (unsigned j = 0; j < run; j++)
	    where[list[i + j].page] = GONE;
	  if (!err)
	    err = ERR (move_failed);
	  count.calls_failed++;
	}
      i += run;
    }
  lsys (SYS_munmap, stage, staged * PAGE, 0, 0, 0, 0);
  /* the entries' protection levels are the caller's: put back what was
     used as scratch */
  for (struct entry *e = list; e->page != -1u; e++)
    e->ppl = level[e->page];
  return err;
}

/* From the SWI vector: r[0..12] and lr; the SWI number without its X bit */
oserror *mm_swi_c (unsigned *r, unsigned swi);
oserror *
mm_swi_c (unsigned *r, unsigned swi)
{
  struct entry *e = (struct entry *) r[0];
  switch (swi)
    {
    case 0x52: return do_read (e);
    case 0x53: return do_set (e);
    default: return do_find (e);
    }
}

/* ------------------------------------------------------------------------ */

static unsigned
get_tls (void)
{
  unsigned v;
  asm volatile ("mrc	p15, 0, %0, c13, c0, 3" : "=r" (v));
  return v;
}

ERROR_BLOCK (not_port, 0x1C7, "MemMapEntries is for RISC OS for Linux (no PVOps device)");
ERROR_BLOCK (no_vector, 0x1C8, "MemMapEntries couldn't claim the SWI vector");
ERROR_BLOCK (in_use, 0x1C9, "MemMapEntries can't leave: another module claimed the SWI vector after it");

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  /* the port's PVOps HAL device (type &30A): LinuxSyscall at +76,
     MOVS_PC_LR at +100 */
  regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
  if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2])
    return ERR (not_port);
  unsigned dev = h.r[2];
  sys_entry = RD (dev + 76);
  mm_movs_pc_lr = RD (dev + 100);
  /* a kernel that has the SWIs (a later ROM, perhaps): leave them to it */
  unsigned none[1] = { -1u };
  regs f = {{ (unsigned) none }};
  if (!swix_regs (0x60, &f))
    {
      dormant = 1;			/* (no error: this is loaded at start-up) */
      return NULL;
    }
  unsigned *vectors = (unsigned *) RD (get_tls () + 8);
  mm_old_swi_vector = vectors[1];
  regs v = {{ 0x102, (unsigned) mm_swi_vector }};
  if (swix_regs (0x69 /* OS_ClaimProcessorVector */, &v))
    return ERR (no_vector);
  claimed = 1;
  return NULL;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  if (claimed)
    {
      regs v = {{ 2, mm_old_swi_vector, (unsigned) mm_swi_vector }};
      if (swix_regs (0x69, &v))
	return ERR (in_use);
      claimed = 0;
    }
  /* pages out of the way go with the module: their memory is given back */
  for (unsigned i = 0; i < npages; i++)
    if (where[i] != GONE && (where[i] & PARKED))
      lsys (SYS_munmap, where[i] & ~PARKED, PAGE, 0, 0, 0, 0);
  lfree (where, cap_pages * 4 * sizeof *where);
  lfree (at, at_size * sizeof *at);
  where = mark = first = level = NULL;
  at = NULL;
  npages = cap_pages = at_size = at_used = 0;
  return NULL;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number; (void) r;
  return NULL;
}

/* ------------------------------------------------------------------------
   Memory that has gone. The dynamic areas there are now (OS_DynamicArea 3
   and 2): a page whose address is in none of them (past an area's end, or
   in one that has been removed) is forgotten; a page out of the way is
   given back when the address it was first found at is in no area's
   whole extent (its area has been removed). tracked[]: the areas with
   numbered pages in them, and their sizes then, so that the many
   Service_MemoryMoved calls that are nothing to do with them cost a call
   or two each. Application space isn't a numbered area: its pages (found,
   never moved) are left. */
#define AREAS_MAX 256
#define TRACKED_MAX 16
static struct area { unsigned number, base, size, max; } areas[AREAS_MAX];
static struct area tracked[TRACKED_MAX];
static unsigned ntracked;
static int track_all;			/* more areas with pages than tracked[] holds */

static int
area_info (unsigned number, struct area *a)
{
  regs r = {{ 2, number }};
  if (swix_regs (OS_DynamicArea, &r))
    return 0;
  a->number = number;
  a->size = r.r[2];
  a->base = r.r[3];
  a->max = r.r[5];
  return 1;
}

static void
look (void)
{
  unsigned n = 0;
  regs r = {{ 3, -1u }};
  count.looks++;
  while (n < AREAS_MAX && !swix_regs (OS_DynamicArea, &r) && r.r[1] != -1u)
    {
      /* (not the free pool, area 6: it has no addresses of its own, and
	 its "size" is all the free memory) */
      if (r.r[1] != 6 && area_info (r.r[1], &areas[n]) && areas[n].base >= APP_HI)
	n++;
      r.r[0] = 3;
    }
  if (n == AREAS_MAX)			/* more areas than that: forget nothing */
    return;
  unsigned char has[AREAS_MAX];
  memset (has, 0, n);
  for (unsigned i = 0; i < npages; i++)
    {
      unsigned w = where[i];
      if (w == GONE)
	continue;
      unsigned a = w & PARKED ? first[i] : w, k;
      if (a < APP_HI)
	continue;
      for (k = 0; k < n; k++)
	if (a - areas[k].base < (w & PARKED ? areas[k].max : areas[k].size))
	  break;
      if (k < n)
	{
	  has[k] = 1;
	  continue;
	}
      if (w & PARKED)
	lsys (SYS_munmap, w & ~PARKED, PAGE, 0, 0, 0, 0);
      else if (page_at (w) == i)
	at_remove (w);
      where[i] = GONE;
      count.forgotten++;
    }
  /* none left (the program that had them has gone): the numbers start
     again, so the tables don't grow with every run of it */
  unsigned live = 0;
  for (unsigned i = 0; i < npages && !live; i++)
    live = where[i] != GONE;
  if (!live)
    {
      npages = 0;
      memset (at, 0, at_size * sizeof *at);
      at_used = 0;
    }
  ntracked = 0;
  track_all = 0;
  for (unsigned k = 0; k < n; k++)
    if (has[k])
      {
	if (ntracked < TRACKED_MAX)
	  tracked[ntracked++] = areas[k];
	else
	  track_all = 1;
      }
  untracked = 0;
}

void
mod_service (regs *r)
{
  if (!npages)
    return;
  if (r->r[1] == 0x91 || untracked || track_all)
    {
      look ();
      return;
    }
  /* Service_MemoryMoved: has one of the areas with pages got smaller? */
  for (unsigned i = 0; i < ntracked; i++)
    {
      struct area a;
      if (!area_info (tracked[i].number, &a) || a.base != tracked[i].base
	  || a.size < tracked[i].size)
	{
	  look ();
	  return;
	}
      tracked[i].size = a.size;
    }
}

static void
line (const char *s, unsigned v)
{
  write0 (s);
  write_dec (v);
  newline ();
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  if (dormant)
    {
      write0 ("This RISC OS has OS_FindMemMapEntries itself: MemMapEntries is doing nothing");
      newline ();
      return NULL;
    }
  unsigned out = 0;
  for (unsigned i = 0; i < npages; i++)
    out += where[i] != GONE && (where[i] & PARKED);
  line ("Pages numbered now: ", npages);
  line ("Pages out of the way: ", out);
  line ("OS_FindMemMapEntries calls: ", count.finds);
  line ("  pages found: ", count.found);
  line ("OS_ReadMemMapEntries calls: ", count.reads);
  line ("OS_SetMemMapEntries calls: ", count.sets);
  line ("  pages moved: ", count.moved);
  line ("  refused (application space): ", count.refused);
  line ("  moves Linux refused: ", count.calls_failed);
  line ("Pages forgotten (their memory removed): ", count.forgotten);
  line ("  looks at the dynamic areas: ", count.looks);
  return NULL;
}
