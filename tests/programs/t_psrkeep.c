/* t_psrkeep: the user state a thread switch or interrupt must keep. Three
   threads run long blocks (ARM and Thumb-2) that never change the flags
   or the registers they hold: each block sets NZCV, GE, r5-r10 and
   d8-d9 to a pattern, then checks them hundreds of times while UnixLib's
   ticker switches threads under it. Thumb blocks also run IT blocks whose
   outcome depends on the flags (ITSTATE kept across an interrupt). Any
   difference is counted and the first few printed.

   With a second argument of 1 the main thread also sends the workers
   SIGUSR1 between its blocks (a handler that uses the flags and VFP).

   Prints PASS if nothing changed, FAIL otherwise.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* r0 = APSR pattern (NZCV in 31..28, GE in 19..16), r1 = register seed.
   Returns the OR of all differences seen (0 = all kept). */
unsigned blk_arm (unsigned psr, unsigned seed);
unsigned blk_thumb (unsigned psr, unsigned seed);

#define CHECK_BODY \
  "mrs r3, APSR\n\t" \
  "eor r3, r3, r12\n\t" \
  "and r3, r3, r11\n\t" \
  "orr r4, r4, r3\n\t" \
  "eor r3, r5, r1\n\t" \
  "orr r4, r4, r3\n\t" \
  "add r3, r1, #1\n\t" \
  "eor r3, r3, r6\n\t" \
  "orr r4, r4, r3\n\t" \
  "add r3, r1, #2\n\t" \
  "eor r3, r3, r7\n\t" \
  "orr r4, r4, r3\n\t" \
  "add r3, r1, #3\n\t" \
  "eor r3, r3, r8\n\t" \
  "orr r4, r4, r3\n\t" \
  "add r3, r1, #4\n\t" \
  "eor r3, r3, r9\n\t" \
  "orr r4, r4, r3\n\t" \
  "add r3, r1, #5\n\t" \
  "eor r3, r3, r10\n\t" \
  "orr r4, r4, r3\n\t" \
  "vmov r3, s16\n\t" \
  "eor r3, r3, r1\n\t" \
  "orr r4, r4, r3\n\t" \
  "vmov r3, s19\n\t" \
  "eor r3, r3, r0\n\t" \
  "orr r4, r4, r3\n\t"

#define PROLOGUE \
  "push {r4-r11, lr}\n\t" \
  "vpush {d8-d9}\n\t" \
  "mov r12, r0\n\t" \
  "movw r11, #0\n\t" \
  "movt r11, #0xf00f\n\t" /* NZCV and GE */ \
  "mov r4, #0\n\t" \
  "mov r5, r1\n\t" \
  "add r6, r1, #1\n\t" \
  "add r7, r1, #2\n\t" \
  "add r8, r1, #3\n\t" \
  "add r9, r1, #4\n\t" \
  "add r10, r1, #5\n\t" \
  "vmov s16, r1\n\t" \
  "vmov s19, r0\n\t" \
  "msr APSR_nzcvqg, r0\n\t"

#define EPILOGUE \
  "mov r0, r4\n\t" \
  "vpop {d8-d9}\n\t" \
  "pop {r4-r11, pc}\n\t"

__asm__ (
  ".syntax unified\n\t"
  ".arm\n\t"
  ".text\n\t"
  ".align 2\n\t"
  ".global blk_arm\n\t"
  ".type blk_arm, %function\n"
  "blk_arm:\n\t"
  PROLOGUE
  ".rept 400\n\t"
  CHECK_BODY
  ".endr\n\t"
  EPILOGUE
  ".size blk_arm, .-blk_arm\n\t"

  ".thumb\n\t"
  ".align 2\n\t"
  ".global blk_thumb\n\t"
  ".type blk_thumb, %function\n\t"
  ".thumb_func\n"
  "blk_thumb:\n\t"
  PROLOGUE
  ".rept 300\n\t"
  CHECK_BODY
  /* ITSTATE: with Z as the pattern says, exactly one of these runs */
  "ite eq\n\t"
  "moveq r3, #1\n\t"
  "movne r3, #2\n\t"
  "tst r12, #0x40000000\n\t"     /* sets flags: restore them after */
  "ite ne\n\t"
  "eorne r3, r3, #1\n\t"
  "eoreq r3, r3, #2\n\t"
  "msr APSR_nzcvqg, r12\n\t"
  "orr r4, r4, r3\n\t"
  ".endr\n\t"
  EPILOGUE
  ".size blk_thumb, .-blk_thumb\n\t"
  ".arm\n\t"
);

static volatile int stop;
static volatile unsigned long sigs;
static volatile double sink;

/* a signal handler that changes the flags, the registers and VFP */
static void
on_sig (int sig)
{
  (void) sig;
  double a = 1.5;
  for (int i = 0; i < 50; i++)
    a = a * 1.000001 + (i & 1 ? 0.25 : -0.25);
  sink = a;
  sigs++;
}
static volatile unsigned long errors, blocks;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void
run (int id)
{
  unsigned n = 0;
  while (!stop)
    {
      unsigned psr = ((n & 15u) << 28) | (((n >> 4) & 15u) << 16);
      unsigned seed = 0x1000u * (unsigned) id + n * 7u;
      unsigned d = (n & 1) ? blk_thumb (psr, seed) : blk_arm (psr, seed);
      pthread_mutex_lock (&lock);
      blocks++;
      if (d)
	{
	  if (errors < 10)
	    printf ("thread %d: %s block psr=%08X seed=%08X diff=%08X\n", id,
		    (n & 1) ? "Thumb" : "ARM", psr, seed, d);
	  errors++;
	}
      pthread_mutex_unlock (&lock);
      n++;
    }
}

static void *
worker (void *arg)
{
  run ((int) (long) arg);
  return NULL;
}

int
main (int argc, char **argv)
{
  int secs = argc > 1 ? atoi (argv[1]) : 20;
  int with_sig = argc > 2 && atoi (argv[2]);
  pthread_t t[2];
  signal (SIGUSR1, on_sig);
  for (long i = 0; i < 2; i++)
    pthread_create (&t[i], NULL, worker, (void *) (i + 1));
  time_t end = time (NULL) + secs;
  while (time (NULL) < end)
    {
      /* the main thread's own blocks, a few at a time */
      if (with_sig)
	for (int i = 0; i < 2; i++)
	  pthread_kill (t[i], SIGUSR1);
      unsigned psr = (unsigned) (time (NULL) & 15) << 28;
      unsigned d = blk_thumb (psr, 0x55aa0000u) | blk_arm (psr, 0x55aa0000u);
      pthread_mutex_lock (&lock);
      blocks += 2;
      if (d)
	{
	  if (errors < 10)
	    printf ("main: psr=%08X diff=%08X\n", psr, d);
	  errors++;
	}
      pthread_mutex_unlock (&lock);
    }
  stop = 1;
  for (int i = 0; i < 2; i++)
    pthread_join (t[i], NULL);
  printf ("%lu blocks, %lu with a difference, %lu signals handled\n", blocks,
	  errors, sigs);
  puts (errors ? "FAIL" : "PASS");
  return errors != 0;
}
