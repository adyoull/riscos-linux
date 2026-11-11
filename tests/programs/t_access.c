/* OS_Memory 24 (CheckMemoryAccess) and OS_ValidateAddress in a page
   allocator (ARMEABISupport_MemoryOp, as SOManager keeps shared libraries
   in): its pages are mapped from a memory file that the kernel's page
   tables don't know about. As on RISC OS, a mapped page must read as
   readable and writable, an unmapped one as not there (EDT's handoff,
   7 Nov 2026: EDT couldn't show library code). */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <kernel.h>
#include <stdio.h>
#include <swis.h>

#define MemoryOp 0x59D00
static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static unsigned
access24 (unsigned lo, unsigned hi)
{
  unsigned f = 0;
  _swix (OS_Memory, _INR (0, 2) | _OUT (1), 24, lo, hi, &f);
  return f;
}

/* 1 if OS_ValidateAddress says lo..hi is valid (C clear) */
static int
valid (unsigned lo, unsigned hi)
{
  unsigned psr = 0;
  _swix (OS_ValidateAddress, _INR (0, 1) | _OUT (_FLAGS), lo, hi, &psr);
  return !(psr & _C);
}

int
main (void)
{
  unsigned h = 0, a = 0;
  _kernel_oserror *e = _swix (MemoryOp, _INR (0, 3) | _OUT (0), 0, "t_access", 16 * 4096, 1, &h);
  if (e)
    {
      printf ("no allocator: %s\n", e->errmess);
      return 1;
    }
  /* 4 pages, claimed and mapped */
  e = _swix (MemoryOp, _INR (0, 3) | _OUT (0), 2, h, 4, 2 << 16, &a);
  CHECK (!e);
  *(volatile unsigned *) a = 1;
  unsigned f = access24 (a, a + 4);
  printf ("mapped %08x: %x\n", a, f);
  CHECK ((f & 0xf) == 0xf);		/* completely readable, writable */
  CHECK ((f & 0xff000000) == 0);	/* reserved bits clear */
  CHECK (valid (a, a + 4));
  CHECK (!_swix (MemoryOp, _INR (0, 3), 6, h, a + 4096, 1));	/* unmap page 1 */
  f = access24 (a + 4096, a + 4100);
  printf ("unmapped %08x: %x\n", a + 4096, f);
  CHECK ((f & 0xff) == 0);		/* no access */
  CHECK ((f & 0xc00) == 0xc00);		/* not executable */
  CHECK (!valid (a + 4096, a + 4100));
  f = access24 (a, a + 3 * 4096);	/* pages 0-2: partly mapped */
  printf ("pages 0-2: %x\n", f);
  CHECK ((f & 0xf) == 0 && (f & 0xf0) == 0xf0);
  f = access24 (a + 8 * 4096, a + 8 * 4096 + 4);	/* never allocated */
  printf ("unallocated %08x: %x\n", a + 8 * 4096, f);
  CHECK ((f & 0xff) == 0);
  _swix (MemoryOp, _INR (0, 2), 7, h, a);
  _swix (MemoryOp, _INR (0, 1), 1, h);
  if (fails)
    printf ("%d FAILED\n", fails);
  else
    printf ("PASS\n");
  return fails != 0;
}
