/* A program that dies: a store to an address nothing is mapped at. UnixLib
   should report the signal, and the run should end with a failure status
   rather than hang. */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>

int
main (void)
{
  printf ("about to crash\n");
  fflush (stdout);
  *(volatile int *) 0xE8000000 = 1;
  printf ("still here\n");
  return 0;
}
