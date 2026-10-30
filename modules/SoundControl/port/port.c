/* port.c: what RISC OS Open's SoundControl needs from the C library and
   CMHG, for the test bed's freestanding C module framework (modules/common):
   _swix, malloc/free from the RMA, a little of stdio, stdlib and string,
   the cmodule.inc entry points mapped to CMHG's, and mixer_getmix (a mixer
   device's GetMix returns its result in r0 and r1, Norcroft's
   __value_in_regs, which GCC can't declare).
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#include <stdarg.h>
#include "cmodule.h"
#include "swis.h"
#include "stdint.h"
#include "Interface/MixerDevice.h"
#include "modhdr.h"

/* --- SWIs --------------------------------------------------------------- */

_kernel_oserror *
_swix (int swi, unsigned mask, ...)
{
  regs r;
  va_list ap;
  va_start (ap, mask);
  for (int i = 0; i < 10; i++)
    r.r[i] = mask & (1U << i) ? va_arg (ap, unsigned) : 0;
  _kernel_oserror *e = (_kernel_oserror *) swix_regs ((unsigned) swi, &r);
  if (!e)
    for (int i = 0; i < 10; i++)
      if (mask & (1U << (31 - i)))
	*va_arg (ap, unsigned *) = r.r[i];
  va_end (ap);
  return e;
}

/* --- C library ------------------------------------------------------------ */

void *
malloc (size_t n)
{
  return rma_claim (n);
}

void
free (void *p)
{
  if (p)
    rma_free (p);
}

char *
strncpy (char *d, const char *s, size_t n)
{
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
  return d;
}

static int
space (int c)
{
  return c == ' ' || (c >= 9 && c <= 13);
}

int
atoi (const char *s)
{
  int v = 0, neg = 0;
  while (space (*s))
    s++;
  if (*s == '-' || *s == '+')
    neg = *s++ == '-';
  while (*s >= '0' && *s <= '9')
    v = v * 10 + (*s++ - '0');
  return neg ? -v : v;
}

/* sprintf: %s, %d, %u, %X and %02X (all SoundControl uses) */
int
sprintf (char *buf, const char *fmt, ...)
{
  va_list ap;
  char *o = buf;
  va_start (ap, fmt);
  for (; *fmt; fmt++)
    {
      if (*fmt != '%' || !fmt[1])
	{
	  *o++ = *fmt;
	  continue;
	}
      fmt++;
      int width = 0;
      if (*fmt == '0')
	fmt++;
      while (*fmt >= '0' && *fmt <= '9')
	width = width * 10 + (*fmt++ - '0');
      if (*fmt == 's')
	for (const char *s = va_arg (ap, const char *); *s; s++)
	  *o++ = *s;
      else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'X')
	{
	  unsigned v = va_arg (ap, unsigned), base = *fmt == 'X' ? 16 : 10;
	  if (*fmt == 'd' && (int) v < 0)
	    {
	      *o++ = '-';
	      v = 0U - v;
	    }
	  char t[12];
	  int n = 0;
	  do
	    t[n++] = "0123456789ABCDEF"[v % base];
	  while (v /= base);
	  while (n < width)
	    t[n++] = '0';
	  while (n)
	    *o++ = t[--n];
	}
      else
	*o++ = *fmt;
    }
  va_end (ap);
  *o = 0;
  return (int) (o - buf);
}

/* sscanf: %d, %u and %c, with spaces in the format matching any spaces */
int
sscanf (const char *s, const char *fmt, ...)
{
  va_list ap;
  int n = 0;
  va_start (ap, fmt);
  for (; *fmt; fmt++)
    {
      if (space (*fmt))
	{
	  while (space (*s))
	    s++;
	  continue;
	}
      if (*fmt != '%')
	{
	  if (*s++ != *fmt)
	    break;
	  continue;
	}
      fmt++;
      if (*fmt == 'c')
	{
	  if (!*s)
	    break;
	  *va_arg (ap, char *) = *s++;
	  n++;
	  continue;
	}
      while (space (*s))
	s++;
      int neg = 0;
      if (*fmt == 'd' && (*s == '-' || *s == '+'))
	neg = *s++ == '-';
      if (*s < '0' || *s > '9')
	break;
      unsigned v = 0;
      while (*s >= '0' && *s <= '9')
	v = v * 10 + (unsigned) (*s++ - '0');
      if (*fmt == 'd')
	*va_arg (ap, int *) = neg ? -(int) v : (int) v;
      else
	*va_arg (ap, unsigned *) = v;
      n++;
    }
  va_end (ap);
  return n;
}

/* --- mixer devices --------------------------------------------------------- */

struct mixer_device_channel_state
mixer_getmix (struct mixer_device *d, uint32_t channel)
{
  uint64_t v = d->GetMix (d, channel);
  struct mixer_device_channel_state s;
  s.mute = (v & 1) != 0;
  s.gain = (int32_t) (uint32_t) (v >> 32);
  return s;
}

/* --- cmodule.inc's entry points, mapped to CMHG's ---------------------- */

static void *pw_saved;

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  pw_saved = pw;
  return (oserror *) module_Init (tail, instance, pw);
}

oserror *
mod_final (int fatal)
{
  return (oserror *) module_Final (fatal, 0, pw_saved);
}

oserror *
mod_swi (unsigned number, regs *r)
{
  return (oserror *) module_SWI ((int) number, (_kernel_swi_regs *) r, pw_saved);
}

void
mod_service (regs *r)
{
  module_Service ((int) r->r[1], (_kernel_swi_regs *) r, pw_saved);
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  return (oserror *) module_Commands (tail, argc, index, pw_saved);
}

/* libgcc's integer division calls raise(SIGFPE) on division by zero */
int raise (int sig);
int
raise (int sig)
{
  (void) sig;
  return 0;
}
