/* t_wimp: a small desktop program for rol-run's desktop mode.
 *
 *   t_wimp            open a window, take a screenshot (*TBShot), stay
 *   t_wimp exit N     the same, then leave with exit status N
 *
 * The window's work area is white, at OS units x 400-1200, y 600-1000
 * (pixels 200-600 across, 268-468 down on a 1024x768 screen), with a
 * text icon "Hello from the desktop" in it. tests/run.sh checks the
 * screenshot's pixels.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

static char text[] = "Hello from the desktop";

static void
fail (const _kernel_oserror *e)
{
  fprintf (stderr, "t_wimp: %s\n", e->errmess);
  exit (1);
}

int
main (int argc, char **argv)
{
  int exit_with = argc == 3 && strcmp (argv[1], "exit") == 0 ? atoi (argv[2]) : -1;
  static int b[64];
  int task, win, polls = 0, shot = 0;
  _kernel_oserror *e;
  int messages[] = { 0 };

  if ((e = _swix (Wimp_Initialise, _INR (0, 3) | _OUT (1),
		  380, 0x4B534154, "t_wimp", messages, &task)))
    fail (e);

  /* The window definition. */
  int *w = b;
  w[0] = 400; w[1] = 600; w[2] = 1200; w[3] = 1000;	/* visible area */
  w[4] = 0; w[5] = 0; w[6] = -1;			/* scroll, behind */
  w[7] = (int) 0xFF000012;				/* moveable, autoredraw, all tools */
  ((unsigned char *) w)[32] = 7;			/* title fg */
  ((unsigned char *) w)[33] = 2;			/* title bg */
  ((unsigned char *) w)[34] = 7;			/* work fg */
  ((unsigned char *) w)[35] = 0;			/* work bg: white */
  ((unsigned char *) w)[36] = 3;
  ((unsigned char *) w)[37] = 1;
  ((unsigned char *) w)[38] = 12;
  ((unsigned char *) w)[39] = 0;
  w[10] = 0; w[11] = -400; w[12] = 800; w[13] = 0;	/* extent */
  w[14] = 0x3D;						/* title: text, centred, filled */
  w[15] = 0;						/* work area button type */
  w[16] = 1;						/* sprite area: Wimp's */
  w[17] = 0;						/* minimum size */
  memcpy (&w[18], "rol-run test", 12);			/* title text (12 bytes) */
  w[21] = 1;						/* one icon */
  int *icon = &w[22];
  icon[0] = 100; icon[1] = -250; icon[2] = 700; icon[3] = -150;
  icon[4] = 0x1700013D;		/* text, border, centred, filled, indirected; fg 7, bg 1 */
  icon[5] = (int) text; icon[6] = -1; icon[7] = sizeof text;
  if ((e = _swix (Wimp_CreateWindow, _IN (1) | _OUT (0), b, &win)))
    fail (e);

  b[0] = win;
  b[1] = 400; b[2] = 600; b[3] = 1200; b[4] = 1000;
  b[5] = 0; b[6] = 0; b[7] = -1;
  if ((e = _swix (Wimp_OpenWindow, _IN (1), b)))
    fail (e);

  for (;;)
    {
      int reason;
      if ((e = _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 0, b, &reason)))
	fail (e);
      switch (reason)
	{
	case 0:		/* null: once the window has been drawn, take a shot */
	  if (++polls == 20 && !shot)
	    {
	      shot = 1;
	      if ((e = _swix (OS_CLI, _IN (0), "TBShot")))
		fail (e);
	      printf ("t_wimp: shot taken\n");
	      if (exit_with >= 0)
		{
		  _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
		  return exit_with;
		}
	    }
	  break;
	case 2:		/* open window request */
	  _swix (Wimp_OpenWindow, _IN (1), b);
	  break;
	case 3:		/* close window request */
	  _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
	  return 0;
	case 17:
	case 18:
	  if (b[4] == 0)	/* Message_Quit */
	    {
	      _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
	      return 0;
	    }
	  break;
	}
    }
}
