/* port.c: what ROOL's WindowScroll needs from the C library, RISC_OSLib
   and CMHG, for the test bed's freestanding C module framework
   (modules/common): _swix/_swi, a few string and stdio functions, malloc
   from the RMA, exit/atexit for the module's task, the RISC_OSLib Wimp and
   MessageTrans calls it makes, and the cmodule.inc entry points mapped to
   CMHG's. Copyright (c) 2026 Andrew Youll. MIT licence. */
#include <stdarg.h>
#include "cmodule.h"
#include "swis.h"
#include "wimp.h"
#include "msgtrans.h"
#include "modhead.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

/* --- SWIs ------------------------------------------------------------- */

static _kernel_oserror *
swi_masked (int swi, unsigned mask, va_list ap, regs *r)
{
  for (int i = 0; i < 10; i++)
    r->r[i] = mask & (1U << i) ? va_arg (ap, unsigned) : 0;
  _kernel_oserror *e = (_kernel_oserror *) swix_regs ((unsigned) swi, r);
  if (!e)
    for (int i = 0; i < 10; i++)
      if (mask & (1U << (31 - i)))
        *va_arg (ap, unsigned *) = r->r[i];
  return e;
}

_kernel_oserror *
_swix (int swi, unsigned mask, ...)
{
  regs r;
  va_list ap;
  va_start (ap, mask);
  _kernel_oserror *e = swi_masked (swi, mask, ap, &r);
  va_end (ap);
  return e;
}

/* (errors are ignored: this is only used for OS_ReadMonotonicTime) */
int
_swi (int swi, unsigned mask, ...)
{
  regs r;
  va_list ap;
  va_start (ap, mask);
  swi_masked (swi, mask & ~0xF0000U, ap, &r);
  va_end (ap);
  return (int) r.r[(mask >> 16) & 0xF];
}

/* --- C library --------------------------------------------------------- */

int
memcmp (const void *a, const void *b, size_t n)
{
  const unsigned char *p = a, *q = b;
  for (; n; n--, p++, q++)
    if (*p != *q)
      return *p - *q;
  return 0;
}

int
strcmp (const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return (unsigned char) *a - (unsigned char) *b;
}

char *
strcpy (char *d, const char *s)
{
  char *r = d;
  while ((*d++ = *s++) != 0)
    ;
  return r;
}

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
      if (*fmt == 'd')
        {
          int v = va_arg (ap, int);
          char t[12];
          int n = 0;
          unsigned u = v < 0 ? 0U - (unsigned) v : (unsigned) v;
          do
            t[n++] = (char) ('0' + u % 10);
          while (u /= 10);
          if (v < 0)
            *o++ = '-';
          while (n)
            *o++ = t[--n];
        }
      else if (*fmt == 's')
        for (const char *s = va_arg (ap, const char *); *s; s++)
          *o++ = *s;
      else
        *o++ = *fmt;
    }
  va_end (ap);
  *o = 0;
  return (int) (o - buf);
}

int
puts (const char *s)
{
  _swix (OS_Write0, _IN (0), s);
  _swix (OS_NewLine, 0);
  return 0;
}

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

static void (*exit_fn) (void);

int
atexit (void (*fn) (void))
{
  exit_fn = fn;
  return 0;
}

void
exit (int status)
{
  if (exit_fn)
    exit_fn ();
  /* OS_Exit with "ABEX" and the return code */
  regs r = { { 0, status ? 0x58454241 : 0, (unsigned) status } };
  swix_regs (OS_Exit, &r);
  for (;;)
    ;
}

/* --- RISC_OSLib ---------------------------------------------------------- */

os_error *
wimp_get_caret_pos (wimp_caretstr *c)
{
  return _swix (Wimp_GetCaretPosition, _IN (1), c);
}

os_error *
wimp_get_point_info (wimp_mousestr *m)
{
  return _swix (Wimp_GetPointerInfo, _IN (1), m);
}

os_error *
wimp_get_wind_state (wimp_w w, wimp_wstate *s)
{
  s->o.w = w;
  return _swix (Wimp_GetWindowState, _IN (1), s);
}

os_error *
wimp_get_wind_info (wimp_winfo *i)
{
  return _swix (Wimp_GetWindowInfo, _IN (1), i);
}

os_error *
wimp_get_icon_info (wimp_w w, wimp_i i, wimp_icon *icon)
{
  struct { wimp_w w; wimp_i i; wimp_icon icon; } b;
  b.w = w;
  b.i = i;
  os_error *e = _swix (Wimp_GetIconState, _IN (1), &b);
  if (!e)
    memcpy (icon, &b.icon, sizeof *icon);
  return e;
}

os_error *
wimp_sendwmessage (wimp_etype code, wimp_msgstr *m, wimp_w w, wimp_i i)
{
  return _swix (Wimp_SendMessage, _INR (0, 3), code, m, w, i);
}

os_error *
wimp_open_wind (wimp_openstr *o)
{
  return _swix (Wimp_OpenWindow, _IN (1), o);
}

os_error *
wimp_taskclose (wimp_t t)
{
  return _swix (Wimp_CloseDown, _INR (0, 1), t, 0x4B534154);
}

os_error *
wimp_reporterror (os_error *e, wimp_errflags flags, char *name)
{
  return _swix (Wimp_ReportError, _INR (0, 2), e, flags, name);
}

_kernel_oserror *
msgtrans_open_file (msgtrans_control_block *cb, const char *name, char *buffer)
{
  return _swix (MessageTrans_OpenFile, _INR (0, 2), cb, name, buffer);
}

_kernel_oserror *
msgtrans_close_file (msgtrans_control_block *cb)
{
  return _swix (MessageTrans_CloseFile, _IN (0), cb);
}

/* --- cmodule.inc's entry points, mapped to CMHG's ---------------------- */

extern void *private_word;      /* cmodule.c (ROOL's) sets it in module_init */
static void *pw_saved;

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  pw_saved = pw;
  return (oserror *) module_init (tail, instance, pw);
}

oserror *
mod_final (int fatal)
{
  return (oserror *) module_final (fatal, 0, pw_saved);
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number, (void) r;
  return 0;
}

void
mod_service (regs *r)
{
  module_service ((int) r->r[1], (_kernel_swi_regs *) r, pw_saved);
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  return (oserror *) module_commands (tail, argc, index, pw_saved);
}

/* The module's task (OS_Module Enter, from *Desktop_WindowScroll): header.s
   sets up the stack at the top of the Wimp slot and calls this. */
extern int main (int argc, char **argv);

void
ws_start (void)
{
  exit (main (0, 0));
}

/* libgcc's integer division calls raise(SIGFPE) on division by zero: the
   result is then 0, as from the hardware divide instruction */
int raise (int sig);
int
raise (int sig)
{
  (void) sig;
  return 0;
}
