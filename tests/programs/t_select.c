/* t_select: Socket_Select's timeout, with the Internet module's own SWIs
 * (not UnixLib's select).
 *
 * 1. A UDP socket bound to 127.0.0.1 with nothing sent to it: select for
 *    reading with a 0.5 s and then a 1 s timeout must return 0 after about
 *    that long. LinuxInternet before 5.31-4 never returned: once the
 *    timeout's ticker fired, the pollword stayed set, so the wait loop
 *    spun at full speed checking sockets that never became ready (the
 *    machine hung in TubeSearch/!REEL until Ctrl+Break).
 * 2. A 5 ms timeout (under a centisecond) and a zero timeout (a poll) both
 *    return 0 promptly.
 * 3. After sending the socket a datagram, select returns 1 at once and
 *    leaves the socket's bit set (skipped where there's no loopback
 *    interface, as in the native test VM).
 *
 * Prints what it checked, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel.h>
#include <swis.h>

#define S_Creat       0x41200
#define S_Bind        0x41201
#define S_Sendto      0x41209
#define S_Getsockname 0x4120F
#define S_Close       0x41210
#define S_Select      0x41211

struct sin { uint8_t len, family; uint16_t port; uint32_t addr; char zero[8]; };
struct tv { long sec, usec; };

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static unsigned
now (void)
{
  unsigned t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  return t;
}

/* select for reading on S with a timeout of SEC, USEC; the result, the
   time it took (cs) and whether S's bit is still set */
static int
sel (int s, long sec, long usec, unsigned *took, int *isset)
{
  uint32_t rd[8] = { 0 };
  struct tv t = { sec, usec };
  int n = -1;
  rd[s / 32] |= 1u << (s % 32);
  unsigned t0 = now ();
  _kernel_oserror *e = _swix (S_Select, _INR (0, 4) | _OUT (0), s + 1, rd, 0, 0, &t, &n);
  *took = now () - t0;
  *isset = !!(rd[s / 32] & (1u << (s % 32)));
  if (e)
    printf ("select error &%X %s\n", e->errnum, e->errmess);
  return e ? -1 : n;
}

int
main (void)
{
  int s = -1;
  CHECK (!_swix (S_Creat, _INR (0, 2) | _OUT (0), 2, 2, 0, &s));
  if (s < 0)
    {
      printf ("FAIL\n");
      return 1;
    }
  struct sin a = { 16, 2, 0, 0x0100007F, { 0 } };
  _kernel_oserror *be = _swix (S_Bind, _INR (0, 2), s, &a, 16);
  /* the native test VM's job init leaves the loopback interface down:
     then only the timeouts are tested */
  int noloop = be && be->errnum == 0x20E31;
  if (be)
    printf ("Socket_Bind error &%X %s%s\n", be->errnum, be->errmess,
	    noloop ? " (no loopback here: the datagram check is skipped)" : "");
  CHECK (!be || noloop);
  int alen = 16;
  CHECK (!_swix (S_Getsockname, _INR (0, 2), s, &a, &alen));
  printf ("socket %d on port %u\n", s, (unsigned) ((a.port >> 8) | ((a.port & 0xFF) << 8)));

  unsigned took;
  int set, n;
  static const struct { long sec, usec; unsigned lo, hi; } c[] = {
    { 0, 500000, 45, 80 },
    { 1, 0, 95, 140 },
    { 0, 5000, 0, 40 },
    { 0, 0, 0, 5 },
  };
  for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++)
    {
      n = sel (s, c[i].sec, c[i].usec, &took, &set);
      int ok = n == 0 && !set && took >= c[i].lo && took <= c[i].hi;
      printf ("nothing sent, timeout %ld.%06ld s: %d ready, %u cs %s\n",
	      c[i].sec, c[i].usec, n, took, ok ? "ok" : "WRONG");
      if (!ok)
	fails++;
    }

  /* now with a datagram waiting */
  if (!noloop)
    {
      _kernel_oserror *e = _swix (S_Sendto, _INR (0, 5), s, "x", 1, 0, &a, 16);
      if (e)
        printf ("Socket_Sendto error &%X %s\n", e->errnum, e->errmess);
      CHECK (!e);
      n = sel (s, 5, 0, &took, &set);
      printf ("datagram sent, timeout 5 s: %d ready, %u cs, bit %s\n", n, took, set ? "set" : "clear");
      CHECK (n == 1 && set && took < 50);
    }

  _swix (S_Close, _IN (0), s);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
