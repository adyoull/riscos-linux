/* VFPSupport's SWIs directly: two contexts, each with its own D0-D15 (and
   D16-D31 where the CPU has them) and FPSCR, switched with ChangeContext
   (also with the Wimp's Lazy+AppSpace flags, and through FastAPI); then
   ExamineContext, ExceptionDump, DestroyContext of the active context,
   Features (0-3), Version and the program's own context. */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#ifndef VFPSupport_CheckContext
#define VFPSupport_CheckContext   0x58EC0
#define VFPSupport_CreateContext  0x58EC1
#define VFPSupport_DestroyContext 0x58EC2
#define VFPSupport_ChangeContext  0x58EC3
#define VFPSupport_ExamineContext 0x58EC4
#define VFPSupport_ActiveContext  0x58EC6
#define VFPSupport_Version        0x58EC7
#define VFPSupport_Features       0x58EC8
#endif
#ifndef VFPSupport_FastAPI
#define VFPSupport_FastAPI        0x58EC5
#endif
#ifndef VFPSupport_ExceptionDump
#define VFPSupport_ExceptionDump  0x58EC9
#endif

/* Call a FastAPI routine: r12 = workspace, r0, r1 in; r0 out (and the V
   flag: returns -1 for an error). */
static unsigned
fast_call (unsigned fn, unsigned ws, unsigned a0, unsigned a1, int *err)
{
  register unsigned r0 asm ("r0") = a0;
  register unsigned r1 asm ("r1") = a1;
  register unsigned r2 asm ("r2") = fn;
  register unsigned r3 asm ("r3") = ws;
  unsigned v;
  asm volatile ("push {r12}\n\tmov r12, r3\n\tblx r2\n\tpop {r12}\n\t"
		"mrs %[v], apsr"
		: "+r" (r0), "+r" (r1), "+r" (r2), "+r" (r3), [v] "=r" (v)
		: : "memory", "cc", "lr");
  *err = (v >> 28) & 1;
  return r0;
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

/* Switch to context TO, fill the registers with values from SEED, switch
   to BACK (all in one asm block, so the compiler doesn't use them). */
static void
fill_in (unsigned to, unsigned back, unsigned seed, int nregs)
{
  unsigned vals[64];
  for (int i = 0; i < 64; i++)
    vals[i] = seed * 0x01010101u + i;
  register unsigned r0 asm ("r0");
  register unsigned r1 asm ("r1");
  register const unsigned *r2 asm ("r2") = vals;
  register unsigned r3 asm ("r3") = (unsigned) nregs;
  register unsigned r4 asm ("r4") = to;
  register unsigned r5 asm ("r5") = back;
  register unsigned r6 asm ("r6") = 0x00C00000 | ((seed & 1) << 24);   /* RMode, FZ */
  asm volatile ("mov r0, r4\n\tmov r1, #0\n\tswi %[change]\n\t"
		"vldmia r2!, {d0-d15}\n\t"
		"cmp r3, #32\n\t"
		"vldmiaeq r2, {d16-d31}\n\t"
		"vmsr fpscr, r6\n\t"
		"mov r0, r5\n\tmov r1, #0\n\tswi %[change]\n\t"
		: "=r" (r0), "=r" (r1), "+r" (r2)
		: "r" (r3), "r" (r4), "r" (r5), "r" (r6), [change] "i" (0x20000 | VFPSupport_ChangeContext)
		: "memory", "cc", "lr", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9",
		  "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20",
		  "d21", "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
}

/* Switch to TO, read the registers and FPSCR into OUT, switch to BACK. */
static unsigned
read_out (unsigned to, unsigned back, unsigned *out, int nregs)
{
  register unsigned r0 asm ("r0");
  register unsigned r1 asm ("r1");
  register unsigned *r2 asm ("r2") = out;
  register unsigned r3 asm ("r3") = (unsigned) nregs;
  register unsigned r4 asm ("r4") = to;
  register unsigned r5 asm ("r5") = back;
  register unsigned r6 asm ("r6");
  asm volatile ("mov r0, r4\n\tmov r1, #0\n\tswi %[change]\n\t"
		"vstmia r2!, {d0-d15}\n\t"
		"cmp r3, #32\n\t"
		"vstmiaeq r2, {d16-d31}\n\t"
		"vmrs r6, fpscr\n\t"
		"mov r0, r5\n\tmov r1, #0\n\tswi %[change]\n\t"
		: "=r" (r0), "=r" (r1), "+r" (r2), "=r" (r6)
		: "r" (r3), "r" (r4), "r" (r5), [change] "i" (0x20000 | VFPSupport_ChangeContext)
		: "memory", "cc", "lr");
  return r6;
}

int
main (void)
{
  _kernel_oserror *e;
  int version = 0, fpsid = 0, mvfr0 = 0, mvfr1 = 0;
  unsigned mine = 0, a = 0, b = 0, size = 0;

  CHECK (!_swix (VFPSupport_Version, _OUT (0), &version));
  CHECK (!_swix (VFPSupport_Features, _IN (0) | _OUTR (0, 2), 0, &fpsid, &mvfr0, &mvfr1));
  int nregs = (mvfr0 & 0xF) == 2 ? 32 : 16;
  printf ("VFPSupport %d.%02d, FPSID &%08X MVFR0 &%08X MVFR1 &%08X, %d D registers\n",
	  version / 100, version % 100, fpsid, mvfr0, mvfr1, nregs);
  CHECK (!_swix (VFPSupport_ActiveContext, _OUT (0), &mine));
  CHECK (mine != 0);		/* UnixLib made one at start-up */

  e = _swix (VFPSupport_CheckContext, _INR (0, 1) | _OUT (0), 0, nregs, &size);
  CHECK (!e && size == 24u + 8u * nregs);
  CHECK (_swix (VFPSupport_CheckContext, _INR (0, 1), 0, 33) != NULL);
  CHECK (!_swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 0, nregs, 0, 0, &a));
  CHECK (!_swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 0, nregs, 0, 0x03000000, &b));

  /* A fresh context reads as zeros, with the FPSCR it was made with. */
  unsigned out[65];
  memset (out, 0xAA, sizeof out);
  unsigned fpscr = read_out (b, mine, out, nregs);
  int zero = 1;
  for (int i = 0; i < 2 * nregs; i++)
    zero &= out[i] == 0;
  CHECK (zero);
  if ((fpscr & 0x0FC00000) != 0x03000000) printf ("fresh FPSCR &%08X\n", fpscr);
  CHECK ((fpscr & 0x0FC00000) == 0x03000000);

  fill_in (a, mine, 1, nregs);
  fill_in (b, mine, 2, nregs);
  double here = 1.5;		/* use our own registers in between */
  for (int i = 0; i < 1000; i++)
    here = here * 1.0001 + 0.25;
  for (unsigned c = 0; c < 2; c++)
    {
      unsigned seed = c + 1;
      memset (out, 0, sizeof out);
      fpscr = read_out (c ? b : a, mine, out, nregs);
      int ok = 1;
      for (int i = 0; i < 2 * nregs; i++)
	ok &= out[i] == seed * 0x01010101u + i;
      printf ("context %c: registers %s, FPSCR &%08X\n", 'A' + c, ok ? "kept" : "LOST", fpscr);
      CHECK (ok);
      CHECK ((fpscr & 0x01C00000) == (0x00C00000 | ((seed & 1) << 24)));
    }
  double want = 1.5;
  for (int i = 0; i < 1000; i++)
    want = want * 1.0001 + 0.25;
  CHECK (here == want);

  /* ExamineContext */
  unsigned flags = 0, n = 0, mask = 0, desc = 0, csize = 0;
  CHECK (!_swix (VFPSupport_ExamineContext, _INR (0, 1) | _OUTR (0, 4), a, 0,
		 &flags, &n, &mask, &desc, &csize));
  CHECK (n == (unsigned) nregs && csize == size && mask == 0 && desc != 0);
  CHECK (!_swix (VFPSupport_ExamineContext, _INR (0, 1) | _OUTR (0, 2), mine, 1,
		 &flags, &n, &mask));
  CHECK (flags & (1u << 30));	/* ours is the active one */

  /* ExceptionDump: Create copies a context (serialised), GetDump gives an
     RMA copy, GetContext the context, Clear forgets it. */
  unsigned dump = 1, ctx = 1;
  CHECK (!_swix (VFPSupport_ExceptionDump, _INR (0, 1) | _OUTR (0, 1), 3, 0, &dump, &ctx));
  CHECK (dump == 0 && ctx == 0);		/* nothing yet */
  CHECK (!_swix (VFPSupport_ExceptionDump, _INR (0, 1), 8, a));
  CHECK (!_swix (VFPSupport_ExceptionDump, _INR (0, 1) | _OUTR (0, 1), 3, 0, &dump, &ctx));
  CHECK (ctx == a && dump != 0);
  if (dump)
    {
      const unsigned *dw = (const unsigned *) dump;
      int same = dw[1] == (unsigned) nregs;
      for (int i = 0; i < 2 * nregs; i++)
	same &= dw[6 + i] == 1 * 0x01010101u + i;
      printf ("ExceptionDump: %s\n", same ? "a copy of context A" : "WRONG");
      CHECK (same);
      CHECK (!_swix (OS_Module, _INR (0, 2), 7, 0, dump));
    }
  CHECK (!_swix (VFPSupport_ExceptionDump, _INR (0, 1) | _OUTR (0, 1), 2 | 4, 0, &dump, &ctx));
  CHECK (ctx == a);				/* returned, then cleared */
  CHECK (!_swix (VFPSupport_ExceptionDump, _INR (0, 1) | _OUTR (0, 1), 3, 0, &dump, &ctx));
  CHECK (dump == 0 && ctx == 0);
  CHECK (_swix (VFPSupport_ExceptionDump, _IN (0), 0x10) != NULL);

  /* ChangeContext as the Wimp calls it (Lazy + AppSpace), and back */
  unsigned prev = 0;
  CHECK (!_swix (VFPSupport_ChangeContext, _INR (0, 1) | _OUT (0), 0, 3, &prev));
  CHECK (prev == mine);
  CHECK (!_swix (VFPSupport_ChangeContext, _INR (0, 1) | _OUT (0), mine, 1, &prev));
  CHECK (prev == 0);

  /* FastAPI */
  unsigned ws = 0, f_check = 0, f_create = 0, f_destroy = 0, f_change = 0;
  CHECK (!_swix (VFPSupport_FastAPI, _OUTR (0, 4), &ws, &f_check, &f_create, &f_destroy, &f_change));
  int err = 1;
  CHECK (fast_call (f_check, ws, 0, nregs, &err) == size && !err);
  fast_call (f_check, ws, 0, 99, &err);
  CHECK (err);
  CHECK (fast_call (f_change, ws, a, 0, &err) == mine && !err);
  CHECK (fast_call (f_change, ws, mine, 0, &err) == a && !err);
  memset (out, 0, sizeof out);
  read_out (a, mine, out, nregs);
  CHECK (out[0] == 0x01010101u && out[2 * nregs - 1] == 0x01010101u + 2 * nregs - 1);

  /* Features 1 (no trappable exceptions here), 2 (elementary functions),
     3 (MVFR2) */
  unsigned f1 = 1, f2 = 0, mvfr2 = 0;
  CHECK (!_swix (VFPSupport_Features, _IN (0) | _OUT (0), 1, &f1));
  CHECK (!_swix (VFPSupport_Features, _IN (0) | _OUT (0), 2, &f2));
  CHECK (!_swix (VFPSupport_Features, _IN (0) | _OUT (0), 3, &mvfr2));
  printf ("Features 1 &%X, 2 &%X, MVFR2 &%08X\n", f1, f2, mvfr2);
  CHECK (f1 == 0 && f2 == 4);

  /* Destroying the active context activates r1 */
  unsigned now = 0;
  CHECK (!_swix (VFPSupport_ChangeContext, _INR (0, 1), a, 0));
  CHECK (!_swix (VFPSupport_DestroyContext, _INR (0, 1) | _OUT (0), a, mine, &now));
  CHECK (now == mine);
  CHECK (!_swix (VFPSupport_ActiveContext, _OUT (0), &now));
  CHECK (now == mine);
  CHECK (!_swix (VFPSupport_DestroyContext, _INR (0, 1), b, 0));
  CHECK (_swix (VFPSupport_Features, _IN (0), 99) != NULL);

  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
