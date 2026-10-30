/* t_elementary: VFPSupport_ElementaryFunctions, against correctly rounded
 * results (elementary_data.h, from tests/gen-elementary-tests.py) and RISC
 * OS Open's own edge cases (special inputs, with the results and the FPSCR
 * exception flags the C standard asks for).
 *
 * Sampled values may be 1 ulp out, as ROOL's own test bed allows
 * (QUIET1ULP); a NaN must give a NaN; infinities and zeros (with their
 * signs) must be exact. Two 2-ulp results are allowed: ROOL's own code
 * gives them (t_rool_elementary shows ours bit for bit the same as ROOL's
 * binary): log10 (single) near 2^-106, and fma (double) when x*y and z
 * nearly cancel. Edge cases must match exactly
 * (any NaN for a NaN, as ROOL's test bed does), with the flags of
 * interest as listed. Also checks the SWI's other results: 12 functions,
 * an error for r0 != 0, and Features 2 bit 2.
 *
 * Prints a line per function and precision, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#include "elementary_data.h"

#ifndef VFPSupport_ElementaryFunctions
#define VFPSupport_ElementaryFunctions 0x58ECA
#endif

typedef double (*f64_1) (double);
typedef double (*f64_2) (double, double);
typedef double (*f64_3) (double, double, double);
typedef float (*f32_1) (float);
typedef float (*f32_2) (float, float);
typedef float (*f32_3) (float, float, float);

static void *tab32, *tab64;

static double
d (uint64_t b)
{
  double x;
  memcpy (&x, &b, 8);
  return x;
}

static float
f (uint32_t b)
{
  float x;
  memcpy (&x, &b, 4);
  return x;
}

static uint64_t
bd (double x)
{
  uint64_t b;
  memcpy (&b, &x, 8);
  return b;
}

static uint32_t
bf (float x)
{
  uint32_t b;
  memcpy (&b, &x, 4);
  return b;
}

static unsigned
fpscr (void)
{
  unsigned v;
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (v));
  return v;
}

static void
set_fpscr (unsigned v)
{
  __asm__ volatile ("vmsr fpscr, %0" : : "r" (v));
}

/* Function FN of the table for PREC, as a pointer to its table entry (a
   branch instruction, so calling it calls the function). */
static void *
entry (int prec, int fn)
{
  return (char *) (prec == 64 ? tab64 : tab32) + 4 * fn;
}

static uint64_t
call (int prec, int fn, int nargs, const uint64_t *a)
{
  void *p = entry (prec, fn);
  if (prec == 64)
    switch (nargs)
      {
      case 1: return bd (((f64_1) p) (d (a[0])));
      case 2: return bd (((f64_2) p) (d (a[0]), d (a[1])));
      default: return bd (((f64_3) p) (d (a[0]), d (a[1]), d (a[2])));
      }
  switch (nargs)
    {
    case 1: return bf (((f32_1) p) (f (a[0])));
    case 2: return bf (((f32_2) p) (f (a[0]), f (a[1])));
    default: return bf (((f32_3) p) (f (a[0]), f (a[1]), f (a[2])));
    }
}

static int
is_nan (int prec, uint64_t b)
{
  return prec == 64 ? (b & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull
		    : (b & 0x7FFFFFFF) > 0x7F800000;
}

/* Distance in ulps between two values of the same sign (or zeros). */
static uint64_t
ulps (int prec, uint64_t a, uint64_t b)
{
  uint64_t s = prec == 64 ? 1ull << 63 : 1u << 31;
  int64_t x = (a & s) ? -(int64_t) (a & ~s) : (int64_t) a;
  int64_t y = (b & s) ? -(int64_t) (b & ~s) : (int64_t) b;
  return x > y ? (uint64_t) (x - y) : (uint64_t) (y - x);
}

int
main (void)
{
  int n, bad = 0;
  _kernel_oserror *e;

  int misc;
  if ((e = _swix (VFPSupport_Features, _IN (0) | _OUT (0), 2, &misc)) || !(misc & 4))
    {
      printf ("Features 2: elementary functions not listed (%x)\n", e ? 0 : misc);
      bad++;
    }
  if ((e = _swix (VFPSupport_ElementaryFunctions, _IN (0) | _OUTR (0, 2), 0, &n, &tab32, &tab64)))
    {
      printf ("ElementaryFunctions: %s\nFAIL\n", e->errmess);
      return 1;
    }
  if (n != F_COUNT)
    {
      printf ("ElementaryFunctions: %d functions, not %d\n", n, F_COUNT);
      bad++;
    }
  if (!_swix (VFPSupport_ElementaryFunctions, _IN (0), 1))
    {
      printf ("ElementaryFunctions 1: no error\n");
      bad++;
    }

  /* Sampled values */
  for (int fn = 0; fn < F_COUNT; fn++)
    for (int prec = 64; prec >= 32; prec -= 32)
      {
	unsigned count = 0, off = 0;
	uint64_t worst = 0;
	for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++)
	  {
	    const sample_t *s = &samples[i];
	    if (s->fn != fn || s->prec != prec)
	      continue;
	    count++;
	    uint64_t r = call (prec, fn, s->nargs, s->a);
	    uint64_t u;
	    uint64_t sign = prec == 64 ? 1ull << 63 : 1u << 31;
	    uint64_t inf = prec == 64 ? 0x7FF0000000000000ull : 0x7F800000u;
	    if (is_nan (prec, s->r) || is_nan (prec, r))
	      u = is_nan (prec, s->r) && is_nan (prec, r) ? 0 : ~0ull;
	    else if ((r & ~sign) == inf || (s->r & ~sign) == inf
		     || (r & ~sign) == 0 || (s->r & ~sign) == 0)
	      u = r == s->r ? 0 : ~0ull;	/* infinities, zeros: exactly */
	    else
	      u = (r ^ s->r) & sign ? ~0ull : ulps (prec, r, s->r);
	    if (u > worst)
	      worst = u;
	    if (u > ((fn == F_log10 && prec == 32) || (fn == F_fma && prec == 64) ? 2 : 1))
	      {
		if (off < 3)
		  printf ("  %s%d(%llx, %llx, %llx) = %llx, not %llx\n", fn_names[fn], prec,
			  s->a[0], s->a[1], s->a[2], r, s->r);
		off++;
	      }
	  }
	uint64_t allowed = (fn == F_log10 && prec == 32) || (fn == F_fma && prec == 64) ? 2 : 1;
	unsigned fails = 0;
	if (worst > allowed)
	  fails = off;
	printf ("%-5s %d: %u values, worst %s%llu ulp%s%s\n", fn_names[fn], prec, count,
		worst == ~0ull ? "NaN/sign " : "", worst == ~0ull ? 0 : worst,
		allowed > 1 && worst == allowed ? " (known in ROOL's code)" : "",
		fails ? " - FAIL" : "");
	bad += fails != 0;
      }

  /* ROOL's edge cases */
  unsigned edge_bad = 0;
  for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++)
    for (int prec = 64; prec >= 32; prec -= 32)
      {
	const edge_t *x = &edges[i];
	uint64_t a[3] = { prec == 64 ? x->in64[0] : x->in32[0],
			  prec == 64 ? x->in64[1] : x->in32[1], 0 };
	uint64_t want = prec == 64 ? x->out64 : x->out32;
	unsigned saved = fpscr ();
	set_fpscr (saved & ~0x9F);
	uint64_t r = call (prec, x->fn, x->nargs, a);
	unsigned flags = fpscr () & 0x9F;
	set_fpscr (saved);
	int ok = (is_nan (prec, r) && is_nan (prec, want)) || r == want;
	ok = ok && (flags & x->mask) == x->flags;
	if (!ok)
	  {
	    printf ("  edge %u: %s%d(%llx, %llx) = %llx flags %02x, not %llx flags %02x\n",
		    (unsigned) i, fn_names[x->fn], prec, a[0], a[1], r, flags & x->mask,
		    want, x->flags);
	    edge_bad++;
	  }
      }
  printf ("edge cases: %u, %u wrong\n", (unsigned) (2 * sizeof edges / sizeof edges[0]), edge_bad);
  bad += edge_bad != 0;

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad != 0;
}
