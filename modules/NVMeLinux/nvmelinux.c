/* NVMeDriver for RISC OS on Linux: NVMe drives through Linux, for RISC OS
   Open's NVMeFS.

   On a machine running RISC OS itself, NVMe drives belong to three RISC OS
   Open modules: NVMeDriver (the PCI hardware), NVMeFS (a FileCore filing
   system that sends NVMeDriver NVMe commands) and NVMeFiler (the icon).
   On RISC OS on Linux, Linux owns the hardware, so this module takes
   NVMeDriver's place: the same name and SWIs, and each NVMe command NVMeFS
   sends is done on the drive through Linux - Identify answered from what
   Linux says of it, Read and Write as reads and writes of its sectors.
   NVMeFS and NVMeFiler are RISC OS Open's own, unchanged; a drive with
   4096 byte sectors works as it does there (SCSIFS, which the machine's
   other internal FileCore discs go through, stops at 1024).

   The drives: the image's rol-discd lists the machine's NVMe drives (or
   partitions of them) that are in RISC OS's FileCore format in
   /riscos/mediactl/nvme (device, size, disc name, sector size;
   tab-separated), and opens one for us when asked on its abstract socket
   "rol-discd-discs" ("OPEN nvme0n1": the reply carries the open drive).
   RISC OS can't open Linux's /dev itself. Each drive is a controller
   with one namespace. For testing, the system variable NVMeLinux$Test
   names a disc image file (a Linux path), which becomes a drive too, with
   sectors of NVMeLinux$TestSector bytes (512 if unset).

   What NVMeFS uses (and so what is here): NVMeDriver_ControllerEnumerate,
   _ControllerAccessInfo, _ControllerInfo, and _Transfer with the admin
   command Identify (namespace, controller) and the I/O commands Read,
   Write, Verify and Flush, data given as PRP entries of one memory page
   each in system memory. Every command is done before _Transfer returns;
   a completion routine asked for "later" (completion type 2) is called
   from a transient callback, as the hardware driver does, the others at
   once. Writes reach the drive before the command ends (rol-discd opens
   it O_DSYNC).

   The interface is RISC OS Open's and Stader Softwareentwicklung's
   (NVMeDriver's doc/Concept_Driver.txt); none of their code is here.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */

#include "cmodule.h"

#define IXSupport_LinuxSyscall	0x59EC0
#define OS_ReadVarVal		0x23
#define OS_ServiceCall		0x30
#define OS_AddCallBack		0x54
#define OS_RemoveCallBack	0x5F

#define Service_NVMeDriverStarted	0x811C0
#define Service_NVMeDriverDying		0x811C1
#define Service_NVMeDriverTransferDone	0x811C2

#define NR_read		3
#define NR_open		5
#define NR_close	6
#define NR_ioctl	54
#define NR_fsync	118
#define NR_llseek	140
#define NR_pread64	180
#define NR_pwrite64	181
#define NR_socket	281
#define NR_connect	283
#define NR_send		289
#define NR_recvmsg	297
#define O_RDWR		2
#define O_CLOEXEC	02000000
#define O_DSYNC		010000
#define MSG_CMSG_CLOEXEC 0x40000000
#define BLKSSZGET	0x1268

#define MAXDEV		8		/* NVMeDriver's limit is the same */
#define PAGE		4096u		/* the "memory page" of a PRP entry */
#define MAXDATA		(128u * 1024)	/* most data in one command */
#define NOHANDLE	0xFFFFFFFFu

/* completion types (bits 24-27 of r1) */
#define CT_NONE		0
#define CT_SERVICE	1
#define CT_CALLBACK	2
#define CT_GETRESULT	3
#define CT_BLOCK	4
#define CT_SERVICE_S	5
#define CT_CALLBACK_S	6

/* NVMe status (bits 17 up of the result's fourth word): type << 8 | code.
   NVMeFS takes a command as failed when the low SEVEN bits of the code
   are not zero (its NVMe_DW3_SF_SC is &7F, where NVMe's code is eight
   bits), so NVMe's own codes &80 (LBA out of range) and &280 (write
   fault) would pass for success, and a failed write would be lost
   without a word. Every failure here therefore has a code NVMeFS sees:
   a write that fails is "data transfer error", an address off the end
   "invalid field". &281 (unrecovered read error) NVMeFS turns into
   FileCore's disc error. */
#define ST_OK		0x000
#define ST_BAD_OPCODE	0x001
#define ST_BAD_FIELD	0x002
#define ST_DATA_ERROR	0x004
#define ST_BAD_NS	0x00B
#define ST_LBA_RANGE	ST_BAD_FIELD
#define ST_WRITE_FAULT	ST_DATA_ERROR
#define ST_READ_ERROR	0x281
#define ST_READ_ONLY	0x182

#define SEEN(st) (((st) & 0x7F) != 0)
typedef char status_codes_seen_by_nvmefs[SEEN (ST_BAD_OPCODE) && SEEN (ST_BAD_FIELD) && SEEN (ST_DATA_ERROR)
					 && SEEN (ST_BAD_NS) && SEEN (ST_LBA_RANGE) && SEEN (ST_WRITE_FAULT)
					 && SEEN (ST_READ_ERROR) && SEEN (ST_READ_ONLY) ? 1 : -1];

ERROR_BLOCK (bad_handle, 0x821C00, "Invalid NVMe controller handle");
ERROR_BLOCK (bad_swi, 0x1E6, "SWI value out of range for module NVMeDriver");
ERROR_BLOCK (bad_cmd, 0x821C01, "NVMeDriver: no command, or no result block");
ERROR_BLOCK (not_port, 0x821C03, "NVMeDriver (Linux) is for RISC OS on Linux (no PVOps device)");
ERROR_BLOCK (no_vector, 0x821C04, "NVMeDriver couldn't claim the SWI vector");
ERROR_BLOCK (in_use, 0x821C05, "NVMeDriver can't leave: another module claimed the SWI vector after it");
ERROR_BLOCK (busy, 0x821C02, "NVMeDriver: too many completions waiting");

void nv_call (void (*proc) (void), void *ws, unsigned handle, unsigned id);
extern void nv_swi_vector (void);
extern unsigned nv_old_swi_vector, nv_movs_pc_lr;
static int claimed;
#define RD(a) (*(volatile unsigned *) (a))

static unsigned
get_tls (void)
{
  unsigned v;
  asm volatile ("mrc	p15, 0, %0, c13, c0, 3" : "=r" (v));
  return v;
}
void callback_entry (void);

static int
lsys (int nr, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
  regs r = {{ a, b, c, d, e, f, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;
  return (int) r.r[0];
}

static int
streq (const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

struct dev
{
  int used;
  char src[64];			/* rol-discd's device (nvme0n1), or the test file */
  char name[16];		/* its FileCore disc name */
  char model[41], serial[21], subnqn[80];
  int fd;
  int ro;
  unsigned log2ss;		/* sector (LBA) size */
  unsigned long long bytes, sectors;
  unsigned reads, writes, errors;
};

static struct dev devs[MAXDEV];
static void *pw_ws;

/* completion routines to call from the transient callback */
#define MAXPEND 8
static struct { void (*proc) (void); void *ws; unsigned handle, id; } pend[MAXPEND];
static int npend, callback_set, announce;
static unsigned next_id = 1;

/* --- the drives --------------------------------------------------------- */

static unsigned long long
file_size (int fd)
{
  unsigned long long r = 0;
  if (lsys (NR_llseek, (unsigned) fd, 0, 0, (unsigned) &r, 2 /* SEEK_END */, 0) < 0)
    return 0;
  return r;
}

/* ask rol-discd for device DEV: the open drive, or -1 */
static int
ask_roldiscd (const char *dev, int *ro)
{
  int s = lsys (NR_socket, 1 /* AF_UNIX */, 5 /* SOCK_SEQPACKET */ | O_CLOEXEC, 0, 0, 0, 0);
  if (s < 0)
    return -1;
  static const char addr[] = "\1\0\0rol-discd-discs";	/* abstract name */
  int fd = -1;
  if (lsys (NR_connect, (unsigned) s, (unsigned) addr, 2 + 16, 0, 0, 0) == 0)
    {
      char req[48] = "OPEN ";
      size_t l = strlen (dev);
      if (l > 40)
	l = 40;
      memcpy (req + 5, dev, l);
      req[5 + l] = 0;
      if (lsys (NR_send, (unsigned) s, (unsigned) req, 6 + l, 0, 0, 0) > 0)
	{
	  char rep[64];
	  struct { unsigned len; int level, type; int fd; } cm = { 16, 0, 0, -1 };
	  struct { void *base; unsigned len; } iov = { rep, sizeof rep - 1 };
	  struct { void *name; unsigned namelen; void *iov; unsigned iovlen; void *control; unsigned controllen; int flags; }
	    mh = { 0, 0, &iov, 1, &cm, 16, 0 };
	  memset (rep, 0, sizeof rep);
	  int n = lsys (NR_recvmsg, (unsigned) s, (unsigned) &mh, MSG_CMSG_CLOEXEC, 0, 0, 0);
	  int got = mh.controllen >= 16 && cm.level == 1 && cm.type == 1 ? cm.fd : -1;
	  if (n > 3 && rep[0] == 'O' && rep[1] == 'K' && got >= 0)
	    {
	      /* "OK bytes ro" */
	      const char *p = rep + 3;
	      while (*p && *p != ' ')
		p++;
	      *ro = *p == ' ' && p[1] == '1';
	      fd = got;
	    }
	  else if (got >= 0)
	    lsys (NR_close, (unsigned) got, 0, 0, 0, 0, 0);
	}
    }
  lsys (NR_close, (unsigned) s, 0, 0, 0, 0, 0);
  return fd;
}

static void
put (char *to, size_t n, const char *a, const char *b)
{
  size_t k = 0;
  for (; *a && k + 1 < n; a++)
    to[k++] = *a;
  for (; *b && k + 1 < n; b++)
    to[k++] = *b;
  to[k] = 0;
}

/* a drive: SRC is rol-discd's device name, or (TEST) a file's path with
   sectors of TESTSS bytes */
static void
dev_add (const char *src, const char *name, int test, unsigned testss)
{
  struct dev *d = 0;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used && streq (devs[i].src, src))
      return;
  for (int i = 0; i < MAXDEV && !d; i++)
    if (!devs[i].used)
      d = &devs[i];
  if (!d)
    return;
  int ro = 0, fd;
  if (test)
    {
      fd = lsys (NR_open, (unsigned) src, O_RDWR | O_DSYNC | O_CLOEXEC, 0, 0, 0, 0);
      if (fd < 0)
	{
	  fd = lsys (NR_open, (unsigned) src, O_CLOEXEC /* O_RDONLY */, 0, 0, 0, 0);
	  ro = 1;
	}
    }
  else
    fd = ask_roldiscd (src, &ro);
  if (fd < 0)
    return;
  memset (d, 0, sizeof *d);
  d->fd = fd;
  d->ro = ro;
  put (d->src, sizeof d->src, src, "");
  put (d->name, sizeof d->name, name, "");
  put (d->model, sizeof d->model, test ? "Disc image " : "Linux NVMe ", test ? "" : src);
  put (d->serial, sizeof d->serial, test ? "TEST" : src, "");
  put (d->subnqn, sizeof d->subnqn, "nqn.2026-11.riscos-on-linux:", test ? "test" : src);
  int ss = 0;
  if (test)
    ss = (int) testss;
  else if (lsys (NR_ioctl, (unsigned) fd, BLKSSZGET, (unsigned) &ss, 0, 0, 0) < 0)
    ss = 512;
  d->log2ss = 9;
  while ((1 << d->log2ss) < ss && d->log2ss < 12)
    d->log2ss++;
  d->bytes = file_size (fd);
  d->sectors = d->bytes >> d->log2ss;
  if (!d->sectors)
    {
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
      return;
    }
  d->used = 1;
}

static unsigned
number (const char *p)
{
  unsigned v = 0;
  while (*p >= '0' && *p <= '9')
    v = v * 10 + (unsigned) (*p++ - '0');
  return v;
}

/* rol-discd's list, and the test file */
static void
find_drives (void)
{
  static char buf[1024];
  int fd = lsys (NR_open, (unsigned) "/riscos/mediactl/nvme", O_CLOEXEC, 0, 0, 0, 0);
  if (fd >= 0)
    {
      int n = lsys (NR_read, (unsigned) fd, (unsigned) buf, sizeof buf - 1, 0, 0, 0);
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
      buf[n > 0 ? n : 0] = 0;
      /* lines: device <tab> bytes <tab> name <tab> sector size */
      char *p = buf;
      while (*p)
	{
	  char *e = p;
	  while (*e && *e != '\n')
	    e++;
	  char *t1 = p;
	  while (t1 < e && *t1 != '\t')
	    t1++;
	  char *t2 = t1 < e ? t1 + 1 : e;
	  while (t2 < e && *t2 != '\t')
	    t2++;
	  char *t3 = t2 < e ? t2 + 1 : e;
	  while (t3 < e && *t3 != '\t')
	    t3++;
	  char dev[40], name[16];
	  size_t dl = (size_t) (t1 - p), nl = t2 < e ? (size_t) (t3 - t2 - 1) : 0;
	  if (dl > 0 && dl < sizeof dev)
	    {
	      memcpy (dev, p, dl);
	      dev[dl] = 0;
	      if (nl >= sizeof name)
		nl = sizeof name - 1;
	      memcpy (name, t2 + 1, nl);
	      name[nl] = 0;
	      dev_add (dev, name, 0, 0);
	    }
	  p = *e ? e + 1 : e;
	}
    }
  char path[64], num[16];
  regs v = {{ (unsigned) "NVMeLinux$Test", (unsigned) path, sizeof path - 1, 0, 0 }};
  if (!swix_regs (OS_ReadVarVal, &v) && v.r[2] > 0)
    {
      path[v.r[2]] = 0;
      unsigned ss = 512;
      regs w = {{ (unsigned) "NVMeLinux$TestSector", (unsigned) num, sizeof num - 1, 0, 0 }};
      if (!swix_regs (OS_ReadVarVal, &w) && w.r[2] > 0)
	{
	  num[w.r[2]] = 0;
	  ss = number (num);
	}
      if (ss == 512 || ss == 1024 || ss == 2048 || ss == 4096)
	dev_add (path, "Test", 1, ss);
    }
}

/* --- NVMe commands ------------------------------------------------------ */

static unsigned
rd32 (const unsigned char *p)
{
  return p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24;
}

static void
wr32 (unsigned char *p, unsigned v)
{
  p[0] = (unsigned char) v, p[1] = (unsigned char) (v >> 8);
  p[2] = (unsigned char) (v >> 16), p[3] = (unsigned char) (v >> 24);
}

static void
wr64 (unsigned char *p, unsigned long long v)
{
  wr32 (p, (unsigned) v);
  wr32 (p + 4, (unsigned) (v >> 32));
}

/* a field of spaces-padded text, as Identify has them */
static void
padded (unsigned char *to, size_t n, const char *s)
{
  size_t k = 0;
  for (; s[k] && k < n; k++)
    to[k] = (unsigned char) s[k];
  for (; k < n; k++)
    to[k] = ' ';
}

/* a PRP entry as an address here: the caller's memory is system memory
   (logical addresses), and the two low bits are flags of its own */
static unsigned char *
prp_addr (const unsigned char *entry)
{
  return (unsigned char *) (rd32 (entry) & ~3u);
}

/* Read (WRITE 0) or write (1) BYTES bytes at byte OFFSET of the drive from
   or to the memory the command's PRP entries name: the first entry and
   each later one are a memory page (PAGE bytes; the last may be short).
   With more than two, the second entry is a list of them, whose last slot
   in each page of the list is the next page's address when more than one
   entry is still to come. Entries next to each other in memory are done
   in one go. Returns an NVMe status. */
static unsigned
transfer (struct dev *d, const unsigned char *cmd, int write, unsigned long long offset, unsigned bytes)
{
  const unsigned per_page = PAGE / 8;
  unsigned char *run = 0;		/* memory not yet done: a run of pages */
  unsigned runlen = 0, left = bytes, slot = 0, n = 0;
  const unsigned char *list = 0;
  while (left || runlen)
    {
      unsigned char *at = 0;
      unsigned len = 0;
      if (left)
	{
	  if (n == 0)
	    at = prp_addr (cmd + 24);
	  else if (n == 1 && left <= PAGE)
	    at = prp_addr (cmd + 32);
	  else
	    {
	      if (!list)
		list = prp_addr (cmd + 32), slot = 0;
	      if (!list)
		return ST_DATA_ERROR;
	      if (slot == per_page - 1 && left > PAGE)
		{
		  list = prp_addr (list + 8 * slot);
		  slot = 0;
		  if (!list)
		    return ST_DATA_ERROR;
		}
	      at = prp_addr (list + 8 * slot++);
	    }
	  if (!at)
	    return ST_DATA_ERROR;
	  len = left < PAGE ? left : PAGE;
	  left -= len;
	  n++;
	  if (run && at == run + runlen)
	    {
	      runlen += len;
	      if (left)
		continue;
	      at = 0;
	    }
	}
      if (runlen)
	{
	  int r = lsys (write ? NR_pwrite64 : NR_pread64, (unsigned) d->fd, (unsigned) run, runlen,
			0, (unsigned) offset, (unsigned) (offset >> 32));
	  if (r != (int) runlen)
	    {
	      d->errors++;
	      return write ? ST_WRITE_FAULT : ST_READ_ERROR;
	    }
	  offset += runlen;
	  runlen = 0;
	  run = 0;
	}
      if (at)
	run = at, runlen = len;
    }
  return ST_OK;
}

static unsigned
identify (struct dev *d, const unsigned char *cmd)
{
  unsigned char *b = prp_addr (cmd + 24);
  unsigned cns = rd32 (cmd + 40) & 0xFF, nsid = rd32 (cmd + 4);
  if (!b)
    return ST_DATA_ERROR;
  memset (b, 0, 4096);
  if (cns == 0)
    {
      /* the namespace: its size in sectors, and one LBA format */
      if (nsid != 1)
	return ST_BAD_NS;
      wr64 (b + 0, d->sectors);		/* NSZE */
      wr64 (b + 8, d->sectors);		/* NCAP */
      wr64 (b + 16, d->sectors);	/* NUSE */
      b[25] = 0;			/* NLBAF: one format (0 based) */
      b[26] = 0;			/* FLBAS: format 0 */
      wr32 (b + 128, d->log2ss << 16);	/* LBAF0: no metadata, 2^n bytes */
      return ST_OK;
    }
  if (cns == 1)
    {
      padded (b + 4, 20, d->serial);
      padded (b + 24, 40, d->model);
      padded (b + 64, 8, "Linux");
      b[77] = 5;			/* MDTS: 2^5 pages = MAXDATA */
      wr32 (b + 80, 0x00010400);	/* VER 1.4 */
      b[111] = 1;			/* an I/O controller */
      b[512] = 0x66;			/* SQES */
      b[513] = 0x44;			/* CQES */
      wr32 (b + 516, 1);		/* NN: one namespace */
      for (size_t k = 0; d->subnqn[k] && k < 255; k++)
	b[768 + k] = (unsigned char) d->subnqn[k];
      return ST_OK;
    }
  return ST_BAD_FIELD;
}

static unsigned
command (struct dev *d, unsigned queue, const unsigned char *cmd)
{
  unsigned op = cmd[0];
  if (queue == 0)
    {
      if (op == 0x06)
	return identify (d, cmd);
      if (op == 0x09 || op == 0x0A)	/* Set/Get Features: nothing to set */
	return ST_OK;
      return ST_BAD_OPCODE;
    }
  if (op == 0x00)			/* Flush */
    {
      lsys (NR_fsync, (unsigned) d->fd, 0, 0, 0, 0, 0);
      return ST_OK;
    }
  if (op != 0x01 && op != 0x02 && op != 0x0C)
    return ST_BAD_OPCODE;
  if (rd32 (cmd + 4) != 1)
    return ST_BAD_NS;
  unsigned long long lba = rd32 (cmd + 40) | (unsigned long long) rd32 (cmd + 44) << 32;
  unsigned count = (rd32 (cmd + 48) & 0xFFFF) + 1;
  if (lba >= d->sectors || count > d->sectors - lba)
    return ST_LBA_RANGE;
  if (((unsigned long long) count << d->log2ss) > MAXDATA)
    return ST_BAD_FIELD;
  if (op == 0x0C)			/* Verify: no data comes back */
    return ST_OK;
  if (op == 0x01)
    {
      if (d->ro)
	return ST_READ_ONLY;
      d->writes++;
    }
  else
    d->reads++;
  return transfer (d, cmd, op == 0x01, lba << d->log2ss, count << d->log2ss);
}

/* --- the transient callback --------------------------------------------- */

static void
want_callback (void)
{
  if (callback_set)
    return;
  regs r = {{ (unsigned) callback_entry, (unsigned) pw_ws }};
  if (!swix_regs (OS_AddCallBack, &r))
    callback_set = 1;
}

void
c_callback (void)
{
  callback_set = 0;
  if (announce)
    {
      announce = 0;
      regs r = {{ 0, Service_NVMeDriverStarted }};
      swix_regs (OS_ServiceCall, &r);
    }
  /* (a routine may send another command, which may add to the list) */
  while (npend > 0)
    {
      void (*proc) (void) = pend[0].proc;
      void *ws = pend[0].ws;
      unsigned handle = pend[0].handle, id = pend[0].id;
      npend--;
      memmove (&pend[0], &pend[1], (size_t) npend * sizeof pend[0]);
      nv_call (proc, ws, handle, id);
    }
}

/* --- SWIs --------------------------------------------------------------- */

static oserror *
swi_transfer (regs *r)
{
  unsigned h = r->r[0], queue = r->r[1] & 0xFFFF, ct = (r->r[1] >> 24) & 15;
  const unsigned char *cmd = (const unsigned char *) r->r[2];
  unsigned *res = (unsigned *) r->r[3];
  if (h >= MAXDEV || !devs[h].used)
    {
      r->r[3] = 0;
      return ERR (bad_handle);
    }
  if (!cmd || !res)
    {
      r->r[3] = 0;
      return ERR (bad_cmd);
    }
  /* the caller's own words, before the result goes over them */
  unsigned handle = res[0];
  void (*proc) (void) = (void (*) (void)) res[1];
  void *ws = (void *) res[2];
  if (ct == CT_CALLBACK && npend == MAXPEND)
    {
      r->r[3] = 0;
      return ERR (busy);
    }
  unsigned id = next_id++;
  if (!next_id)
    next_id = 1;
  res[3] = 0;
  unsigned status = command (&devs[h], queue, cmd);
  res[0] = 0;
  res[1] = 0;
  res[2] = queue << 16;
  /* done (bit 16: never zero), the status, and the command's identifier */
  res[3] = 0x10000 | status << 17 | (rd32 (cmd) >> 16);
  r->r[3] = id;
  switch (ct)
    {
    case CT_SERVICE:
    case CT_SERVICE_S:
      {
	regs s = {{ 0, Service_NVMeDriverTransferDone, handle, (unsigned) ws, id }};
	swix_regs (OS_ServiceCall, &s);
      }
      break;
    case CT_CALLBACK:
      if (proc && ws)
	{
	  pend[npend].proc = proc, pend[npend].ws = ws;
	  pend[npend].handle = handle, pend[npend].id = id;
	  npend++;
	  want_callback ();
	}
      break;
    case CT_CALLBACK_S:
      if (proc && ws)
	nv_call (proc, ws, handle, id);
      break;
    }
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 0:				/* ControllerEnumerate */
      {
	unsigned h = r->r[0];
	/* (a filter by type, r1 bit 0: all of ours are I/O controllers, 1) */
	if ((r->r[1] & 1) && ((r->r[1] >> 8) & 0xFF) != 1)
	  h = MAXDEV;
	while (h < MAXDEV && !devs[h].used)
	  h++;
	if (h >= MAXDEV)
	  {
	    r->r[0] = NOHANDLE;
	    r->r[5] = NOHANDLE;
	    return 0;
	  }
	r->r[0] = h;
	r->r[1] = (unsigned) devs[h].subnqn;
	r->r[2] = h;			/* controller ID */
	r->r[3] = 0;			/* domain */
	r->r[4] = 1;			/* an I/O controller */
	r->r[5] = h + 1;
	r->r[6] = 0;			/* name set */
	r->r[7] = 1;			/* namespaces */
	r->r[8] = 0;			/* "PCIe": a fixed disc to NVMeFS */
	return 0;
      }
    case 1:				/* ControllerAccessInfo */
      if (r->r[0] >= MAXDEV || !devs[r->r[0]].used)
	return ERR (bad_handle);
      r->r[1] = 2;			/* queues: admin and one for I/O */
      r->r[2] = 1;			/* data in system memory */
      r->r[3] = PAGE;
      r->r[4] = MAXDATA;
      return 0;
    case 2:				/* Transfer */
      return swi_transfer (r);
    case 3:				/* GetResult: all are complete */
      r->r[0] = 0;
      return 0;
    case 4:				/* SetSpecialPRPinfo */
    case 5:				/* QueueReset */
      return 0;
    case 6:				/* ControllerInfo */
      {
	if (r->r[0] >= MAXDEV || !devs[r->r[0]].used)
	  return ERR (bad_handle);
	struct dev *d = &devs[r->r[0]];
	r->r[2] = (unsigned) (r->r[1] == 0 ? d->model : r->r[1] == 1 ? d->serial
			      : r->r[1] == 2 ? "Linux" : d->subnqn);
	return 0;
      }
    }
  return ERR (bad_swi);
}

/* --- the module --------------------------------------------------------- */

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail, (void) instance;
  pw_ws = pw;
  /* OS_MMUControl 2, for NVMeFS (see header.s), unless the kernel answers
     it: the port's PVOps HAL device (type &30A) has its MOVS pc, lr
     routine at +100 */
  regs f = {{ 2 }};
  if (swix_regs (0x6B /* OS_MMUControl */, &f))
    {
      regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
      if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2])
	return ERR (not_port);
      nv_movs_pc_lr = RD (h.r[2] + 100);
      unsigned *vectors = (unsigned *) RD (get_tls () + 8);
      nv_old_swi_vector = vectors[1];
      regs v = {{ 0x102, (unsigned) nv_swi_vector }};
      if (swix_regs (0x69 /* OS_ClaimProcessorVector */, &v))
	return ERR (no_vector);
      claimed = 1;
    }
  find_drives ();
  /* NVMeFS, if it is there already, looks again at the service call */
  announce = 1;
  want_callback ();
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  if (claimed)
    {
      regs v = {{ 2, nv_old_swi_vector, (unsigned) nv_swi_vector }};
      if (swix_regs (0x69, &v))
	return ERR (in_use);
      claimed = 0;
    }
  if (callback_set)
    {
      regs c = {{ (unsigned) callback_entry, (unsigned) pw_ws }};
      swix_regs (OS_RemoveCallBack, &c);
      callback_set = 0;
    }
  npend = 0;
  regs s = {{ 0, Service_NVMeDriverDying }};
  swix_regs (OS_ServiceCall, &s);
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      {
	lsys (NR_fsync, (unsigned) devs[i].fd, 0, 0, 0, 0, 0);
	lsys (NR_close, (unsigned) devs[i].fd, 0, 0, 0, 0, 0);
	devs[i].used = 0;
      }
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index, (void) tail, (void) argc;
  int any = 0;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      {
	struct dev *d = &devs[i];
	any = 1;
	write0 (d->src);
	write0 (": \"");
	write0 (d->name);
	write0 ("\", ");
	write_dec ((unsigned) (d->bytes >> 20));
	write0 (" MB, ");
	write_dec (1u << d->log2ss);
	write0 (" byte sectors, ");
	write0 (d->ro ? "read only, " : "");
	write0 ("controller ");
	write_dec ((unsigned) i);
	write0 (", reads ");
	write_dec (d->reads);
	write0 (", writes ");
	write_dec (d->writes);
	if (d->errors)
	  {
	    write0 (", errors ");
	    write_dec (d->errors);
	  }
	newline ();
      }
  if (!any)
    {
      write0 ("No NVMe drives in RISC OS's format.");
      newline ();
    }
  return 0;
}
