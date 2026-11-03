/* Floating point in several threads at once: the threads are switched by
   PThreadTicker's callbacks, and VFPSupport switches their VFP contexts.
   Each thread runs a calculation that keeps many double registers live
   (D0-D31 where the CPU has them) under its own rounding mode; the results
   must match the same calculations run one at a time. */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <fenv.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define THREADS 4
#define LANES 24
#define ITER 400000

static const int modes[THREADS] = { FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD };
static double together[THREADS], alone[THREADS];
static volatile int go;

static double
calc (int id)
{
  double a[LANES];
  fesetround (modes[id]);
  for (int l = 0; l < LANES; l++)
    a[l] = id + l * 0.37 + 1.0;
  for (long i = 0; i < ITER; i++)
    {
#pragma GCC unroll 24
      for (int l = 0; l < LANES; l++)
	a[l] = a[l] * 1.0000001 + 0.3333333 / (l + 1);
      if ((i & 0x3FFF) == 0)
	sched_yield ();
    }
  double s = 0;
  for (int l = 0; l < LANES; l++)
    s += a[l];
  if (fegetround () != modes[id])
    s = -1;			/* the rounding mode was lost */
  return s;
}

static void *
thread (void *p)
{
  int id = (int) (long) p;
  while (!go)
    sched_yield ();
  together[id] = calc (id);
  return NULL;
}

int
main (void)
{
  pthread_t t[THREADS];
  for (int i = 0; i < THREADS; i++)
    alone[i] = calc (i);
  fesetround (FE_TONEAREST);
  for (int i = 0; i < THREADS; i++)
    pthread_create (&t[i], NULL, thread, (void *) (long) i);
  go = 1;
  double mine = calc (0);	/* the main thread too */
  for (int i = 0; i < THREADS; i++)
    pthread_join (t[i], NULL);
  int bad = memcmp (&mine, &alone[0], sizeof mine) != 0;
  for (int i = 0; i < THREADS; i++)
    {
      int ok = memcmp (&together[i], &alone[i], sizeof (double)) == 0;
      printf ("thread %d: %.17g %s\n", i, together[i], ok ? "ok" : "DIFFERS");
      bad |= !ok;
    }
  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
