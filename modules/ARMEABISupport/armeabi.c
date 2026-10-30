/* ARMEABISupport (lite) for RISC OS for Linux.

   GCCSDK's ARMEABISupport can't run on the Linux port of RISC OS: it tells
   programs apart by the physical page at &8000 (the port has no physical
   pages, OS_Memory 0 fails) and it builds stacks and mmap areas from PMP
   dynamic areas (not implemented in the port). This module answers the
   same SWIs (chunk &59D00), as far as UnixLib uses them, with one ordinary
   dynamic area:

   - StackOp: Alloc, Free, GetStack, GetBounds, GetSizes. Stacks are
     committed in full when allocated (no growth on abort). Guard pages are
     reserved but not protected.
   - MMapOp: Map (anonymous, and file mappings that UnixLib fills itself),
     Unmap (whole or part of a block), Advise and Protect (accepted, no
     effect), GetInfo, Remap (in place, or moved with MREMAP_MAYMOVE).
   - Cleanup: frees what the ending program allocated.
   - MemoryOp: page allocators (as SOManager uses them), with the test
     bed's QEMU doing the page mapping; see below.
   - AbortOp: handlers for aborts in an allocator (called by QEMU);
     ReadError returns 0.
   - ShmOp: not supported (an error).

   Which program owns an allocation: a program starts when it allocates its
   one-page signal-handler stack (StackOp Alloc with 1 page and no guard,
   as UnixLib's start-up does), and ends at Cleanup. A call made while the
   caller's stack pointer (USR r13, read through the port's PSR HAL device)
   is in one of our stacks belongs to that stack's program: this tells
   apart desktop programs that start, switch and end in any order. Other
   calls (start-up, before UnixLib moves to its own stack) belong to the
   program started last.

   Memory isn't zeroed when it's mapped (UnixLib clears MAP_ANON maps
   itself), except the new part of a block mremap makes bigger.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
   SWI interface: GCCSDK's ARMEABISupport by Lee Noar (MIT). */

#include "cmodule.h"

#define PAGE		4096u
#define PAGE_SHIFT	12
#define NAME_LEN	32
#define MAX_NEST	32

#define MAP_FIXED	0x10
#define MREMAP_MAYMOVE	1

ERROR_BLOCK (err_bad_swi, 0x1E6, "Unknown ARMEABISupport SWI");
ERROR_BLOCK (err_bad_reason, 0x81DC20, "Bad ARMEABISupport reason code");
ERROR_BLOCK (err_bad_param, 0x81DC21, "Bad ARMEABISupport parameter");
ERROR_BLOCK (err_no_memory, 0x81DC22, "ARMEABISupport is out of memory");
ERROR_BLOCK (err_in_use, 0x81DC24, "ARMEABISupport is in use");
ERROR_BLOCK (err_einval, 0x81DC28, "Invalid argument");
ERROR_BLOCK (err_enomem, 0x81DC2D, "Not enough memory");

enum kind { KIND_STACK = 1, KIND_MMAP = 2 };

typedef struct block
{
  struct block *next;
  unsigned first;		/* first page in the dynamic area */
  unsigned pages;		/* including guard pages */
  unsigned owner;		/* program id */
  enum kind kind;
  unsigned guard;		/* stacks: guard pages at the bottom */
  int is_main;			/* a program's main stack */
  int prot, flags, fd;		/* mmaps: as given to Map */
  unsigned offset;
  char name[NAME_LEN];		/* stacks */
} block;

static struct
{
  int da;			/* dynamic area number, 0 = none yet */
  unsigned char *base;
  unsigned max_pages;
  unsigned committed;		/* pages */
  unsigned *bitmap;		/* 1 = page in use */
  block *blocks;
  unsigned current;		/* id of the program started last, 0 = none */
  void *pv;			/* the port's PSR device, or NULL */
  unsigned freed_lo, freed_hi;	/* the stack freed last, */
  unsigned freed_owner;		/* and its program */
  unsigned next_id;
  unsigned nest[MAX_NEST];
  int depth;
  unsigned overflow;		/* programs started past MAX_NEST */
} g;

static const char da_name[] = "ARMEABISupport (lite)";

/* --- the dynamic area and its pages ------------------------------------ */

static oserror *
da_create (void)
{
  static const unsigned sizes[] = { 512u << 20, 256u << 20, 128u << 20, 64u << 20 };
  oserror *e = ERR (err_no_memory);
  for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
      regs r = {{ 0, (unsigned) -1, 0, (unsigned) -1, 0x80, sizes[i], 0, 0,
		  (unsigned) da_name }};
      e = swix_regs (OS_DynamicArea, &r);
      if (e)
	continue;
      g.da = (int) r.r[1];
      g.base = (unsigned char *) r.r[3];
      g.max_pages = sizes[i] >> PAGE_SHIFT;
      g.committed = 0;
      g.bitmap = rma_claim (g.max_pages / 8);
      if (!g.bitmap)
	{
	  regs k = {{ 1, (unsigned) g.da }};
	  swix_regs (OS_DynamicArea, &k);
	  g.da = 0;
	  return ERR (err_no_memory);
	}
      memset (g.bitmap, 0, g.max_pages / 8);
      return NULL;
    }
  return e;
}

static void
da_remove (void)
{
  if (g.da)
    {
      regs r = {{ 1, (unsigned) g.da }};
      swix_regs (OS_DynamicArea, &r);
      g.da = 0;
    }
  rma_free (g.bitmap);
  g.bitmap = NULL;
}

static int
page_used (unsigned p)
{
  return (g.bitmap[p >> 5] >> (p & 31)) & 1;
}

static void
mark (unsigned first, unsigned n, int used)
{
  for (unsigned p = first; p < first + n; p++)
    if (used)
      g.bitmap[p >> 5] |= 1u << (p & 31);
    else
      g.bitmap[p >> 5] &= ~(1u << (p & 31));
}

/* Makes the area at least END pages long. */
static oserror *
commit (unsigned end)
{
  if (end <= g.committed)
    return NULL;
  regs r = {{ (unsigned) g.da, (end - g.committed) << PAGE_SHIFT }};
  oserror *e = swix_regs (OS_ChangeDynamicArea, &r);
  if (e)
    return e;
  g.committed = end;
  return NULL;
}

/* Gives back the pages above the highest one in use. */
static void
trim (void)
{
  unsigned top = g.committed;
  while (top > 0 && !page_used (top - 1))
    top--;
  if (top < g.committed)
    {
      regs r = {{ (unsigned) g.da, (unsigned) -(int) ((g.committed - top) << PAGE_SHIFT) }};
      if (!swix_regs (OS_ChangeDynamicArea, &r))
	g.committed -= r.r[1] >> PAGE_SHIFT;
    }
}

/* Finds N free pages (first fit), marks them and commits them. */
static oserror *
pages_alloc (unsigned n, unsigned *first_ret)
{
  oserror *e;
  if (n == 0)
    return ERR (err_bad_param);
  if (!g.da && (e = da_create ()) != NULL)
    return e;
  unsigned run = 0;
  for (unsigned p = 0; p < g.max_pages; p++)
    {
      if ((p & 31) == 0 && g.bitmap[p >> 5] == 0xFFFFFFFFu)
	{
	  run = 0;
	  p += 31;
	  continue;
	}
      if (page_used (p))
	{
	  run = 0;
	  continue;
	}
      if (++run == n)
	{
	  unsigned first = p + 1 - n;
	  if ((e = commit (first + n)) != NULL)
	    return ERR (err_no_memory);
	  mark (first, n, 1);
	  *first_ret = first;
	  return NULL;
	}
    }
  return ERR (err_no_memory);
}

static void
pages_free (unsigned first, unsigned n)
{
  mark (first, n, 0);
  trim ();
}

static unsigned char *
page_addr (unsigned p)
{
  return g.base + (p << PAGE_SHIFT);
}

/* --- the caller ------------------------------------------------------------ */

/* The port's HAL device for PSR operations (type &30A, ID 1); its routine
   at +124 stores the USR-mode r13 and r14. */
#define PV_DEVICE_TYPE	0x30A
#define PV_DEVICE_ID	1
#define PV_STOREUSR	124
extern void pv_store_usr (void *routine, unsigned *out);

static void
find_pv (void)
{
  g.pv = NULL;
  for (unsigned i = 0;; )
    {
      regs r = {{ PV_DEVICE_TYPE, i, 0, 0, 0, 0, 0, 0, 5 }};
      if (swix_regs (OS_Hardware, &r) || (int) r.r[1] == -1)
	return;
      const unsigned short *d = (const unsigned short *) r.r[2];
      /* +0 type, +2 ID, +4 location, +8 API version (major in the top half) */
      if (d[1] == PV_DEVICE_ID && ((const unsigned *) d)[2] >> 16 == 0)
	{
	  g.pv = (void *) r.r[2];
	  return;
	}
      i = r.r[1];
    }
}

/* The caller's USR stack pointer, or 0 if it can't be read. */
static unsigned
caller_sp (void)
{
  if (!g.pv)
    return 0;
  unsigned out[2] = { 0, 0 };
  pv_store_usr (*(void **) ((char *) g.pv + PV_STOREUSR), out);
  return out[0];
}

/* --- blocks -------------------------------------------------------------- */

static unsigned caller_program (void);

static block *
block_new (enum kind kind, unsigned first, unsigned pages)
{
  block *b = rma_claim (sizeof *b);
  if (!b)
    return NULL;
  memset (b, 0, sizeof *b);
  b->kind = kind;
  b->first = first;
  b->pages = pages;
  b->owner = caller_program ();
  b->fd = -1;
  b->next = g.blocks;
  g.blocks = b;
  return b;
}

static void
block_unlink (block *b)
{
  for (block **pp = &g.blocks; *pp; pp = &(*pp)->next)
    if (*pp == b)
      {
	*pp = b->next;
	return;
      }
}

static void
block_free (block *b)
{
  block_unlink (b);
  pages_free (b->first, b->pages);
  rma_free (b);
}

/* The block of KIND (0 = any) containing ADDR. */
static block *
block_at (const void *addr, enum kind kind)
{
  const unsigned char *a = addr;
  if (!g.da || a < g.base || a >= g.base + (g.max_pages << PAGE_SHIFT))
    return NULL;
  unsigned p = (unsigned) (a - g.base) >> PAGE_SHIFT;
  for (block *b = g.blocks; b; b = b->next)
    if ((!kind || b->kind == kind) && p >= b->first && p < b->first + b->pages)
      return b;
  return NULL;
}

/* The program a call belongs to: the owner of the stack the caller is
   running on, or else the program started last. */
static unsigned
caller_program (void)
{
  unsigned sp = caller_sp ();
  if (sp)
    {
      block *b = block_at ((const void *) (sp - 4), KIND_STACK);
      if (b && b->owner)
	return b->owner;
      if (sp - 4 >= g.freed_lo && sp - 4 < g.freed_hi && g.freed_owner)
	return g.freed_owner;
    }
  return g.current;
}

static int
is_block (block *x)
{
  for (block *b = g.blocks; b; b = b->next)
    if (b == x)
      return 1;
  return 0;
}

/* --- programs ------------------------------------------------------------ */

/* A program starts. Past MAX_NEST levels, the new program is counted but
   shares its parent's id, and its Cleanup frees nothing (safe, but its
   memory stays until its parent's Cleanup). */
static void
program_start (void)
{
  if (g.depth < MAX_NEST)
    {
      g.nest[g.depth++] = g.current;
      g.current = ++g.next_id;
    }
  else
    g.overflow++;
}

/* Program ID has ended: free what it allocated and forget it. */
static void
program_end (unsigned id)
{
  block *b = g.blocks;
  while (b)
    {
      block *next = b->next;
      if (b->owner == id)
	block_free (b);
      b = next;
    }
  /* Forget it: usually the last one started, but desktop programs can end
     in any order. */
  if (id == g.current)
    g.current = g.depth > 0 ? g.nest[--g.depth] : 0;
  else
    for (int i = 0; i < g.depth; i++)
      if (g.nest[i] == id)
	{
	  for (int j = i; j < g.depth - 1; j++)
	    g.nest[j] = g.nest[j + 1];
	  g.depth--;
	  break;
	}
}

static oserror *
cleanup (void)
{
  if (g.overflow)
    {
      g.overflow--;
      return NULL;
    }
  program_end (caller_program ());
  g.freed_lo = g.freed_hi = g.freed_owner = 0;
  return NULL;
}

/* --- StackOp ------------------------------------------------------------- */

static oserror *
stack_op (regs *r)
{
  block *b;
  oserror *e;
  switch (r->r[0])
    {
    case 0:			/* Alloc: r1 pages, r2 guard pages, r3 name */
      {
	unsigned pages = r->r[1], guard = r->r[2], first;
	const char *name = (const char *) r->r[3];
	if (pages == 0)
	  return ERR (err_bad_param);
	if ((e = pages_alloc (pages + guard, &first)) != NULL)
	  {
	    r->r[1] = r->r[2] = 0;
	    return e;
	  }
	if (!(b = block_new (KIND_STACK, first, pages + guard)))
	  {
	    pages_free (first, pages + guard);
	    r->r[1] = r->r[2] = 0;
	    return ERR (err_no_memory);
	  }
	/* UnixLib's start-up allocates its signal-handler stack (one page,
	   no guard) first: a new program, which owns this stack. Only once
	   it's allocated, so a failure leaves the running program as it was
	   (UnixLib makes no Cleanup call then). */
	if (pages == 1 && guard == 0)
	  {
	    program_start ();
	    b->owner = g.current;
	  }
	/* A program's main stack: allocated while the caller still runs on
	   the stack it started with, not on one of ours (thread stacks are
	   allocated from the main stack or another thread's). */
	else
	  {
	    unsigned sp = caller_sp ();
	    b->is_main = sp && !block_at ((const void *) (sp - 4), KIND_STACK);
	  }
	b->guard = guard;
	if (!name)
	  name = "<unnamed>";
	unsigned i;
	for (i = 0; i < NAME_LEN - 1 && name[i]; i++)
	  b->name[i] = name[i];
	b->name[i] = '\0';
	r->r[0] = (unsigned) b;
	r->r[1] = (unsigned) page_addr (first + guard);
	r->r[2] = (unsigned) page_addr (first + pages + guard);
	return NULL;
      }
    case 1:			/* Free: r1 handle */
      b = (block *) r->r[1];
      if (!is_block (b) || b->kind != KIND_STACK)
	return ERR (err_bad_param);
      /* SharedUnixLibrary's exit code frees the program's main stack while
	 still running on it, and then calls Cleanup - but only for a
	 program with no parent: programs started by system() don't. So
	 freeing your own main stack ends your program; remember whose
	 stack it was for the Cleanup call that may follow. */
      {
	unsigned sp = caller_sp (), lo = (unsigned) page_addr (b->first);
	unsigned hi = (unsigned) page_addr (b->first + b->pages);
	int ending = b->is_main && sp - 4 >= lo && sp - 4 < hi;
	unsigned owner = b->owner;
	block_free (b);
	if (ending)
	  {
	    g.freed_lo = lo;
	    g.freed_hi = hi;
	    g.freed_owner = owner;
	    program_end (owner);
	  }
      }
      return NULL;
    case 2:			/* GetStack: r1 address in it -> r1 handle */
      b = r->r[1] ? block_at ((void *) r->r[1], KIND_STACK) : NULL;
      r->r[1] = (unsigned) b;
      return b ? NULL : ERR (err_bad_param);
    case 3:			/* GetBounds: r1 handle -> r1 base, r2 top */
    case 4:			/* GetSizes: r1 handle -> r1 size, r2 guard size */
      b = (block *) r->r[1];
      if (!is_block (b) || b->kind != KIND_STACK)
	{
	  r->r[1] = r->r[2] = 0;
	  return ERR (err_bad_param);
	}
      if (r->r[0] == 3)
	{
	  r->r[1] = (unsigned) page_addr (b->first + b->guard);
	  r->r[2] = (unsigned) page_addr (b->first + b->pages);
	}
      else
	{
	  r->r[1] = (b->pages - b->guard) << PAGE_SHIFT;
	  r->r[2] = b->guard << PAGE_SHIFT;
	}
      return NULL;
    default:
      return ERR (err_bad_reason);
    }
}

/* --- MMapOp -------------------------------------------------------------- */

static unsigned
bytes_to_pages (unsigned n)
{
  return (n + PAGE - 1) >> PAGE_SHIFT;
}

/* Unmaps [addr, addr + len) inside block B, splitting it if need be. */
static oserror *
unmap_range (block *b, unsigned char *addr, unsigned len)
{
  if ((unsigned) addr & (PAGE - 1))
    return ERR (err_einval);
  unsigned p = (unsigned) (addr - g.base) >> PAGE_SHIFT;
  unsigned n = bytes_to_pages (len);
  unsigned end = b->first + b->pages;
  if (n == 0 || p + n > end)
    return ERR (err_einval);
  if (p == b->first && n == b->pages)
    {
      block_free (b);
      return NULL;
    }
  if (p + n < end && p > b->first)
    {
      /* A hole in the middle: the tail becomes a block of its own. */
      block *t = block_new (KIND_MMAP, p + n, end - (p + n));
      if (!t)
	return ERR (err_enomem);
      t->owner = b->owner;
      t->prot = b->prot;
      t->flags = b->flags;
      t->fd = b->fd;
      t->offset = b->offset + ((p + n - b->first) << PAGE_SHIFT);
      b->pages = p - b->first;
    }
  else if (p == b->first)
    {
      b->offset += n << PAGE_SHIFT;
      b->first += n;
      b->pages -= n;
    }
  else
    b->pages -= n;
  pages_free (p, n);
  return NULL;
}

static oserror *
remap (regs *r)
{
  unsigned char *addr = (unsigned char *) r->r[1];
  unsigned new_pages = bytes_to_pages (r->r[3]);
  block *b = block_at (addr, KIND_MMAP);
  oserror *e;
  if (!b || addr != page_addr (b->first) || new_pages == 0)
    return ERR (err_einval);
  if (new_pages <= b->pages)
    {
      if (new_pages < b->pages)
	{
	  unsigned cut = b->pages - new_pages;
	  b->pages = new_pages;
	  pages_free (b->first + new_pages, cut);
	}
      r->r[0] = (unsigned) addr;
      return NULL;
    }
  /* Grow in place if the pages after it are free. */
  unsigned end = b->first + b->pages, want = b->first + new_pages, p;
  for (p = end; p < want && p < g.max_pages && !page_used (p); p++)
    ;
  if (p == want && commit (want) == NULL)
    {
      mark (end, want - end, 1);
      /* Linux gives zeros in the new part; freed pages here keep their
	 old contents. */
      memset (page_addr (end), 0, (want - end) << PAGE_SHIFT);
      b->pages = new_pages;
      r->r[0] = (unsigned) addr;
      return NULL;
    }
  if (!(r->r[4] & MREMAP_MAYMOVE))
    return ERR (err_enomem);
  unsigned first;
  if ((e = pages_alloc (new_pages, &first)) != NULL)
    return ERR (err_enomem);
  memcpy (page_addr (first), addr, b->pages << PAGE_SHIFT);
  memset (page_addr (first + b->pages), 0, (new_pages - b->pages) << PAGE_SHIFT);
  mark (b->first, b->pages, 0);
  b->first = first;
  b->pages = new_pages;
  trim ();
  r->r[0] = (unsigned) page_addr (first);
  return NULL;
}

static oserror *
mmap_op (regs *r)
{
  block *b;
  oserror *e;
  switch (r->r[0])
    {
    case 0:			/* Map: r1 addr, r2 len, r3 prot, r4 flags, r5 fd, r6 offset */
      {
	unsigned len = r->r[2], first;
	if (len == 0)
	  return ERR (err_einval);
	if (r->r[1])
	  {
	    /* Only for an existing block (to change its protection). */
	    b = block_at ((void *) r->r[1], KIND_MMAP);
	    if (!b)
	      return ERR (err_enomem);
	    if ((void *) r->r[1] == page_addr (b->first)
		&& bytes_to_pages (len) == b->pages)
	      b->prot = (int) r->r[3];
	    r->r[0] = (unsigned) page_addr (b->first);
	    return NULL;
	  }
	if (r->r[4] & MAP_FIXED)
	  return ERR (err_einval);
	if ((e = pages_alloc (bytes_to_pages (len), &first)) != NULL)
	  return ERR (err_enomem);
	if (!(b = block_new (KIND_MMAP, first, bytes_to_pages (len))))
	  {
	    pages_free (first, bytes_to_pages (len));
	    return ERR (err_enomem);
	  }
	b->prot = (int) r->r[3];
	b->flags = (int) r->r[4];
	b->fd = (int) r->r[5];
	b->offset = r->r[6];
	r->r[0] = (unsigned) page_addr (first);
	return NULL;
      }
    case 1:			/* Unmap: r1 addr, r2 len */
      b = block_at ((void *) r->r[1], KIND_MMAP);
      if (!b)
	return ERR (err_einval);
      return unmap_range (b, (unsigned char *) r->r[1], r->r[2]);
    case 2:			/* Advise */
    case 3:			/* Protect */
      return block_at ((void *) r->r[1], KIND_MMAP) ? NULL : ERR (err_enomem);
    case 4:			/* GetInfo: r1 addr -> r0 start, r1 len, r2 prot, r3 flags, r4 fd, r5 offset */
      b = block_at ((void *) r->r[1], KIND_MMAP);
      if (!b)
	return ERR (err_einval);
      r->r[0] = (unsigned) page_addr (b->first);
      r->r[1] = b->pages << PAGE_SHIFT;
      r->r[2] = (unsigned) b->prot;
      r->r[3] = (unsigned) b->flags;
      r->r[4] = (unsigned) b->fd;
      r->r[5] = b->offset;
      return NULL;
    case 5:			/* Remap: r1 addr, r2 old len, r3 new len, r4 flags */
      return remap (r);
    case 6:			/* GetProtection: r1 addr -> r0 prot */
      b = block_at ((void *) r->r[1], KIND_MMAP);
      if (!b)
	return ERR (err_einval);
      r->r[0] = (unsigned) b->prot;
      return NULL;
    default:
      return ERR (err_bad_reason);
    }
}

/* --- module entry points --------------------------------------------------- */

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  memset (&g, 0, sizeof g);
  find_pv ();
  return NULL;
}

static int allocators_free (int fatal);

oserror *
mod_final (int fatal)
{
  if ((g.blocks && !fatal) || allocators_free (fatal))
    return ERR (err_in_use);
  while (g.blocks)
    {
      block *b = g.blocks;
      g.blocks = b->next;
      rma_free (b);
    }
  da_remove ();
  return NULL;
}

/* "Not supported" errors say which SWI and reason, to see what a program
   needs that this module doesn't do. */
static oserror unsupported_err;

static oserror *
unsupported (unsigned number, unsigned reason)
{
  static const char text[] = "Not supported by ARMEABISupport (lite): SWI &59D0";
  char *d = unsupported_err.errmess;
  unsupported_err.errnum = 0x81DC2E;
  for (const char *t = text; *t; t++)
    *d++ = *t;
  *d++ = "0123456789ABCDEF"[number & 15];
  for (const char *t = " reason "; *t; t++)
    *d++ = *t;
  char digits[12];
  int n = 0;
  do
    digits[n++] = '0' + reason % 10;
  while ((reason /= 10) && n < 10);
  while (n)
    *d++ = digits[--n];
  *d = 0;
  return &unsupported_err;
}

/* --- MemoryOp and AbortOp: page allocators --------------------------------

   SOManager (the shared library loader) keeps libraries' data in a page
   allocator and gives each task its own copy of a library's data by moving
   "physical" pages around with OS_DynamicArea 22, mapping a task's copy in
   from a data abort handler when the task touches it. The port has neither
   physical pages nor PMP dynamic areas, so the test bed's QEMU does that
   part (SWI &C0002, see qemu-arm-riscos): an allocator is an ordinary
   dynamic area, of size 0 but reserving its address space, whose pages QEMU
   backs with a memory file. Page n of the area starts out as file page n.
   A QEMU without that support gives an error (an unknown SWI) and the
   allocator isn't made. */

#define ALLOC_MAGIC	0x434c4c41u	/* "ALLC" */

typedef struct ablock
{
  struct ablock *next;
  unsigned first, pages;
} ablock;

typedef struct allocator
{
  unsigned magic;
  struct allocator *next;
  int da;
  unsigned char *base;
  unsigned pages;
  unsigned flags;
  ablock *blocks;		/* sorted by first page */
} allocator;

static allocator *allocators;

/* SWI &C0002 (X form), see QEMU: returns 0, -errno, or an error pointer
   (a QEMU without it: the SWI goes to RISC OS, which doesn't know it). */
static int
host_pmp (unsigned op, unsigned a, unsigned b, unsigned c, unsigned d)
{
  register unsigned r0 __asm__ ("r0") = op;
  register unsigned r1 __asm__ ("r1") = a;
  register unsigned r2 __asm__ ("r2") = b;
  register unsigned r3 __asm__ ("r3") = c;
  register unsigned r4 __asm__ ("r4") = d;
  __asm__ volatile ("swi 0xE0002\n\tmovvs r0, #-1"
		    : "+r" (r0) : "r" (r1), "r" (r2), "r" (r3), "r" (r4)
		    : "lr", "cc", "memory");
  return (int) r0 > -4096 && (int) r0 <= 0 ? (int) r0 : -1;
}

__asm__ (".text\n"
	 "\t.global abort_return\n"
	 "\t.type abort_return, %function\n"
	 "abort_return:\n"
	 "\tswi 0xE0003\n");
extern char abort_return[];
static unsigned abort_regs[17];

static allocator *
alloc_valid (unsigned h)
{
  for (allocator *a = allocators; a; a = a->next)
    if ((unsigned) a == h && a->magic == ALLOC_MAGIC)
      return a;
  return NULL;
}

static allocator *
alloc_at (unsigned addr)
{
  for (allocator *a = allocators; a; a = a->next)
    if (addr >= (unsigned) a->base
	&& addr - (unsigned) a->base < a->pages << PAGE_SHIFT)
      return a;
  return NULL;
}

/* Pages [addr, addr + n pages) of a, or -1. */
static int
alloc_page (allocator *a, unsigned addr, unsigned n)
{
  unsigned off = addr - (unsigned) a->base;
  if (addr < (unsigned) a->base || (off & (PAGE - 1))
      || (off >> PAGE_SHIFT) >= a->pages
      || n > a->pages - (off >> PAGE_SHIFT))
    return -1;
  return (int) (off >> PAGE_SHIFT);
}

static oserror *
pmp_err (int e)
{
  return e == -12 ? ERR (err_enomem) : e ? ERR (err_bad_param) : NULL;
}

static oserror *
alloc_new (const char *name, unsigned max, unsigned flags, unsigned *handle)
{
  unsigned pages = (max + PAGE - 1) >> PAGE_SHIFT;
  if (!pages || pages > (1u << 20) - 1)
    return ERR (err_bad_param);
  allocator *a = rma_claim (sizeof *a);
  if (!a)
    return ERR (err_no_memory);
  regs r = {{ 0, (unsigned) -1, 0, (unsigned) -1, 0x80, pages << PAGE_SHIFT,
	      0, 0, (unsigned) name }};
  oserror *e = swix_regs (OS_DynamicArea, &r);
  if (e)
    {
      rma_free (a);
      return e;
    }
  a->magic = ALLOC_MAGIC;
  a->da = (int) r.r[1];
  a->base = (unsigned char *) r.r[3];
  a->pages = pages;
  a->flags = flags;
  a->blocks = NULL;
  if (host_pmp (0, (unsigned) a->da, (unsigned) a->base, pages, 0))
    {
      regs k = {{ 1, (unsigned) a->da }};
      swix_regs (OS_DynamicArea, &k);
      rma_free (a);
      return unsupported (0, 0);
    }
  a->next = allocators;
  allocators = a;
  *handle = (unsigned) a;
  return NULL;
}

static oserror *
alloc_destroy (allocator *a)
{
  host_pmp (1, (unsigned) a->da, 0, 0, 0);
  regs k = {{ 1, (unsigned) a->da }};
  swix_regs (OS_DynamicArea, &k);
  while (a->blocks)
    {
      ablock *b = a->blocks;
      a->blocks = b->next;
      rma_free (b);
    }
  for (allocator **p = &allocators; *p; p = &(*p)->next)
    if (*p == a)
      {
	*p = a->next;
	break;
      }
  a->magic = 0;
  rma_free (a);
  return NULL;
}

/* Alloc: n pages, flags bits 16-17: 0 leave unmapped, 1 claim, 2 claim and
   map (claiming is nothing here: the memory file has every page). */
static oserror *
alloc_alloc (allocator *a, unsigned n, unsigned flags, unsigned *addr)
{
  if ((a->flags & 15) != 1)	/* only page allocators */
    return unsupported (0, 2);
  if (!n || n > a->pages)
    return ERR (err_bad_param);
  unsigned at = 0;
  ablock **p = &a->blocks;
  for (; *p; p = &(*p)->next)
    {
      if ((*p)->first - at >= n)
	break;
      at = (*p)->first + (*p)->pages;
    }
  if (n > a->pages - at)
    return ERR (err_enomem);
  ablock *b = rma_claim (sizeof *b);
  if (!b)
    return ERR (err_no_memory);
  b->first = at;
  b->pages = n;
  b->next = *p;
  *p = b;
  if (((flags >> 16) & 3) == 2)
    {
      int e = host_pmp (2, (unsigned) a->da, at, at, n);
      if (e)
	{
	  *p = b->next;
	  rma_free (b);
	  return pmp_err (e);
	}
    }
  *addr = (unsigned) a->base + (at << PAGE_SHIFT);
  return NULL;
}

static oserror *
alloc_free (allocator *a, unsigned addr)
{
  int pg = alloc_page (a, addr, 1);
  for (ablock **p = &a->blocks; pg >= 0 && *p; p = &(*p)->next)
    if ((*p)->first == (unsigned) pg)
      {
	ablock *b = *p;
	host_pmp (3, (unsigned) a->da, b->first, b->pages, 0);
	host_pmp (4, (unsigned) a->da, b->first, b->pages, 0);
	*p = b->next;
	rma_free (b);
	return NULL;
      }
  return ERR (err_bad_param);
}

static oserror *
memory_op (regs *r)
{
  allocator *a = NULL;
  int pg;
  unsigned reason = r->r[0];

  if (reason >= 1 && reason <= 9)
    {
      a = alloc_valid (r->r[1]);
      if (!a)
	return ERR (err_bad_param);
    }
  switch (reason)
    {
    case 0:			/* new: r1 name, r2 max size, r3 flags */
      return alloc_new ((const char *) r->r[1], r->r[2], r->r[3], &r->r[0]);
    case 1:			/* destroy */
      r->r[0] = 0;
      return alloc_destroy (a);
    case 2:			/* alloc: r2 pages, r3 flags */
      return alloc_alloc (a, r->r[2], r->r[3], &r->r[0]);
    case 3:			/* claim pages: r2 address, r3 count */
    case 4:			/* release pages */
    case 5:			/* map pages, r4 access flags */
    case 6:			/* unmap pages */
      pg = alloc_page (a, r->r[2], r->r[3]);
      if (pg < 0 || !r->r[3])
	return ERR (err_bad_param);
      {
	int e = 0;
	if (reason == 4)
	  {
	    e = host_pmp (3, (unsigned) a->da, (unsigned) pg, r->r[3], 0);
	    if (!e)
	      e = host_pmp (4, (unsigned) a->da, (unsigned) pg, r->r[3], 0);
	  }
	else if (reason == 5)
	  e = host_pmp (2, (unsigned) a->da, (unsigned) pg, (unsigned) pg,
			r->r[3]);
	else if (reason == 6)
	  e = host_pmp (3, (unsigned) a->da, (unsigned) pg, r->r[3], 0);
	r->r[0] = 0;
	return pmp_err (e);
      }
    case 7:			/* free: r2 address */
      r->r[0] = 0;
      return alloc_free (a, r->r[2]);
    case 8:			/* info -> r0 DA, r1 base, r2 end */
      r->r[0] = (unsigned) a->da;
      r->r[1] = (unsigned) a->base;
      r->r[2] = (unsigned) a->base + (a->pages << PAGE_SHIFT);
      return NULL;
    case 9:			/* page number of r2 */
      pg = alloc_page (a, r->r[2] & ~(PAGE - 1), 1);
      if (pg < 0)
	return ERR (err_bad_param);
      r->r[0] = (unsigned) pg;
      return NULL;
    case 10:			/* allocator from address r1, 0 if none */
      if (!r->r[1])
	return ERR (err_bad_param);
      r->r[0] = (unsigned) alloc_at (r->r[1]);
      return NULL;
    case 11:			/* allocator from dynamic area r1, 0 if none */
      r->r[0] = 0;
      for (allocator *b = allocators; b; b = b->next)
	if (b->da == (int) r->r[1])
	  r->r[0] = (unsigned) b;
      return NULL;
    case 12:			/* dump page mappings: nothing to show */
    case 13:			/* dump allocator */
      return NULL;
    default:
      return ERR (err_bad_reason);
    }
}

/* Module finalisation: in use while there are allocators (unless fatal). */
static int
allocators_free (int fatal)
{
  if (allocators && !fatal)
    return 1;
  while (allocators)
    alloc_destroy (allocators);
  return 0;
}

/* AbortOp: 0 register handler (r1 routine, r2 its r12), 1 deregister,
   2 install, 3 remove (nothing to do: QEMU calls the handlers), 4 read
   error status (0). Handlers are only called for aborts in an allocator. */
static oserror *
abort_op (regs *r)
{
  int e;
  switch (r->r[0])
    {
    case 0:
      e = host_pmp (5, r->r[1], r->r[2], (unsigned) abort_regs,
		    (unsigned) abort_return);
      if (e == -1)
	return unsupported (1, 0);
      r->r[0] = 0;
      return pmp_err (e);
    case 1:
      host_pmp (6, r->r[1], r->r[2], 0, 0);
      r->r[0] = 0;
      return NULL;
    case 2:
    case 3:
    case 4:
      r->r[0] = 0;
      return NULL;
    default:
      return ERR (err_bad_reason);
    }
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 0:
      return memory_op (r);
    case 1:
      return abort_op (r);
    case 2:
      return stack_op (r);
    case 3:
      return cleanup ();
    case 4:
      return mmap_op (r);
    case 5:			/* ShmOp */
      return unsupported (number, r->r[0]);
    default:
      return ERR (err_bad_swi);
    }
}

void
mod_service (regs *r)
{
  (void) r;
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  write0 ("Dynamic area ");
  write_dec ((unsigned) g.da);
  write0 (" at &");
  write_hex8 ((unsigned) g.base);
  write0 (", ");
  write_dec (g.committed);
  write0 (" of ");
  write_dec (g.max_pages);
  write0 (" pages in use");
  newline ();
  write0 ("Running program ");
  write_dec (g.current);
  write0 (", nested ");
  write_dec ((unsigned) g.depth);
  write0 (g.pv ? ", callers told apart by stack" : ", no PSR device");
  newline ();
  for (block *b = g.blocks; b; b = b->next)
    {
      write0 (b->kind == KIND_STACK ? "  stack &" : "  mmap  &");
      write_hex8 ((unsigned) page_addr (b->first));
      write0 (" ");
      write_dec (b->pages);
      write0 (" pages, program ");
      write_dec (b->owner);
      if (b->kind == KIND_STACK)
	{
	  write0 (" ");
	  write0 (b->name);
	}
      newline ();
    }
  return NULL;
}
