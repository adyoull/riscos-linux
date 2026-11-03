/* Two threads. Known not to work on the Linux port (see README): UnixLib's
   thread switcher uses privileged instructions the port runs in user mode.
   tests/run.sh expects this one to time out, and says so if it ever passes. */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

static void *th (void *p) { printf ("in the thread\n"); return p; }

int
main (void)
{
  pthread_t t;
  void *r = NULL;
  pthread_create (&t, NULL, th, (void *) 42);
  sched_yield ();
  pthread_join (t, &r);
  printf ("%s\n", r == (void *) 42 ? "PASS" : "FAIL");
  return r != (void *) 42;
}
