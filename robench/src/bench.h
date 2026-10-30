/* ROBench: a CPU, memory, OS and graphics benchmark for RISC OS.
   Copyright 2026 Andrew Youll. MIT licence (see LICENCE). */
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stddef.h>

/* A test: run(iters) does the work iters times and returns a checksum
   that depends on every result (so nothing can be optimised away, and a
   wrong answer shows). work is the amount of `unit' one iteration does. */
typedef struct test {
  const char *id;               /* e.g. "int.sha256" */
  const char *name;             /* what it is, for the table */
  const char *unit;             /* e.g. "MB/s"; "ns" for latency */
  double work;                  /* units per iteration */
  int (*setup) (void);          /* 0 ok; may be NULL */
  uint32_t (*run) (uint32_t iters);
  int (*verify) (char *msg, size_t len); /* 1 ok; may be NULL */
  void (*teardown) (void);      /* may be NULL */
  int flags;
} test_t;

#define T_LOWER_BETTER 1        /* the metric is time per unit */
#define T_RISCOS       2        /* RISC OS only */
#define T_GFX          4        /* needs the screen (-g) */
#define T_IO           8        /* file system (-io / default on) */
#define T_NOCAL       16        /* fixed iteration count (expensive setup) */

extern const test_t cpu_tests[];
extern const size_t n_cpu_tests;
extern const test_t neon_tests[];
extern const size_t n_neon_tests;
const char *neon_mode (void);
void neon_disable (void);
extern const test_t os_tests[];
extern const size_t n_os_tests;

/* helpers shared by the test files */
double now_seconds (void);
uint32_t rng32 (uint32_t *s);
void make_text (unsigned char *buf, size_t len, uint32_t seed);
uint32_t crc32_buf (uint32_t crc, const void *p, size_t n);
extern const char *g_scratch;   /* RISC OS directory for the file tests */

#endif
