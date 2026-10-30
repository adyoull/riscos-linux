/* t_vfp_irq: VFPSupport switches contexts atomically, as ROOL's does with
 * interrupts off. An OS_CallEvery handler (run from an interrupt, every
 * centisecond) switches to a context of its own with FastAPI's
 * ChangeContext, writes to D0-D3 there, and switches back; meanwhile the
 * foreground switches between two contexts F and G as fast as it can and
 * checks each time that D0-D3 hold that context's values.
 *
 * If a switch weren't atomic, an interrupt between loading the new
 * registers and recording the new active context would save them into the
 * old context, and the foreground would later find the wrong values.
 *
 * Prints how many interrupts ran and PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#ifndef VFPSupport_FastAPI
#define VFPSupport_FastAPI 0x58EC5
#endif

/* What the handler needs (r12 points here). */
static struct
{
  unsigned change;		/* +0 FastAPI ChangeContext */
  unsigned ws;			/* +4 its workspace (r12) */
  unsigned ctx;			/* +8 the handler's context */
  unsigned count;		/* +12 interrupts handled */
} h;

/* The OS_CallEvery handler: IRQs off, r12 = &h; must preserve all
   registers. */
__attribute__ ((naked)) static void
handler (void)
{
  __asm__ volatile (
    "push	{r0-r4, r12, lr}\n\t"
    "mov	r4, r12\n\t"
    "ldr	r0, [r4, #8]\n\t"	/* switch to our context */
    "mov	r1, #0\n\t"
    "ldr	r12, [r4, #4]\n\t"
    "ldr	r2, [r4, #0]\n\t"
    "blx	r2\n\t"
    "mov	r3, r0\n\t"		/* the interrupted one */
    "mov	r0, #0xDE\n\t"		/* scribble on D0-D3 (ours now) */
    "vmov	d0, r0, r0\n\t"
    "vmov	d1, r0, r0\n\t"
    "vmov	d2, r0, r0\n\t"
    "vmov	d3, r0, r0\n\t"
    "mov	r0, r3\n\t"		/* and switch back */
    "mov	r1, #0\n\t"
    "ldr	r12, [r4, #4]\n\t"
    "ldr	r2, [r4, #0]\n\t"
    "blx	r2\n\t"
    "ldr	r0, [r4, #12]\n\t"
    "add	r0, r0, #1\n\t"
    "str	r0, [r4, #12]\n\t"
    "pop	{r0-r4, r12, pc}\n\t");
}

/* Switch to CTX with a SWI, then 0 if D0-D3 are each SEED * 0x10001, else
   the first bad register + 1. */
static int
switch_and_check (unsigned ctx, unsigned seed)
{
  register unsigned r0 asm ("r0") = ctx;
  register unsigned r1 asm ("r1") = 0;
  register unsigned r2 asm ("r2") = seed * 0x10001u;
  register unsigned r3 asm ("r3");
  __asm__ volatile (
    "swi	0x78EC3\n\t"		/* XVFPSupport_ChangeContext */
    "mov	r3, #0\n\t"
    "vmov	r0, r1, d0\n\t"
    "cmp	r0, r2\n\tcmpeq r1, r2\n\tmovne r3, #1\n\tbne 1f\n\t"
    "vmov	r0, r1, d1\n\t"
    "cmp	r0, r2\n\tcmpeq r1, r2\n\tmovne r3, #2\n\tbne 1f\n\t"
    "vmov	r0, r1, d2\n\t"
    "cmp	r0, r2\n\tcmpeq r1, r2\n\tmovne r3, #3\n\tbne 1f\n\t"
    "vmov	r0, r1, d3\n\t"
    "cmp	r0, r2\n\tcmpeq r1, r2\n\tmovne r3, #4\n"
    "1:"
    : "+r" (r0), "+r" (r1), "+r" (r2), "=r" (r3) : : "cc", "lr", "memory");
  return r3;
}

/* Switch to CTX and fill D0-D3 with SEED * 0x10001; switch back to BACK. */
static void
fill (unsigned ctx, unsigned seed, unsigned back)
{
  register unsigned r0 asm ("r0") = ctx;
  register unsigned r1 asm ("r1") = 0;
  register unsigned r2 asm ("r2") = seed * 0x10001u;
  register unsigned r3 asm ("r3") = back;
  __asm__ volatile (
    "swi	0x78EC3\n\t"
    "vmov	d0, r2, r2\n\tvmov d1, r2, r2\n\tvmov d2, r2, r2\n\tvmov d3, r2, r2\n\t"
    "mov	r0, r3\n\tmov r1, #0\n\t"
    "swi	0x78EC3\n\t"
    : "+r" (r0), "+r" (r1), "+r" (r2), "+r" (r3) : : "cc", "lr", "memory");
}

int
main (void)
{
  unsigned mine, f, g, t0, t, unused;
  _swix (VFPSupport_ActiveContext, _OUT (0), &mine);
  if (_swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 0, 16, 0, 0, &f)
      || _swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 0, 16, 0, 0, &g)
      || _swix (VFPSupport_CreateContext, _INR (0, 3) | _OUT (0), 0, 16, 0, 0, &h.ctx)
      || _swix (VFPSupport_FastAPI, _OUTR (0, 4), &h.ws, &unused, &unused, &unused, &h.change))
    {
      printf ("set-up failed\nFAIL\n");
      return 1;
    }
  fill (f, 0x1234, mine);
  fill (g, 0x5678, mine);
  _kernel_oserror *e = _swix (OS_CallEvery, _INR (0, 2), 1, handler, &h);	/* every 2 cs */
  if (e)
    {
      printf ("OS_CallEvery: %s\nFAIL\n", e->errmess);
      return 1;
    }
  unsigned switches = 0, bad = 0, where = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t0);
  do
    {
      for (int i = 0; i < 200; i++)
	{
	  int r = switch_and_check (f, 0x1234);
	  if (r && !bad++)
	    where = r;
	  r = switch_and_check (g, 0x5678);
	  if (r && !bad++)
	    where = 0x100 + r;
	  switches += 2;
	}
      _swix (VFPSupport_ChangeContext, _INR (0, 1), mine, 0);
      _swix (OS_ReadMonotonicTime, _OUT (0), &t);
    }
  while (t - t0 < 600);
  _swix (OS_RemoveTickerEvent, _INR (0, 1), handler, &h);
  _swix (VFPSupport_ChangeContext, _INR (0, 1), mine, 0);
  printf ("%u switches, %u interrupts that switched too, %u wrong (first: %c D%u)\n",
	  switches, h.count, bad, where >= 0x100 ? 'G' : 'F', (where & 0xFF) ? (where & 0xFF) - 1 : 0);
  _swix (VFPSupport_DestroyContext, _INR (0, 1), f, 0);
  _swix (VFPSupport_DestroyContext, _INR (0, 1), g, 0);
  _swix (VFPSupport_DestroyContext, _INR (0, 1), h.ctx, 0);
  int ok = !bad && h.count >= 200;
  printf ("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
