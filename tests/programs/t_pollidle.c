/* t_pollidle: Wimp_PollIdle on an idle desktop.
 *
 * Opens and closes a file (IXFS), then asks for a null event 50 cs from
 * now; three times. Leaves with exit status 7 if each came between 40 and
 * 300 cs later (8 if not), or is still waiting at rol-run's screenshot
 * (exit status 0) if they never come. (Started with rol-run -c in
 * desktop mode they don't: that command runs as an old-style, single-
 * tasking program, and while one runs the Wimp gives nulls to no other
 * task.)
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <kernel.h>
#include <swis.h>

static unsigned
now (void)
{
  unsigned t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  return t;
}

int
main (void)
{
  static int blk[64];
  int task = 0, ok = 1;
  if (_swix (Wimp_Initialise, _INR (0, 3) | _OUT (1), 310, 0x4B534154, "PollIdle test", 0, &task))
    return 8;
  for (int i = 0; i < 3; i++)
    {
      int h = 0;
      if (!_swix (OS_Find, _INR (0, 1) | _OUT (0), 0x83, "<Wimp$ScrapDir>.PollIdleT", &h) && h)
	_swix (OS_Find, _INR (0, 1), 0, h);
      unsigned t0 = now (), d;
      int reason;
      do
	_swix (Wimp_PollIdle, _INR (0, 2) | _OUT (0), 0x3830, blk, t0 + 50, &reason);
      while (reason != 0);
      d = now () - t0;
      printf ("null event after %u cs\n", d);
      if (d < 40 || d > 300)
	ok = 0;
    }
  _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
  return ok ? 7 : 8;
}
