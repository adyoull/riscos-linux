/* rol-discd: gives RISC OS the machine's internal discs that are in RISC
 * OS's own format (FileCore): NVMe, SATA and eMMC drives, and FileCore
 * partitions on them, for RISC OS's SCSIFS through the SCSILinux module -
 * as rol-usbd does for USB discs and cards, which it leaves alone.
 *
 * It looks once, when it starts (internal discs don't come and go): each
 * disc that isn't on USB, isn't removable and isn't an SD card in a slot.
 * A disc that is FileCore as a whole is given whole; otherwise each of
 * its partitions that is. Never: the partition Linux runs from, anything
 * mounted or used as swap, or the disc Linux runs from as a whole (a
 * FileCore partition beside Linux's on that disc is given).
 *
 * /riscos/mediactl/filecore-internal lists them (device, size in bytes,
 * FileCore disc name, 0: not removable). SCSILinux asks for one on the
 * abstract socket "rol-discd-discs" ("OPEN nvme0n1p3") and gets the open
 * disc: read-write, opened O_EXCL so that Linux can't mount it meanwhile.
 * Only the discs listed are ever opened.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <unistd.h>

#define CTL "/riscos/mediactl"
#define RO_UID 1000
#define RO_GID 1000
#define MAXD 16

struct disc { char dev[40], name[12]; long long bytes; };
static struct disc discs[MAXD];
static int ndiscs;
static dev_t root_part, root_disk;

static void
say (const char *msg)
{
  int k = open ("/dev/kmsg", O_WRONLY | O_CLOEXEC);
  if (k >= 0)
    {
      if (write (k, msg, strlen (msg)) < 0) { }
      close (k);
    }
}

static int
read_file (const char *p, char *b, size_t n)
{
  int f = open (p, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return -1;
  ssize_t r = read (f, b, n - 1);
  close (f);
  if (r < 0)
    return -1;
  b[r] = 0;
  return (int) r;
}

static dev_t
dev_of (const char *sysdir)
{
  char p[400], t[32];
  unsigned ma, mi;
  snprintf (p, sizeof p, "%s/dev", sysdir);
  if (read_file (p, t, sizeof t) <= 0 || sscanf (t, "%u:%u", &ma, &mi) != 2)
    return 0;
  return makedev (ma, mi);
}

/* the whole disc that block device d is on (itself if it is one) */
static dev_t
whole_disc (dev_t d)
{
  char p[128], t[64];
  snprintf (p, sizeof p, "/sys/dev/block/%u:%u/partition", major (d), minor (d));
  if (access (p, F_OK))
    return d;
  snprintf (p, sizeof p, "/sys/dev/block/%u:%u/../dev", major (d), minor (d));
  unsigned ma, mi;
  if (read_file (p, t, sizeof t) > 0 && sscanf (t, "%u:%u", &ma, &mi) == 2)
    return makedev (ma, mi);
  return d;
}

/* The same test as rol-usbd's: a disc (or partition) whose boot block
   (bytes &C00-&DFF) has FileCore's checksum and a sensible disc record */
static int
filecore_disc (const char *node, char *name, size_t nsz, long long *bytes)
{
  unsigned char b[512];
  int f = open (node, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return 0;
  ssize_t r = pread (f, b, sizeof b, 0xC00);
  off_t devsize = lseek (f, 0, SEEK_END);
  close (f);
  if (r != (ssize_t) sizeof b)
    return 0;
  unsigned sum = 0, c = 0;
  for (int i = 510; i >= 0; i--)
    {
      sum += b[i] + c;
      c = sum >> 8;
      sum &= 0xFF;
    }
  if (sum != b[511])
    return 0;
  const unsigned char *dr = b + 0x1C0;
  unsigned log2ss = dr[0], nzones = dr[9] | (dr[42] << 8);
  if (log2ss < 8 || log2ss > 12 || dr[5] > 16 || nzones == 0)
    return 0;
  if (dr[4] < 8 || dr[4] > 21)
    return 0;
  unsigned long long dsize = (unsigned long long) (dr[16] | dr[17] << 8 | dr[18] << 16 | (unsigned) dr[19] << 24)
    | (unsigned long long) (dr[36] | dr[37] << 8 | dr[38] << 16 | (unsigned) dr[39] << 24) << 32;
  if (dsize == 0 || devsize <= 0 || dsize > (unsigned long long) devsize || dsize < (unsigned long long) devsize / 2)
    return 0;
  size_t k = 0;
  for (int i = 0; i < 10 && k + 1 < nsz; i++)
    {
      unsigned ch = dr[22 + i];
      if (ch < 32 || ch >= 127)
	break;
      name[k++] = (char) ch;
    }
  while (k > 0 && name[k - 1] == ' ')
    k--;
  name[k] = 0;
  *bytes = devsize;
  return 1;
}

/* in use by Linux: mounted, or swap */
static int
in_use (const char *dev)
{
  static const char *const lists[] = { "/proc/mounts", "/proc/swaps" };
  char node[64], line[512];
  snprintf (node, sizeof node, "/dev/%s", dev);
  size_t l = strlen (node);
  for (int i = 0; i < 2; i++)
    {
      FILE *f = fopen (lists[i], "re");
      if (!f)
	continue;
      while (fgets (line, sizeof line, f))
	if (!strncmp (line, node, l) && (line[l] == ' ' || line[l] == '\t'))
	  {
	    fclose (f);
	    return 1;
	  }
      fclose (f);
    }
  return 0;
}

/* a disc rol-usbd deals with: on USB, removable, or an SD card in a slot */
static int
usbd_disc (const char *blk)
{
  char p[300], t[1024];
  snprintf (p, sizeof p, "/sys/block/%s", blk);
  ssize_t n = readlink (p, t, sizeof t - 1);
  if (n > 0)
    {
      t[n] = 0;
      if (strstr (t, "/usb"))
	return 1;
    }
  if (!strncmp (blk, "mmcblk", 6))
    {
      snprintf (p, sizeof p, "/sys/block/%s/device/type", blk);
      if (read_file (p, t, sizeof t) > 0 && !strncmp (t, "SD", 2))
	return 1;
    }
  snprintf (p, sizeof p, "/sys/block/%s/removable", blk);
  return read_file (p, t, sizeof t) > 0 && t[0] == '1';
}

static int
give (const char *dev)
{
  char node[64], msg[200];
  if (ndiscs == MAXD || in_use (dev))
    return 0;
  struct disc *d = &discs[ndiscs];
  snprintf (node, sizeof node, "/dev/%s", dev);
  if (!filecore_disc (node, d->name, sizeof d->name, &d->bytes))
    return 0;
  snprintf (d->dev, sizeof d->dev, "%s", dev);
  ndiscs++;
  snprintf (msg, sizeof msg, "rol-discd: %s: internal FileCore disc \"%s\", %lld bytes: given to RISC OS (SCSIFS)\n",
	    dev, d->name, d->bytes);
  say (msg);
  return 1;
}

static void
scan (void)
{
  DIR *b = opendir ("/sys/block");
  struct dirent *e;
  if (!b)
    return;
  while ((e = readdir (b)))
    {
      const char *blk = e->d_name;
      char dir[300];
      if (strncmp (blk, "sd", 2) && strncmp (blk, "nvme", 4) && strncmp (blk, "mmcblk", 6)
	  && strncmp (blk, "vd", 2) && strncmp (blk, "hd", 2))
	continue;
      if (strstr (blk, "boot") || strstr (blk, "rpmb") || usbd_disc (blk))
	continue;
      snprintf (dir, sizeof dir, "/sys/block/%s", blk);
      dev_t whole = dev_of (dir);
      if (!whole)
	continue;
      /* the whole disc, unless Linux runs from it */
      if (whole != root_disk && give (blk))
	continue;
      DIR *p = opendir (dir);
      struct dirent *pe;
      if (!p)
	continue;
      while ((pe = readdir (p)))
	{
	  char pdir[600];
	  if (strncmp (pe->d_name, blk, strlen (blk)))
	    continue;
	  snprintf (pdir, sizeof pdir, "%s/%s", dir, pe->d_name);
	  dev_t pd = dev_of (pdir);
	  if (pd && pd != root_part)
	    give (pe->d_name);
	}
      closedir (p);
    }
  closedir (b);
}

static void
write_list (void)
{
  char tmp[] = CTL "/.filecore-internal";
  FILE *f = fopen (tmp, "we");
  if (!f)
    return;
  for (int i = 0; i < ndiscs; i++)
    fprintf (f, "%s\t%lld\t%s\t0\n", discs[i].dev, discs[i].bytes, discs[i].name);
  fclose (f);
  if (chown (tmp, RO_UID, RO_GID)) { }
  rename (tmp, CTL "/filecore-internal");
}

/* One request: "OPEN dev" -> "OK bytes readonly" with the open disc, or
   "ERR errno" (rol-usbd's protocol) */
static void
serve_one (int listener)
{
  int c = accept4 (listener, NULL, NULL, SOCK_CLOEXEC);
  if (c < 0)
    return;
  struct pollfd p = { c, POLLIN, 0 };
  char req[64], rep[64];
  ssize_t n = poll (&p, 1, 2000) == 1 ? recv (c, req, sizeof req - 1, 0) : -1;
  int fd = -1, err = EINVAL, ro = 0;
  long long bytes = 0;
  if (n > 5 && !strncmp (req, "OPEN ", 5))
    {
      req[n] = 0;
      err = ENOENT;
      for (int i = 0; i < ndiscs; i++)
	if (!strcmp (discs[i].dev, req + 5))
	  {
	    char node[64];
	    snprintf (node, sizeof node, "/dev/%.40s", discs[i].dev);
	    /* O_EXCL: Linux can't mount it while RISC OS has it; O_DSYNC:
	       each write reaches the disc before SCSIFS is told it's done */
	    fd = open (node, O_RDWR | O_EXCL | O_DSYNC | O_CLOEXEC);
	    if (fd < 0 && (errno == EROFS || errno == EACCES))
	      {
		fd = open (node, O_RDONLY | O_EXCL | O_CLOEXEC);
		ro = 1;
	      }
	    err = fd < 0 ? errno : 0;
	    bytes = discs[i].bytes;
	    break;
	  }
    }
  if (fd >= 0)
    {
      snprintf (rep, sizeof rep, "OK %lld %d", bytes, ro);
      union { char buf[CMSG_SPACE (sizeof (int))]; struct cmsghdr align; } ctl;
      struct iovec iov = { rep, strlen (rep) + 1 };
      struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl.buf, .msg_controllen = sizeof ctl.buf };
      struct cmsghdr *cm = CMSG_FIRSTHDR (&m);
      cm->cmsg_level = SOL_SOCKET;
      cm->cmsg_type = SCM_RIGHTS;
      cm->cmsg_len = CMSG_LEN (sizeof (int));
      memcpy (CMSG_DATA (cm), &fd, sizeof (int));
      if (sendmsg (c, &m, MSG_NOSIGNAL) < 0) { }
      close (fd);
    }
  else
    {
      snprintf (rep, sizeof rep, "ERR %d", err);
      if (send (c, rep, strlen (rep) + 1, MSG_NOSIGNAL) < 0) { }
    }
  close (c);
}

int
main (void)
{
  struct stat st;
  if (stat ("/", &st) == 0)
    {
      root_part = st.st_dev;
      root_disk = whole_disc (st.st_dev);
    }
  mkdir (CTL, 0755);
  if (chown (CTL, RO_UID, RO_GID)) { }
  scan ();
  write_list ();
  if (!ndiscs)
    return 0;			/* nothing to serve */
  int l = socket (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  struct sockaddr_un a = { .sun_family = AF_UNIX };
  static const char nm[] = "rol-discd-discs";
  memcpy (a.sun_path + 1, nm, sizeof nm - 1);	/* abstract */
  if (l < 0 || bind (l, (struct sockaddr *) &a, offsetof (struct sockaddr_un, sun_path) + sizeof nm)
      || listen (l, 4))
    return 1;
  for (;;)
    serve_one (l);
}
