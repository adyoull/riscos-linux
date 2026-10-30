/* VCHIQ: a stand-in for the Raspberry Pi's VCHIQ module (the VideoCore's
   message queues), for RISC OS for Linux running natively on a Pi 3 or 4.

   RISC OS's own VCHIQ module drives the VideoCore's doorbells and shared
   memory itself. Under Linux, Linux's VCHIQ driver has them, and gives
   programs the same queues through /dev/vchiq: this module answers the
   RISC OS VCHIQ SWIs that programs use (ReelHWAccel's vcdec and vcenc:
   Reel, Matinee, FFmpeg's h264_vchiq) with Linux's /dev/vchiq ioctls.

   SWIs (chunk &59200, as RISC OS's VCHIQ; only these offsets are answered):
     0  Initialise         -> r0 = instance
     1  Connect            r2 = instance
     2  Disconnect         r0 = instance (its services are closed)
     3  BulkQueueTransmit  r0 = service, r1 -> data, r2 = size, r3 = flags,
                           r4 = bulk user data
     4  MsgDequeue         r0 = service, r1 -> buffer, r2 = its size
                           -> r2 = the message's size, 0 if none waiting
     5  MsgQueue           r0 = service, r1 -> message, r2 = its size
     9  ServiceClose       r0 = service
     10 ServiceOpen        r0 = instance, r1 -> setup block:
                           +0 version, +4 version_min, +8 fourcc,
                           +24 callback code, +28 its R0
                           -> r0 = service
     13 ServiceUse, 14 ServiceRelease   r0 = service (nothing to do: the
                           Linux driver keeps the VideoCore awake)
     15 BulkQueueReceive   r0 = service, r1 -> buffer, r2 = size,
                           r3 = flags, r4 = bulk user data

   Bulk transfers are done before the SWI returns (Linux's "blocking" mode).
   ReelHWAccel queues them after the message that says the data's coming
   (a receive), or after the message that asks for it (a transmit), so the
   VideoCore's side is already queued and the transfer is quick. After a
   receive with a callback asked for (flags bit 1), the service's callback
   is called (in SVC mode) with R0 = its R0 from the setup block, R1 = 4
   (received) or 18 (aborted), R2 = the service, R3 = the bulk user data,
   before the SWI returns.

   Linux's driver works with the program's own (virtual) addresses, so
   buffers needn't be physically contiguous. The VideoCore's shared memory
   service ("SMEM", for the VideoCore to write straight into RISC OS
   memory) can't be had - it takes physical addresses - so ServiceOpen
   refuses it: ReelHWAccel then has every picture sent by bulk transfer.

   Linux's VCHIQ ioctls, 32-bit forms (vchiq_dev.c's compat ioctls).

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "rolsys.h"

#define VCHIQ_IOC	0xC4
struct create_service32 { int fourcc; unsigned callback, userdata; short version, version_min; int is_open, is_vchi; unsigned handle; };
struct element32 { unsigned data, size; };
struct queue_message32 { unsigned handle, count, elements; };
struct bulk32 { unsigned handle, data, size, userdata; int mode; };
struct dequeue32 { unsigned handle; int blocking; unsigned bufsize, buf; };
typedef char check_create[sizeof (struct create_service32) == 28 ? 1 : -1];
typedef char check_bulk[sizeof (struct bulk32) == 20 ? 1 : -1];

#define IOC_CONNECT		IOC (0, VCHIQ_IOC, 0, 0)
#define IOC_CREATE_SERVICE	IOC (3, VCHIQ_IOC, 2, sizeof (struct create_service32))
#define IOC_REMOVE_SERVICE	IOC (0, VCHIQ_IOC, 3, 0)
#define IOC_QUEUE_MESSAGE	IOC (1, VCHIQ_IOC, 4, sizeof (struct queue_message32))
#define IOC_BULK_TRANSMIT	IOC (3, VCHIQ_IOC, 5, sizeof (struct bulk32))
#define IOC_BULK_RECEIVE	IOC (3, VCHIQ_IOC, 6, sizeof (struct bulk32))
#define IOC_DEQUEUE_MESSAGE	IOC (3, VCHIQ_IOC, 8, sizeof (struct dequeue32))
#define BULK_MODE_BLOCKING	1
#define EWOULDBLOCK		EAGAIN
#define VCHI_CALLBACK_WHEN_OP_COMPLETE 2u
#define VCHI_CALLBACK_WHEN_DATA_READ   32u
#define REASON_RECEIVED		4
#define REASON_RECEIVE_ABORTED	18
#define FOURCC_SMEM		0x534D454Du	/* 'SMEM' */

typedef struct instance
{
  struct instance *next;
  unsigned magic;
  int fd;
  int connected;
} instance;
#define MAGIC	0x51484356u	/* "VCHQ" */

typedef struct service
{
  instance *in;			/* 0: the slot's free */
  unsigned handle;		/* Linux's */
  unsigned callback, cb_r0;
} service;
#define SERVICES	32

static instance *instances;
static service services[SERVICES];
static unsigned transfers, failures;

ERROR_BLOCK (not_native, 0x81A40, "VCHIQ (RISC OS for Linux) needs RISC OS running natively on a Raspberry Pi 3 or 4");
ERROR_BLOCK (no_vchiq, 0x81A41, "Linux has no VCHIQ (/dev/vchiq): this needs a Raspberry Pi 3 or 4");
ERROR_BLOCK (bad_instance, 0x81A42, "Bad VCHIQ instance");
ERROR_BLOCK (bad_service, 0x81A43, "Bad VCHIQ service handle");
ERROR_BLOCK (no_memory, 0x81A44, "VCHIQ: not enough memory");
ERROR_BLOCK (not_connected, 0x81A45, "VCHIQ: not connected (VCHIQ_Connect first)");
ERROR_BLOCK (no_service, 0x81A46, "The VideoCore has no such service (or refused it)");
ERROR_BLOCK (no_smem, 0x81A47, "The VideoCore's shared memory service isn't available under Linux");
ERROR_BLOCK (too_many, 0x81A48, "VCHIQ: too many services open");
ERROR_BLOCK (queue_failed, 0x81A49, "VCHIQ: Linux couldn't queue the message");
ERROR_BLOCK (bulk_failed, 0x81A4A, "VCHIQ: Linux's bulk transfer failed");
ERROR_BLOCK (dequeue_failed, 0x81A4B, "VCHIQ: Linux couldn't give the message (buffer too small?)");
ERROR_BLOCK (not_here, 0x81A4C, "This VCHIQ SWI isn't provided under RISC OS for Linux");
ERROR_BLOCK (bad_swi, 0x1E6, "Unknown VCHIQ SWI");

static instance *
get_instance (unsigned h)
{
  for (instance *i = instances; i; i = i->next)
    if ((unsigned) i == h && i->magic == MAGIC)
      return i;
  return 0;
}

static service *
get_service (unsigned h)
{
  for (int i = 0; i < SERVICES; i++)
    if (services[i].in && services[i].handle == h)
      return &services[i];
  return 0;
}

static void
close_instance (instance *in)
{
  for (int i = 0; i < SERVICES; i++)
    if (services[i].in == in)
      services[i].in = 0;
  lclose (in->fd);		/* (Linux removes its services) */
  in->magic = 0;
}

static oserror *
v_initialise (regs *r)
{
  if (!rol_native ())
    return ERR (not_native);
  int fd = lopen ("/dev/vchiq");
  if (fd < 0)
    return ERR (no_vchiq);
  instance *in = rma_claim (sizeof *in);
  if (!in)
    {
      lclose (fd);
      return ERR (no_memory);
    }
  in->magic = MAGIC;
  in->fd = fd;
  in->connected = 0;
  in->next = instances;
  instances = in;
  r->r[0] = (unsigned) in;
  return 0;
}

static oserror *
v_connect (regs *r)
{
  instance *in = get_instance (r->r[2]);
  if (!in)
    in = get_instance (r->r[0]);	/* (either: the PRM's R2, or R0) */
  if (!in)
    return ERR (bad_instance);
  if (!in->connected)
    {
      int e;
      do
	e = lioctl (in->fd, IOC_CONNECT, 0);
      while (e == -EINTR);
      if (e < 0)
	return ERR (no_vchiq);
      in->connected = 1;
    }
  return 0;
}

static oserror *
v_disconnect (regs *r)
{
  instance *in = get_instance (r->r[0]);
  if (!in)
    return ERR (bad_instance);
  for (instance **p = &instances; *p; p = &(*p)->next)
    if (*p == in)
      {
	*p = in->next;
	break;
      }
  close_instance (in);
  rma_free (in);
  return 0;
}

static oserror *
v_service_open (regs *r)
{
  instance *in = get_instance (r->r[0]);
  const unsigned *setup = (const unsigned *) r->r[1];
  if (!in)
    return ERR (bad_instance);
  if (!in->connected)
    return ERR (not_connected);
  if (setup[2] == FOURCC_SMEM)
    return ERR (no_smem);
  service *s = 0;
  for (int i = 0; i < SERVICES && !s; i++)
    if (!services[i].in)
      s = &services[i];
  if (!s)
    return ERR (too_many);
  struct create_service32 c;
  memset (&c, 0, sizeof c);
  c.fourcc = (int) setup[2];
  c.version = (short) setup[0];
  c.version_min = (short) setup[1];
  c.is_open = 1;
  c.is_vchi = 1;		/* messages are dequeued, not called back */
  int e;
  do
    e = lioctl (in->fd, IOC_CREATE_SERVICE, &c);
  while (e == -EINTR);
  if (e < 0)
    return ERR (no_service);
  s->in = in;
  s->handle = c.handle;
  s->callback = setup[6];
  s->cb_r0 = setup[7];
  r->r[0] = c.handle;
  return 0;
}

static oserror *
v_service_close (regs *r)
{
  service *s = get_service (r->r[0]);
  if (!s)
    return ERR (bad_service);
  int e;
  do
    e = lsys (NR_ioctl, (unsigned) s->in->fd, IOC_REMOVE_SERVICE, s->handle, 0, 0, 0);
  while (e == -EINTR);
  s->in = 0;
  return 0;
}

static oserror *
v_msg_queue (regs *r)
{
  service *s = get_service (r->r[0]);
  if (!s)
    return ERR (bad_service);
  struct element32 el = { r->r[1], r->r[2] };
  struct queue_message32 q = { s->handle, 1, (unsigned) &el };
  int e;
  do
    e = lioctl (s->in->fd, IOC_QUEUE_MESSAGE, &q);
  while (e == -EINTR);
  return e < 0 ? ERR (queue_failed) : 0;
}

static oserror *
v_msg_dequeue (regs *r)
{
  service *s = get_service (r->r[0]);
  if (!s)
    return ERR (bad_service);
  struct dequeue32 d = { s->handle, 0, r->r[2], r->r[1] };
  int e;
  do
    e = lioctl (s->in->fd, IOC_DEQUEUE_MESSAGE, &d);
  while (e == -EINTR);
  if (e == -EWOULDBLOCK)
    e = 0;
  if (e < 0)
    {
      r->r[2] = 0;
      return ERR (dequeue_failed);
    }
  r->r[2] = (unsigned) e;
  return 0;
}

typedef void (*callback_fn) (unsigned r0, unsigned reason, unsigned handle, unsigned userdata);

static oserror *
v_bulk (regs *r, int receive)
{
  service *s = get_service (r->r[0]);
  if (!s)
    return ERR (bad_service);
  struct bulk32 b = { s->handle, r->r[1], r->r[2], 0, BULK_MODE_BLOCKING };
  int e;
  do				/* (interrupted: Linux has made it "waiting" for the retry) */
    e = lioctl (s->in->fd, receive ? IOC_BULK_RECEIVE : IOC_BULK_TRANSMIT, &b);
  while (e == -EINTR);
  transfers++;
  if (e < 0)
    failures++;
  if (receive && (r->r[3] & (VCHI_CALLBACK_WHEN_OP_COMPLETE | VCHI_CALLBACK_WHEN_DATA_READ)) && s->callback)
    ((callback_fn) s->callback) (s->cb_r0, e < 0 ? REASON_RECEIVE_ABORTED : REASON_RECEIVED, s->handle, r->r[4]);
  return e < 0 ? ERR (bulk_failed) : 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 0: return v_initialise (r);
    case 1: return v_connect (r);
    case 2: return v_disconnect (r);
    case 3: return v_bulk (r, 0);
    case 4: return v_msg_dequeue (r);
    case 5: return v_msg_queue (r);
    case 9: return v_service_close (r);
    case 10: return v_service_open (r);
    case 13:
    case 14: return get_service (r->r[0]) ? 0 : ERR (bad_service);
    case 15: return v_bulk (r, 1);
    }
  return number < 16 ? ERR (not_here) : ERR (bad_swi);
}

oserror *
mod_init (const char *tail, int instance_, void *pw)
{
  (void) tail; (void) instance_; (void) pw;
  instances = 0;
  memset (services, 0, sizeof services);
  transfers = failures = 0;
  /* only where there's a VideoCore to talk to: elsewhere programs should
     see no VCHIQ module (and decode in software) */
  if (!rol_native ())
    return ERR (not_native);
  int fd = lopen ("/dev/vchiq");
  if (fd < 0)
    return ERR (no_vchiq);
  lclose (fd);
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  while (instances)
    {
      instance *in = instances;
      instances = in->next;
      close_instance (in);
      rma_free (in);
    }
  return 0;
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  int n = 0, s = 0;
  for (instance *i = instances; i; i = i->next)
    n++;
  for (int i = 0; i < SERVICES; i++)
    if (services[i].in)
      s++;
  write0 ("VCHIQ (RISC OS for Linux: Linux's /dev/vchiq): ");
  write_dec ((unsigned) n);
  write0 (" connections, ");
  write_dec ((unsigned) s);
  write0 (" services open; ");
  write_dec (transfers);
  write0 (" bulk transfers, ");
  write_dec (failures);
  write0 (" failed");
  newline ();
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}
