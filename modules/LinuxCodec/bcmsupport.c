/* BCMSupport: a stand-in for the Raspberry Pi's BCMSupport module (the
   VideoCore firmware's mailbox property interface), for RISC OS for Linux
   running natively on a Raspberry Pi.

   Linux owns the mailbox; it passes property buffers to the firmware for
   programs through /dev/vcio (IOCTL_MBOX_PROPERTY). The buffers are the
   firmware's own format, the same as on RISC OS: +0 size, +4 code (0 to
   send; 0x80000000 back for success), then the tags, ending with 0.

   SWIs (chunk &591C0, as RISC OS's BCMSupport; only these are answered):
     2  AllocPropertyBuffer     r0 = length -> r0 -> buffer (16 byte aligned)
     3  FreePropertyBuffer      r0 -> buffer
     4  SendPropertyBuffer      r0 -> buffer, r1 = flags (bit 0, the
                                response by callback, isn't provided)
     5  SendTempPropertyBuffer  r0 -> request, r1 -> where the response
                                goes (may be the same)

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "rolsys.h"

#define IOCTL_MBOX_PROPERTY	IOC (3, 100, 0, 4)	/* _IOWR(100, 0, compat_uptr_t) */
#define MAX_BUFFER		(64 * 1024)

ERROR_BLOCK (not_native, 0x823620, "BCMSupport (RISC OS for Linux) needs RISC OS running natively on a Raspberry Pi");
ERROR_BLOCK (no_vcio, 0x823621, "Linux has no VideoCore mailbox (/dev/vcio): this needs a Raspberry Pi");
ERROR_BLOCK (no_memory, 0x823622, "BCMSupport: not enough memory");
ERROR_BLOCK (bad_buffer, 0x823623, "BCMSupport: bad property buffer");
ERROR_BLOCK (failed, 0x823624, "BCMSupport: the firmware didn't take the property buffer");
ERROR_BLOCK (no_callback, 0x823625, "BCMSupport_SendPropertyBuffer: responses by callback aren't provided under RISC OS for Linux");
ERROR_BLOCK (not_here, 0x823626, "This BCMSupport SWI isn't provided under RISC OS for Linux");
ERROR_BLOCK (bad_swi, 0x1E6, "Unknown BCMSupport SWI");

static int vcio = -1;

static oserror *
send (unsigned *buf)
{
  unsigned size = buf[0];
  if (size < 12 || size > MAX_BUFFER || (size & 3))
    return ERR (bad_buffer);
  if (vcio < 0 && (vcio = lopen ("/dev/vcio")) < 0)
    {
      vcio = -1;
      return ERR (no_vcio);
    }
  int e;
  do
    e = lioctl (vcio, IOCTL_MBOX_PROPERTY, buf);
  while (e == -EINTR);
  return e < 0 ? ERR (failed) : 0;
}

static oserror *
alloc (regs *r)
{
  unsigned n = r->r[0];
  if (!n || n > MAX_BUFFER)
    return ERR (bad_buffer);
  unsigned char *p = rma_claim (n + 32);
  if (!p)
    return ERR (no_memory);
  unsigned char *a = (unsigned char *) (((unsigned) p + 4 + 15) & ~15u);
  ((void **) a)[-1] = p;
  r->r[0] = (unsigned) a;
  return 0;
}

static void
release (void *a)
{
  if (a)
    rma_free (((void **) a)[-1]);
}

static oserror *
send_temp (regs *r)
{
  unsigned *in = (unsigned *) r->r[0], *out = (unsigned *) r->r[1];
  unsigned size = in[0];
  if (size < 12 || size > MAX_BUFFER || (size & 3))
    return ERR (bad_buffer);
  if (!out)
    out = in;
  regs a = {{ size }};
  oserror *e = alloc (&a);
  if (e)
    return e;
  unsigned *t = (unsigned *) a.r[0];
  memcpy (t, in, size);
  e = send (t);
  if (!e)
    memcpy (out, t, size);
  release (t);
  return e;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 2: return alloc (r);
    case 3: release ((void *) r->r[0]); return 0;
    case 4:
      if (r->r[1] & 1)
	return ERR (no_callback);
      return send ((unsigned *) r->r[0]);
    case 5: return send_temp (r);
    }
  return number < 64 ? ERR (not_here) : ERR (bad_swi);
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  vcio = -1;
  if (!rol_native ())
    return ERR (not_native);
  if ((vcio = lopen ("/dev/vcio")) < 0)
    {
      vcio = -1;
      return ERR (no_vcio);
    }
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  if (vcio >= 0)
    lclose (vcio);
  vcio = -1;
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}
