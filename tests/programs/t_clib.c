/* t_clib: in the desktop (HardDisc4's !Boot), the SharedCLibrary loaded
 * must be the one !System asks for (6.19 or later: HardDisc4 5.30 has
 * 6.22), and a program that uses it (!Boot's BootVars) must run and end.
 * Exit status 0 if so; 1 if the module is older; 2 if BootVars failed.
 * (No output: in the desktop that would open a window waiting for a key.)
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

int
main (void)
{
  const char *base;
  if (_swix (OS_Module, _INR (0, 1) | _OUT (3), 18, "SharedCLibrary", &base))
    return 1;
  const char *help = base + *(const unsigned *) (base + 0x14);
  const char *tab = strchr (help, '\t');
  while (tab && *tab == '\t')
    tab++;
  int major = 0, minor = 0;
  if (!tab || sscanf (tab, "%d.%d", &major, &minor) != 2)
    return 1;
  if (major * 100 + minor < 619)
    return 1;
  return system ("Boot:Utils.BootVars") == 0 ? 0 : 2;
}
