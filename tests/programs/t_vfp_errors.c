/* t_vfp_errors: floating point errors on RISC OS for Linux.
 *
 * 1. The FPSCR's cumulative exception flags, from VFP instructions: divide
 *    by zero (DZC), invalid operations (0/0, sqrt -1, inf - inf: IOC, with
 *    a NaN), overflow (OFC), underflow (UFC), inexact (IXC) and, in
 *    flush-to-zero mode, a subnormal input (IDC); each from clear flags,
 *    with the result IEEE 754 asks for.
 * 2. Trapped exceptions: the trap-enable bits (IOE...IDE) read back as
 *    zero (Linux and every Cortex-A core leave them unsupported), so 1/0
 *    with them "enabled" still gives infinity and DZC, no error; and
 *    VFPSupport_Features 1 says no exceptions can be trapped.
 * 3. Each VFPSupport context keeps its own flags: set in A, clear in B,
 *    still set in A after switching back.
 * 4. VFPSupport's own errors: CheckContext and CreateContext with 0 or too
 *    many D registers or unknown flags (&81F102), DestroyContext 0
 *    (&81F103), Features 99 (&81F104), ExceptionDump with unknown flags
 *    (&81F107), ElementaryFunctions r0 <> 0, and an unknown SWI in the
 *    chunk (&1E6).
 *
 * Prints what it checked, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel.h>
#include <swis.h>

#ifndef VFPSupport_CheckContext
#define VFPSupport_CheckContext   0x58EC0
#define VFPSupport_CreateContext  0x58EC1
#define VFPSupport_DestroyContext 0x58EC2
#define VFPSupport_ChangeContext  0x58EC3
#define VFPSupport_ActiveContext  0x58EC6
#define VFPSupport_Features       0x58EC8
#endif
#ifndef VFPSupport_ExceptionDump
#define VFPSupport_ExceptionDump  0x58EC9
#endif
#ifndef VFPSupport_ElementaryFunctions
#define VFPSupport_ElementaryFunctions 0x58ECA
#endif

#define IOC 0x01u
#define DZC 0x02u
#define OFC 0x04u
#define UFC 0x08u
#define IXC 0x10u
#define IDC 0x80u
#define FLAGS 0x9Fu
#define TRAPS 0x9F00u			/* IOE DZE OFE UFE IXE IDE */
#define FZ (1u << 24)

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static unsigned
get_fpscr (void)
{
  unsigned v;
  asm volatile ("vmrs %0, fpscr" : "=r" (v));
  return v;
}

static void
set_fpscr (unsigned v)
{
  asm volatile ("vmsr fpscr, %0" : : "r" (v) : "memory");
}

/* Run OP on A and B with the FPSCR set to MODE (flags clear); return the
   flags it left, and the result in *R. Each in one asm block, so that the
   compiler can't move or fold the operation. */
enum { DIV, MUL, SUB, SQRT, ADD };

static unsigned
op (int which, double a, double b, unsigned mode, double *r)
{
  double x;
  unsigned flags;
  set_fpscr (mode);
  switch (which)
    {
    case DIV:  asm volatile ("vdiv.f64 %P0, %P1, %P2" : "=w" (x) : "w" (a), "w" (b)); break;
    case MUL:  asm volatile ("vmul.f64 %P0, %P1, %P2" : "=w" (x) : "w" (a), "w" (b)); break;
    case SUB:  asm volatile ("vsub.f64 %P0, %P1, %P2" : "=w" (x) : "w" (a), "w" (b)); break;
    case ADD:  asm volatile ("vadd.f64 %P0, %P1, %P2" : "=w" (x) : "w" (a), "w" (b)); break;
    default:   asm volatile ("vsqrt.f64 %P0, %P1" : "=w" (x) : "w" (a)); break;
    }
  flags = get_fpscr () & FLAGS;
  set_fpscr (0);
  *r = x;
  return flags;
}

static uint64_t
bits (double x)
{
  uint64_t b;
  memcpy (&b, &x, 8);
  return b;
}

static double
from (uint64_t b)
{
  double x;
  memcpy (&x, &b, 8);
  return x;
}

#define INF  0x7FF0000000000000ull
#define NINF 0xFFF0000000000000ull

static int
is_nan (double x)
{
  uint64_t b = bits (x);
  return (b & INF) == INF && (b & 0x000FFFFFFFFFFFFFull);
}

static void
flags_case (const char *what, int which, double a, double b, unsigned mode,
	    unsigned want, int want_nan, uint64_t want_bits)
{
  double r;
  unsigned got = op (which, a, b, mode, &r);
  int ok = got == want && (want_nan ? is_nan (r) : bits (r) == want_bits);
  printf ("%-28s flags &%02X (want &%02X), result &%016llX %s\n", what, got, want,
	  (unsigned long long) bits (r), ok ? "ok" : "WRONG");
  if (!ok)
    fails++;
}

/* The error a SWI gives (0 if none) */
static int
swi_err (int swi, unsigned r0, unsigned r1)
{
  _kernel_oserror *e = _swix (swi, _INR (0, 1), r0, r1);
  return e ? e->errnum : 0;
}

int
main (void)
{
  /* 1. flags from instructions */
  double tiny = from (0x0010000000000000ull);	/* smallest normal, 2^-1022 */
  double sub = from (0x0000000000000001ull);	/* smallest subnormal */
  flags_case ("1 / 0", DIV, 1.0, 0.0, 0, DZC, 0, INF);
  flags_case ("-1 / 0", DIV, -1.0, 0.0, 0, DZC, 0, NINF);
  flags_case ("0 / 0", DIV, 0.0, 0.0, 0, IOC, 1, 0);
  flags_case ("sqrt -1", SQRT, -1.0, 0.0, 0, IOC, 1, 0);
  flags_case ("inf - inf", SUB, from (INF), from (INF), 0, IOC, 1, 0);
  flags_case ("1e308 * 1e308", MUL, 1e308, 1e308, 0, OFC | IXC, 0, INF);
  flags_case ("2^-1022 * 0.3", MUL, tiny, 0.3, 0, UFC | IXC, 0, 0x0004CCCCCCCCCCCDull);
  flags_case ("1 / 3", DIV, 1.0, 3.0, 0, IXC, 0, 0x3FD5555555555555ull);
  flags_case ("1 + 1 (none)", ADD, 1.0, 1.0, 0, 0, 0, 0x4000000000000000ull);
  flags_case ("subnormal + 0, flush to 0", ADD, sub, 0.0, FZ, IDC, 0, 0);

  /* 2. trapped exceptions aren't supported: the enables read back as 0,
     and an "enabled" exception just sets its flag */
  set_fpscr (TRAPS);
  unsigned back = get_fpscr ();
  set_fpscr (0);
  printf ("trap enables written &%04X, read back &%04X\n", TRAPS, back & TRAPS);
  CHECK ((back & TRAPS) == 0);
  {
    double r;
    unsigned got = op (DIV, 1.0, 0.0, TRAPS, &r);
    printf ("1 / 0 with traps \"enabled\": flags &%02X, result %s\n", got,
	    bits (r) == INF ? "infinity" : "WRONG");
    CHECK (got == DZC && bits (r) == INF);
  }
  unsigned f1 = 99;
  CHECK (!_swix (VFPSupport_Features, _IN (0) | _OUT (0), 1, &f1));
  printf ("Features 1 (trappable exceptions) &%X\n", f1);
  CHECK (f1 == 0);

  /* 3. flags belong to the context */
  unsigned mine = 0, a = 0, b = 0;
  CHECK (!_swix (VFPSupport_ActiveContext, _OUT (0), &mine));
  CHECK (!_swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 1, 16, 0, 0, &a));
  CHECK (!_swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 1, 16, 0, 0, &b));
  if (a && b)
    {
      unsigned fa, fb, fa2;
      double r;
      _swix (VFPSupport_ChangeContext, _INR (0, 1), a, 0);
      set_fpscr (0);
      asm volatile ("vdiv.f64 %P0, %P1, %P2" : "=w" (r) : "w" (1.0), "w" (0.0));
      fa = get_fpscr () & FLAGS;
      _swix (VFPSupport_ChangeContext, _INR (0, 1), b, 0);
      fb = get_fpscr () & FLAGS;
      _swix (VFPSupport_ChangeContext, _INR (0, 1), a, 0);
      fa2 = get_fpscr () & FLAGS;
      _swix (VFPSupport_ChangeContext, _INR (0, 1), mine, 0);
      printf ("context flags: A &%02X, B &%02X, A again &%02X\n", fa, fb, fa2);
      CHECK (fa == DZC && fb == 0 && fa2 == DZC);
      _swix (VFPSupport_DestroyContext, _INR (0, 1), a, 0);
      _swix (VFPSupport_DestroyContext, _INR (0, 1), b, 0);
    }

  /* 4. VFPSupport's errors */
  struct { const char *what; int got, want; } e[] = {
    { "CheckContext 0 registers", swi_err (VFPSupport_CheckContext, 0, 0), 0x81F102 },
    { "CheckContext 33 registers", swi_err (VFPSupport_CheckContext, 0, 33), 0x81F102 },
    { "CheckContext unknown flags", swi_err (VFPSupport_CheckContext, 0x100, 16), 0x81F102 },
    { "CreateContext 0 registers", swi_err (VFPSupport_CreateContext, 0, 0), 0x81F102 },
    { "DestroyContext 0", swi_err (VFPSupport_DestroyContext, 0, 0), 0x81F103 },
    { "Features 99", swi_err (VFPSupport_Features, 99, 0), 0x81F104 },
    { "ExceptionDump flags &10", swi_err (VFPSupport_ExceptionDump, 0x10, 0), 0x81F107 },
    { "ElementaryFunctions r0=1", swi_err (VFPSupport_ElementaryFunctions, 1, 0), -1 },
    { "SWI &58ECF (unknown)", swi_err (0x58ECF, 0, 0), 0x1E6 },
  };
  for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++)
    {
      int ok = e[i].want == -1 ? e[i].got != 0 : e[i].got == e[i].want;
      printf ("%-28s error &%X %s\n", e[i].what, (unsigned) e[i].got, ok ? "ok" : "WRONG");
      if (!ok)
	fails++;
    }
  /* still the program's own context, and flags clear */
  unsigned now = 0;
  CHECK (!_swix (VFPSupport_ActiveContext, _OUT (0), &now));
  CHECK (now == mine);

  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
