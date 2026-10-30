/* t_wimp_vfp: two desktop tasks using VFP at the same time.
 *
 *   t_wimp_vfp          start a second copy of itself ("t_wimp_vfp B"),
 *                       then both run the test; this one waits for the
 *                       other's result and exits with the outcome
 *   t_wimp_vfp B        the second copy
 *
 * Each task fills D0-D31 (D0-D15 on a 16-register VFP) and FPSCR's rounding
 * mode with values of its own, then calls Wimp_Poll 200 times, so that the
 * Wimp switches to the other task in between, and after each poll checks
 * that every register and the rounding mode are as it left them. It also
 * computes with the rounding mode (1/3 rounded up and down must differ in
 * the last bit). The second task reports its result to the first by a
 * Wimp message (&5A5A0 + result); the first exits with 0 if both passed,
 * 1 if it failed, 2 if the other failed, 3 if it never heard from it.
 * The window shows "A: PASS" / "B: PASS" for the screenshot.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <kernel.h>
#include <swis.h>

#define MSG_RESULT 0x5A5A0

static int nregs;
static char text[64];

/* D registers <- seed + i (as bit patterns), FPSCR rounding mode <- mode. */
static void
fill (uint64_t seed, unsigned mode)
{
  uint64_t v[32];
  for (int i = 0; i < 32; i++)
    v[i] = seed * 0x9E3779B97F4A7C15ull + i;
  unsigned fpscr;
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (fpscr));
  fpscr = (fpscr & ~(3u << 22)) | (mode << 22);
  __asm__ volatile ("vmsr fpscr, %0" : : "r" (fpscr));
  __asm__ volatile ("vldmia %0, {d0-d15}" : : "r" (v) : "memory");
  if (nregs == 32)
    __asm__ volatile ("vldmia %0, {d16-d31}" : : "r" (v + 16) : "memory");
}

/* 0 if D registers and the rounding mode are as fill() left them. The
   compiler mustn't use VFP registers in between: this file is built with
   the registers live across the SWIs only through these asm blocks, and the
   checks happen straight after Wimp_Poll returns. */
static int
check (uint64_t seed, unsigned mode)
{
  uint64_t v[32];
  unsigned fpscr;
  __asm__ volatile ("vstmia %0, {d0-d15}" : : "r" (v) : "memory");
  if (nregs == 32)
    __asm__ volatile ("vstmia %0, {d16-d31}" : : "r" (v + 16) : "memory");
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (fpscr));
  for (int i = 0; i < nregs; i++)
    if (v[i] != seed * 0x9E3779B97F4A7C15ull + i)
      return 1 + i;
  if (((fpscr >> 22) & 3) != mode)
    return 100;
  return 0;
}

/* 1/3 rounded towards +inf and towards -inf must differ. All in one asm
   block: the compiler may move ordinary FP arithmetic past an FPSCR write. */
static int
rounding_works (void)
{
  uint64_t up, down;
  __asm__ volatile (
    "vmrs	r0, fpscr\n\t"
    "vmov.f64	d6, #1.0\n\t"
    "vmov.f64	d7, #3.0\n\t"
    "bic	r1, r0, #0xC00000\n\t"
    "orr	r2, r1, #0x400000\n\t"		/* towards +inf */
    "vmsr	fpscr, r2\n\t"
    "vdiv.f64	d5, d6, d7\n\t"
    "vmov	%Q0, %R0, d5\n\t"
    "orr	r2, r1, #0x800000\n\t"		/* towards -inf */
    "vmsr	fpscr, r2\n\t"
    "vdiv.f64	d5, d6, d7\n\t"
    "vmov	%Q1, %R1, d5\n\t"
    "vmsr	fpscr, r0"
    : "=&r" (up), "=&r" (down) : : "r0", "r1", "r2", "d5", "d6", "d7", "cc");
  return up == down + 1;	/* positive doubles: one ulp apart */
}

int
main (int argc, char **argv)
{
  int child = argc > 1 && strcmp (argv[1], "B") == 0;
  const char name = child ? 'B' : 'A';
  static int b[64];
  int task, win, other = 0, other_result = -1;
  int messages[] = { MSG_RESULT, 0 };
  _kernel_oserror *e;

  int features;
  if ((e = _swix (VFPSupport_Features, _IN (0) | _OUT (1), 0, &features)))
    return 4;
  nregs = (features & 0xF) == 2 ? 32 : 16;

  if ((e = _swix (Wimp_Initialise, _INR (0, 3) | _OUT (1),
		  380, 0x4B534154, child ? "t_wimp_vfp B" : "t_wimp_vfp A", messages, &task)))
    return 5;

  /* A window to show the result in. */
  int *w = b;
  memset (b, 0, sizeof b);
  int x0 = child ? 1100 : 300;
  w[0] = x0; w[1] = 900; w[2] = x0 + 600; w[3] = 1100;
  w[6] = -1;
  w[7] = (int) 0xFF000012;
  ((unsigned char *) w)[32] = 7; ((unsigned char *) w)[33] = 2;
  ((unsigned char *) w)[34] = 7; ((unsigned char *) w)[35] = 0;
  ((unsigned char *) w)[36] = 3; ((unsigned char *) w)[37] = 1;
  ((unsigned char *) w)[38] = 12;
  w[11] = -200; w[12] = 600;
  w[14] = 0x3D; w[16] = 1;
  memcpy (&w[18], child ? "VFP task B" : "VFP task A", 10);
  w[21] = 1;
  int *icon = &w[22];
  icon[0] = 50; icon[1] = -150; icon[2] = 550; icon[3] = -50;
  icon[4] = 0x1700013D;
  snprintf (text, sizeof text, "%c: running", name);
  icon[5] = (int) text; icon[6] = -1; icon[7] = sizeof text;
  if ((e = _swix (Wimp_CreateWindow, _IN (1) | _OUT (0), b, &win)))
    return 6;
  b[0] = win; b[1] = x0; b[2] = 900; b[3] = x0 + 600; b[4] = 1100;
  b[5] = 0; b[6] = 0; b[7] = -1;
  _swix (Wimp_OpenWindow, _IN (1), b);

  if (!child)
    {
      /* The second copy: the same program (the first word of the command
	 line) with the argument B. */
      char *env, cmd[512];
      _swix (OS_GetEnv, _OUT (0), &env);
      size_t n = strcspn (env, " ");
      snprintf (cmd, sizeof cmd, "%.*s B", (int) (n < 400 ? n : 400), env);
      if ((e = _swix (Wimp_StartTask, _IN (0) | _OUT (0), cmd, &other)))
	return 7;
    }

  uint64_t seed = child ? 0xB0B : 0xA0A;
  unsigned mode = child ? 3 : 1;
  int bad = rounding_works () ? 0 : 200;
  fill (seed, mode);
  for (int i = 0; i < 200 && !bad; i++)
    {
      int reason;
      if ((e = _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 0, b, &reason)))
	return 8;
      bad = check (seed, mode);
      if (reason == 17 || reason == 18)
	{
	  if (b[4] == MSG_RESULT)
	    other_result = b[5];
	  else if (b[4] == 0)
	    exit (9);
	}
      else if (reason == 2)
	_swix (Wimp_OpenWindow, _IN (1), b);
    }

  snprintf (text, sizeof text, "%c: %s", name, bad ? "FAIL" : "PASS");
  _swix (Wimp_SetIconState, _IN (1), (int[]) { win, 0, 0, 0 });

  if (child)
    {
      /* Tell A, broadcast (A's task handle isn't known here). */
      memset (b, 0, 24);
      b[0] = 24; b[4] = MSG_RESULT; b[5] = bad;
      _swix (Wimp_SendMessage, _INR (0, 2), 17, b, 0);
      for (int i = 0; i < 400; i++)	/* stay up for the screenshot */
	{
	  int reason;
	  _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 0, b, &reason);
	  if ((reason == 17 || reason == 18) && b[4] == 0)
	    break;
	}
      _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
      return bad ? 1 : 0;
    }

  /* A: wait (up to ~10000 polls) for B's result. */
  for (int i = 0; i < 10000 && other_result < 0; i++)
    {
      int reason;
      _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 0, b, &reason);
      if ((reason == 17 || reason == 18) && b[4] == MSG_RESULT)
	other_result = b[5];
    }
  snprintf (text, sizeof text, "A: %s, B: %s", bad ? "FAIL" : "PASS",
	    other_result < 0 ? "no answer" : other_result ? "FAIL" : "PASS");
  _swix (Wimp_SetIconState, _IN (1), (int[]) { win, 0, 0, 0 });
  for (int i = 0; i < 20; i++)		/* let it redraw */
    {
      int reason;
      _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 0, b, &reason);
    }
  _swix (OS_CLI, _IN (0), "TBShot");
  _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
  return bad ? 1 : other_result < 0 ? 3 : other_result ? 2 : 0;
}
