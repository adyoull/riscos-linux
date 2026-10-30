/* t_rool_elementary ROOL_MODULE: our VFPSupport's elementary functions
 * against RISC OS Open's own, in ROOL's VFPSupport 0.18 as built for the
 * Raspberry Pi ROM (basicvfp/fetch.sh takes it out of the ROM image).
 *
 * The module isn't started (it can't run on the port: it needs privileged
 * registers); its file is loaded as data, and its ElementaryFunctions
 * branch table (24 "B" instructions: 12 single precision, 12 double) is
 * called directly. ROOL's code is position-independent, and its only
 * outside call, RaiseException, just sets FPSCR flags unless a trap is
 * enabled (none can be here).
 *
 * For every input in elementary_data.h (sampled values and ROOL's edge
 * cases), both must give the same bits and the same FPSCR flags. The Pi
 * ROM is built for every Pi from the Pi 1 on, so it does FMA in software:
 * that's our "v3" build, which VFPSupport uses on a VFPv3 CPU (run this
 * with QEMU_CPU=cortex-a8). On a VFPv4 CPU ours uses VFMA (the "v4" build,
 * as ROOL's builds for VFPv4 machines do): then differences are counted
 * and listed, not failed, unless "strict" is given as the second argument.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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

static uint64_t
call (const char *t32, const char *t64, int prec, int fn, int nargs, const uint64_t *a,
      unsigned *flags)
{
  const void *p = (prec == 64 ? t64 : t32) + 4 * fn;
  unsigned saved, after;
  uint64_t r;
  union { double d; uint64_t b; } x[3], rd;
  union { float f; uint32_t b; } y[3], rf;
  for (int i = 0; i < 3; i++)
    {
      x[i].b = a[i];
      y[i].b = (uint32_t) a[i];
    }
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (saved));
  __asm__ volatile ("vmsr fpscr, %0" : : "r" (saved & ~0x9Fu));
  if (prec == 64)
    {
      switch (nargs)
	{
	case 1: rd.d = ((f64_1) p) (x[0].d); break;
	case 2: rd.d = ((f64_2) p) (x[0].d, x[1].d); break;
	default: rd.d = ((f64_3) p) (x[0].d, x[1].d, x[2].d); break;
	}
      r = rd.b;
    }
  else
    {
      switch (nargs)
	{
	case 1: rf.f = ((f32_1) p) (y[0].f); break;
	case 2: rf.f = ((f32_2) p) (y[0].f, y[1].f); break;
	default: rf.f = ((f32_3) p) (y[0].f, y[1].f, y[2].f); break;
	}
      r = rf.b;
    }
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (after));
  __asm__ volatile ("vmsr fpscr, %0" : : "r" (saved));
  *flags = after & 0x9F;
  return r;
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    {
      printf ("usage: t_rool_elementary ROOL_VFPSUPPORT_MODULE [strict]\n");
      return 2;
    }
  int strict = argc > 2 && strcmp (argv[2], "strict") == 0;
  FILE *f = fopen (argv[1], "rb");
  if (!f)
    {
      printf ("can't open %s\nFAIL\n", argv[1]);
      return 2;
    }
  static unsigned char mod[256 * 1024] __attribute__ ((aligned (8)));
  size_t len = fread (mod, 1, sizeof mod, f);
  fclose (f);
  _swix (OS_SynchroniseCodeAreas, _INR (0, 2), 1, mod, mod + len - 4);

  /* ROOL's table: 24 consecutive "B" instructions (condition AL). */
  const unsigned *w = (const unsigned *) mod;
  const char *rool32 = NULL;
  for (size_t i = 0; i + 24 <= len / 4 && !rool32; i++)
    {
      int k = 0;
      while (k < 24 && (w[i + k] >> 24) == 0xEA)
	k++;
      if (k == 24)
	rool32 = (const char *) &w[i];
    }
  void *our32, *our64;
  int n;
  if (!rool32 || _swix (VFPSupport_ElementaryFunctions, _IN (0) | _OUTR (0, 2), 0, &n, &our32, &our64))
    {
      printf ("no table\nFAIL\n");
      return 2;
    }
  const char *rool64 = rool32 + 48;
  unsigned hwcap_v4 = 0;
  int features;
  _swix (VFPSupport_Features, _IN (0) | _OUTR (2, 2), 0, &features);
  hwcap_v4 = (features >> 24) & 0xF;	/* MVFR1 FMAC field: VFPv4 */
  printf ("ours: the %s build\n", hwcap_v4 ? "VFMA (v4)" : "software FMA (v3)");

  unsigned diffs[F_COUNT][2] = { { 0 } }, total = 0, count = 0;
  for (size_t i = 0; i < sizeof samples / sizeof samples[0] + sizeof edges / sizeof edges[0] * 2; i++)
    {
      int fn, prec, nargs;
      uint64_t a[3] = { 0, 0, 0 };
      if (i < sizeof samples / sizeof samples[0])
	{
	  const sample_t *s = &samples[i];
	  fn = s->fn; prec = s->prec; nargs = s->nargs;
	  memcpy (a, s->a, sizeof a);
	}
      else
	{
	  size_t j = i - sizeof samples / sizeof samples[0];
	  const edge_t *e = &edges[j / 2];
	  fn = e->fn; nargs = e->nargs; prec = j & 1 ? 32 : 64;
	  a[0] = prec == 64 ? e->in64[0] : e->in32[0];
	  a[1] = prec == 64 ? e->in64[1] : e->in32[1];
	}
      unsigned fr, fo;
      uint64_t rr = call (rool32, rool64, prec, fn, nargs, a, &fr);
      uint64_t ro = call (our32, our64, prec, fn, nargs, a, &fo);
      count++;
      if (rr != ro || fr != fo)
	{
	  if (total < 8)
	    printf ("  %s%d(%llx, %llx, %llx): ROOL %llx flags %02x, ours %llx flags %02x\n",
		    fn_names[fn], prec, a[0], a[1], a[2], rr, fr, ro, fo);
	  diffs[fn][prec == 32]++;
	  total++;
	}
    }
  for (int fn = 0; fn < F_COUNT; fn++)
    if (diffs[fn][0] || diffs[fn][1])
      printf ("%-5s: %u double, %u single different\n", fn_names[fn], diffs[fn][0], diffs[fn][1]);
  printf ("%u calls, %u different from ROOL's\n", count, total);
  int ok = total == 0 || (hwcap_v4 && !strict);
  printf ("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
