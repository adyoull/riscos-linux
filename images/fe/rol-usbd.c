/* rol-usbd: gives RISC OS the USB devices Linux has no driver for.
 *
 * RISC OS's USBDriver (the RISC OS for Linux one: modules/USBLinux) talks
 * to USB devices through Linux's usbfs nodes, /dev/bus/usb/BBB/DDD. With
 * RISC OS running as the user "riscos", it can only open the nodes this
 * gives it: those of devices none of whose interfaces Linux has a driver
 * bound to (phones and tablets in MTP or PTP mode, cameras, ...), once
 * they've been on the bus for two seconds (Linux binds its drivers as the
 * device arrives). Keyboards, mice, discs, sound and network adapters stay
 * Linux's. Hubs are never given.
 *
 * It also mounts USB discs and SD/MMC cards (not the disc Linux runs
 * from) in /riscos/media/NAME, where RISC OS sees them as
 * IXFS:$.riscos.media.NAME (HardDisc4's icon task puts each on the icon
 * bar): FAT, exFAT, NTFS and ext partitions, named by their volume label
 * (or USBDisc1, SDCard1...). FAT, exFAT and NTFS are RISC OS's user's; ext
 * keeps its owners. RISC OS asks to eject one by making the file
 * /riscos/mediactl/eject-NAME; the mounted discs are listed, one name a
 * line, in /riscos/mediactl/discs. A disc pulled out without being
 * ejected is unmounted at once (lazily).
 *
 * It looks every second (there's no udev on these images).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#define SYS "/sys/bus/usb/devices"
#define RO_UID 1000
#define RO_GID 1000

static int
read_int (const char *dir, const char *leaf, int base)
{
  char p[512], b[32];
  snprintf (p, sizeof p, "%s/%s/%s", SYS, dir, leaf);
  int f = open (p, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return -1;
  ssize_t n = read (f, b, sizeof b - 1);
  close (f);
  if (n <= 0)
    return -1;
  b[n] = 0;
  return (int) strtol (b, NULL, base);
}

/* seen[bus][dev]: when it was first seen (0: not yet), so a device is only
   given once it has been there a while */
static time_t seen[128][128];

static void
look (void)
{
  DIR *d = opendir (SYS);
  if (!d)
    return;
  static char present[128][128];
  memset (present, 0, sizeof present);
  struct dirent *e;
  time_t now = time (NULL);
  while ((e = readdir (d)))
    {
      const char *n = e->d_name;
      if (n[0] < '0' || n[0] > '9' || strchr (n, ':'))
	continue;		/* devices only (not usbN roots, not interfaces) */
      int bus = read_int (n, "busnum", 10), dev = read_int (n, "devnum", 10);
      int cls = read_int (n, "bDeviceClass", 16);
      if (bus <= 0 || bus > 127 || dev <= 0 || dev > 127 || cls == 9)
	continue;
      present[bus][dev] = 1;
      if (!seen[bus][dev])
	seen[bus][dev] = now;
      if (now - seen[bus][dev] < 2)
	continue;
      /* any interface with a driver? */
      int ifaces = 0, driven = 0;
      DIR *s = opendir (SYS);
      struct dirent *f;
      size_t nl = strlen (n);
      while (s && (f = readdir (s)))
	if (!strncmp (f->d_name, n, nl) && f->d_name[nl] == ':')
	  {
	    char p[600];
	    struct stat st;
	    ifaces++;
	    snprintf (p, sizeof p, "%s/%s/driver", SYS, f->d_name);
	    if (lstat (p, &st) == 0)
	      driven = 1;
	  }
      if (s)
	closedir (s);
      char node[64];
      snprintf (node, sizeof node, "/dev/bus/usb/%03d/%03d", bus, dev);
      struct stat st;
      if (stat (node, &st))
	continue;
      int give = ifaces > 0 && !driven;
      if (give && st.st_uid != RO_UID)
	{
	  if (chown (node, RO_UID, RO_GID) == 0)
	    chmod (node, 0660);
	}
      else if (!give && st.st_uid == RO_UID)
	{
	  if (chown (node, 0, 0) == 0)	/* (Linux has taken it since) */
	    chmod (node, 0664);
	}
    }
  closedir (d);
  for (int b = 0; b < 128; b++)
    for (int v = 0; v < 128; v++)
      if (!present[b][v])
	seen[b][v] = 0;
}

/* --- USB discs and SD cards --------------------------------------- */

#define MEDIA "/riscos/media"
#define CTL "/riscos/mediactl"
#define MAXD 32

struct disc
{
  char dev[32];			/* sdc1, mmcblk1p1, sdd (no partition table) */
  char name[32];		/* its directory in /riscos/media */
  dev_t num;
  int mounted, ejected, tries;
  int unreadable;		/* tried and no file system found */
  int filecore;			/* its partition is RISC OS's (&AD) */
  int given;			/* a FileCore disc, given to RISC OS (SCSILinux) */
  long long bytes;		/* its size, when given */
  char fcname[12];		/* its FileCore disc name, when given */
  int removable;		/* Linux's removable flag (of the whole disc) */
};
static struct disc discs[MAXD];
static dev_t root_disk;		/* the whole disc Linux runs from */

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

/* the whole disc that block device MAJ:MIN is on (itself if it is one) */
static dev_t
whole_disc (dev_t d)
{
  char p[128], t[512];
  snprintf (p, sizeof p, "/sys/dev/block/%u:%u/partition", major (d), minor (d));
  if (access (p, F_OK))
    return d;
  snprintf (p, sizeof p, "/sys/dev/block/%u:%u/../dev", major (d), minor (d));
  unsigned ma, mi;
  if (read_file (p, t, sizeof t) > 0 && sscanf (t, "%u:%u", &ma, &mi) == 2)
    return makedev (ma, mi);
  return d;
}

/* a disc RISC OS should get: on USB, or a removable one (an SD card in a
   reader), never the one Linux runs from */
/* An SD card in a slot of the machine's own (a Pi's, a laptop's): Linux
   doesn't mark those removable, but the card is - unlike eMMC soldered
   on, which the MMC layer calls "MMC" */
static int
sd_card (const char *blk)
{
  char p[300], t[16];
  if (strncmp (blk, "mmcblk", 6))
    return 0;
  snprintf (p, sizeof p, "/sys/block/%s/device/type", blk);
  return read_file (p, t, sizeof t) > 0 && !strncmp (t, "SD", 2);
}

static int
wanted_disc (const char *blk)
{
  /* USB discs, removable cards and SD cards in the machine's own slot
     (mounted for IXFS, or given to RISC OS if FileCore): never the
     machine's internal discs */
  char p[300], t[1024];
  if (strncmp (blk, "sd", 2) && strncmp (blk, "mmcblk", 6))
    return 0;
  if (strstr (blk, "boot") || strstr (blk, "rpmb"))
    return 0;
  snprintf (p, sizeof p, "/sys/block/%s/dev", blk);
  unsigned ma, mi;
  if (read_file (p, t, sizeof t) <= 0 || sscanf (t, "%u:%u", &ma, &mi) != 2)
    return 0;
  if (makedev (ma, mi) == root_disk)
    return 0;
  snprintf (p, sizeof p, "/sys/block/%s", blk);
  ssize_t n = readlink (p, t, sizeof t - 1);
  if (n > 0)
    {
      t[n] = 0;
      if (strstr (t, "/usb"))
	return 1;
    }
  if (sd_card (blk))
    return 1;
  snprintf (p, sizeof p, "/sys/block/%s/removable", blk);
  return read_file (p, t, sizeof t) > 0 && t[0] == '1';
}

/* the volume's label, from its first sectors: FAT, exFAT, ext */
static void
label_of (const char *node, char *out, size_t n)
{
  unsigned char b[4096];
  out[0] = 0;
  int f = open (node, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return;
  if (pread (f, b, sizeof b, 0) != (ssize_t) sizeof b)
    {
      close (f);
      return;
    }
  char l[64] = "";
  if (!memcmp (b + 3, "EXFAT   ", 8))
    {
      /* exFAT: the label is an entry (type 0x83) in the root directory */
      unsigned bps = 1u << b[108], spc = 1u << b[109];
      uint32_t heap = b[88] | b[89] << 8 | b[90] << 16 | (uint32_t) b[91] << 24;
      uint32_t root = b[96] | b[97] << 8 | b[98] << 16 | (uint32_t) b[99] << 24;
      unsigned char c[4096];
      off_t at = ((off_t) heap + (off_t) (root - 2) * spc) * bps;
      if (root >= 2 && pread (f, c, sizeof c, at) == (ssize_t) sizeof c)
	for (unsigned i = 0; i + 32 <= sizeof c && c[i]; i += 32)
	  if (c[i] == 0x83)
	    {
	      unsigned k = c[i + 1] > 11 ? 11 : c[i + 1], j;
	      for (j = 0; j < k; j++)
		l[j] = c[i + 2 + 2 * j + 1] ? '_' : (char) c[i + 2 + 2 * j];
	      l[j] = 0;
	      break;
	    }
    }
  else if (b[510] == 0x55 && b[511] == 0xAA && (!memcmp (b + 82, "FAT32", 5) || !memcmp (b + 54, "FAT", 3)))
    {
      memcpy (l, b + (!memcmp (b + 82, "FAT32", 5) ? 71 : 43), 11);
      l[11] = 0;
      if (!strncmp (l, "NO NAME", 7))
	l[0] = 0;
    }
  else if (b[1024 + 56] == 0x53 && b[1024 + 57] == 0xEF)	/* ext2/3/4 */
    {
      memcpy (l, b + 1024 + 120, 16);
      l[16] = 0;
    }
  close (f);
  /* a RISC OS leaf name: letters, digits, - and _ (spaces and the rest
     become _), no trailing ones */
  size_t j = 0;
  for (size_t i = 0; l[i] && j + 1 < n && j < 24; i++)
    {
      unsigned char ch = (unsigned char) l[i];
      out[j++] = (isalnum (ch) || ch == '-') ? (char) ch : '_';
    }
  while (j && out[j - 1] == '_')
    j--;
  out[j] = 0;
}

static int
name_used (const char *nm)
{
  for (int i = 0; i < MAXD; i++)
    if (discs[i].dev[0] && discs[i].mounted && !strcasecmp (discs[i].name, nm))
      return 1;
  return !strcasecmp (nm, "HardDisc4");
}

static void
write_list (void)
{
  char tmp[] = CTL "/.discs";
  FILE *f = fopen (tmp, "we");
  if (!f)
    return;
  for (int i = 0; i < MAXD; i++)
    if (discs[i].dev[0] && discs[i].mounted)
      fprintf (f, "%s\n", discs[i].name);
  fclose (f);
  if (chown (tmp, RO_UID, RO_GID)) { }
  rename (tmp, CTL "/discs");
}

/* --- FileCore discs, for RISC OS's SCSIFS (through SCSILinux) -------

   A disc (or partition) whose boot block (bytes &C00-&DFF) has FileCore's
   checksum and a sensible disc record is RISC OS's own format: it isn't
   mounted, and RISC OS gets it whole. /riscos/mediactl/filecore lists them
   (device, size in bytes, FileCore disc name, removable 1/0); SCSILinux, in RISC OS, asks
   for one on the abstract socket "rol-usbd-discs" ("OPEN sdb") and gets an
   open file for it (read-write, or read-only if write-protected) - RISC OS
   can't open Linux's /dev itself. */
static int
filecore_disc (const char *node, char *name, size_t nsz)
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
  /* FileCore's checksum: from the end backwards, adding with the carry */
  unsigned sum = 0, c = 0;
  for (int i = 510; i >= 0; i--)
    {
      sum += b[i] + c;
      c = sum >> 8;
      sum &= 0xFF;
    }
  if (sum != b[511])
    return 0;
  const unsigned char *dr = b + 0x1C0;		/* the disc record */
  unsigned log2ss = dr[0], nzones = dr[9] | (dr[42] << 8);
  if (log2ss < 8 || log2ss > 12 || dr[5] > 16 || nzones == 0)
    return 0;
  /* the ID length FileCore uses (8 to 21 bits), and a size that fits the
     disc and is most of it: so that a FAT disc whose bytes happen to sum
     right is never taken for FileCore */
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
  return 1;
}

static void
write_filecore_list (void)
{
  char tmp[] = CTL "/.filecore";
  FILE *f = fopen (tmp, "we");
  if (!f)
    return;
  for (int i = 0; i < MAXD; i++)
    if (discs[i].dev[0] && discs[i].given)
      fprintf (f, "%s\t%lld\t%s\t%d\n", discs[i].dev, discs[i].bytes, discs[i].fcname, discs[i].removable);
  fclose (f);
  if (chown (tmp, RO_UID, RO_GID)) { }
  rename (tmp, CTL "/filecore");
}

/* A FileCore disc: give it to RISC OS instead of mounting it */
static int
give_if_filecore (struct disc *d)
{
  char node[64];
  snprintf (node, sizeof node, "/dev/%s", d->dev);
  if (!filecore_disc (node, d->fcname, sizeof d->fcname))
    return 0;
  int f = open (node, O_RDONLY | O_CLOEXEC);
  d->bytes = 0;
  if (f >= 0)
    {
      d->bytes = lseek (f, 0, SEEK_END);
      close (f);
    }
  d->given = d->filecore = 1;
  write_filecore_list ();
  char msg[200];
  snprintf (msg, sizeof msg, "rol-usbd: %s: FileCore disc \"%s\", %lld bytes: given to RISC OS (SCSIFS)\n",
	    d->dev, d->fcname, d->bytes);
  int k = open ("/dev/kmsg", O_WRONLY | O_CLOEXEC);
  if (k >= 0)
    {
      if (write (k, msg, strlen (msg)) < 0) { }
      close (k);
    }
  return 1;
}

/* the socket SCSILinux asks on */
static int disc_listen = -1;

static void
disc_serve_start (void)
{
  disc_listen = socket (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (disc_listen < 0)
    return;
  struct sockaddr_un a = { .sun_family = AF_UNIX };
  static const char nm[] = "rol-usbd-discs";
  memcpy (a.sun_path + 1, nm, sizeof nm - 1);	/* abstract */
  if (bind (disc_listen, (struct sockaddr *) &a, offsetof (struct sockaddr_un, sun_path) + sizeof nm)
      || listen (disc_listen, 4))
    {
      close (disc_listen);
      disc_listen = -1;
    }
}

/* One request: "OPEN dev" -> "OK bytes readonly" with the open disc, or
   "ERR errno" */
static void
disc_serve_one (void)
{
  int c = accept4 (disc_listen, NULL, NULL, SOCK_CLOEXEC);
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
      for (int i = 0; i < MAXD; i++)
	if (discs[i].dev[0] && discs[i].given && !strcmp (discs[i].dev, req + 5))
	  {
	    char node[64];
	    snprintf (node, sizeof node, "/dev/%s", discs[i].dev);
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
      memcpy (CMSG_DATA (cm), &fd, sizeof fd);
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

static void
try_mount (struct disc *d)
{
  char node[64], mp[128], nm[32];
  snprintf (node, sizeof node, "/dev/%s", d->dev);
  label_of (node, nm, sizeof nm);
  int sd = !strncmp (d->dev, "mmcblk", 6);
  for (int k = 1; !nm[0] || name_used (nm); k++)
    {
      snprintf (nm, sizeof nm, "%s%d", sd ? "SDCard" : "USBDisc", k);
      if (k > 99)
	return;
    }
  snprintf (mp, sizeof mp, MEDIA "/%s", nm);
  mkdir (mp, 0755);
  char opt[200];
  /* files not executable (fmask 0133): FAT, exFAT and NTFS can't store the
     bit, and IXFS calls an executable file with no RISC OS type "UNIX Ex";
     without it, IXFS types the file from its extension (MimeMap) */
  snprintf (opt, sizeof opt, "uid=%d,gid=%d,fmask=0133,dmask=0022", RO_UID, RO_GID);
  static const struct { const char *type, *extra; } fs[] = {
    {"vfat", ",shortname=mixed,flush,utf8"},
    {"vfat", ",shortname=mixed,flush"},
    {"exfat", ",iocharset=utf8"},
    {"exfat", ""},
    {"ntfs3", ",iocharset=utf8"},
    {"ntfs3", ""},
    {"ext4", NULL},
  };
  const unsigned long fl = MS_NOSUID | MS_NODEV | MS_NOATIME;
  for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++)
    {
      char o[256];
      snprintf (o, sizeof o, "%s%s", opt, fs[i].extra ? fs[i].extra : "");
      if (mount (node, mp, fs[i].type, fl, fs[i].extra ? o : NULL) == 0)
	{
	  snprintf (d->name, sizeof d->name, "%s", nm);
	  d->mounted = 1;
	  write_list ();
	  return;
	}
    }
  rmdir (mp);
  if (d->tries == 3)
    d->unreadable = 1;
  if (d->tries == 3)
    {
      /* say what it is, once: the partition's type in the disc's MBR, and
         its first bytes (a FileCore disc - RISC OS's own format - can't
         be read by Linux) */
      unsigned char b[512];
      int ptype = -1;
      char whole[64];
      size_t l = strlen (d->dev);
      while (l > 0 && d->dev[l - 1] >= '0' && d->dev[l - 1] <= '9')
	l--;
      int pn = atoi (d->dev + l);			/* sdb1 -> 1; sdb -> 0 */
      if (l > 0 && d->dev[l - 1] == 'p' && !strncmp (d->dev, "mmcblk", 6))
	l--;
      snprintf (whole, sizeof whole, "/dev/%.*s", (int) l, d->dev);
      int f = open (whole, O_RDONLY | O_CLOEXEC);
      if (f >= 0)
	{
	  if (strcmp (whole + 5, d->dev) && pn >= 1 && pn <= 4 && pread (f, b, 512, 0) == 512 && b[510] == 0x55 && b[511] == 0xAA)
	    ptype = b[0x1BE + (pn - 1) * 16 + 4];
	  close (f);
	}
      memset (b, 0, 16);
      f = open (node, O_RDONLY | O_CLOEXEC);
      if (f >= 0)
	{
	  if (pread (f, b, 512, 0) != 512)
	    memset (b, 0, 16);
	  close (f);
	}
      d->filecore = ptype == 0xAD;
      char msg[300];
      snprintf (msg, sizeof msg, "rol-usbd: %s: no FAT, exFAT, NTFS or ext4 file system on it (partition type %s%02X; starts %02X %02X %02X %02X %02X %02X %02X %02X%s)\n",
	       d->dev, ptype < 0 ? "none " : "&", ptype < 0 ? 0 : ptype, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
	       ptype == 0xAD ? "; RISC OS's FileCore" : "");
      /* into the kernel's log (kernel-log.txt; rol-usbd's own output goes
         nowhere) */
      int k = open ("/dev/kmsg", O_WRONLY | O_CLOEXEC);
      if (k >= 0)
	{
	  if (write (k, msg, strlen (msg)) < 0) { }
	  close (k);
	}
    }
}

static void
unmount (struct disc *d, int lazy)
{
  char mp[128];
  snprintf (mp, sizeof mp, MEDIA "/%s", d->name);
  sync ();
  if (umount (mp) && (!lazy || umount2 (mp, MNT_DETACH)))
    return;
  rmdir (mp);
  d->mounted = 0;
  write_list ();
}

/* each partition of a wanted disc (or the disc, with no partition table) */
static void
discs_look (void)
{
  char present[MAXD] = { 0 };
  DIR *b = opendir ("/sys/block");
  struct dirent *e;
  while (b && (e = readdir (b)))
    {
      if (e->d_name[0] == '.' || !wanted_disc (e->d_name))
	continue;
      char p[300], t[64];
      int parts = 0;
      DIR *s;
      snprintf (p, sizeof p, "/sys/block/%s", e->d_name);
      s = opendir (p);
      struct dirent *f;
      /* A disc RISC OS set up is FileCore across the whole disc, even with
	 a partition table (a RISC OS Pi card or SSD: the FAT "Loader"
	 partition for the Pi's firmware sits inside it). Then it's given
	 whole, and none of its partitions is mounted. */
      int whole_fc = 0;
      for (int k = 0; k < MAXD; k++)
	if (discs[k].dev[0] && discs[k].given && !strcmp (discs[k].dev, e->d_name))
	  whole_fc = 1;
      if (!whole_fc)
	{
	  int busy = 0;			/* a partition of it mounted already */
	  size_t el = strlen (e->d_name);
	  for (int k = 0; k < MAXD; k++)
	    if (discs[k].dev[0] && discs[k].mounted && !strncmp (discs[k].dev, e->d_name, el))
	      busy = 1;
	  char wn[64], fn[12];
	  snprintf (wn, sizeof wn, "/dev/%s", e->d_name);
	  if (!busy && filecore_disc (wn, fn, sizeof fn))
	    whole_fc = 1;
	}
      for (int pass = whole_fc ? 1 : 0; pass < 2; pass++)
	{
	  if (pass == 1 && parts)
	    break;
	  if (s)
	    rewinddir (s);
	  while (s && (f = readdir (s)))
	    {
	      const char *dv = NULL;
	      if (pass == 0 && !strncmp (f->d_name, e->d_name, strlen (e->d_name)) && f->d_name[strlen (e->d_name)])
		dv = f->d_name, parts++;
	      else if (pass == 1 && !strcmp (f->d_name, "size"))
		dv = e->d_name;
	      if (!dv)
		continue;
	      /* an empty card reader reads size 0 */
	      if (pass)
		snprintf (p, sizeof p, "/sys/block/%.64s/size", e->d_name);
	      else
		snprintf (p, sizeof p, "/sys/block/%.64s/%.64s/size", e->d_name, dv);
	      if (read_file (p, t, sizeof t) <= 0 || atoll (t) == 0)
		continue;
	      int i, freei = -1;
	      for (i = 0; i < MAXD; i++)
		if (discs[i].dev[0] && !strcmp (discs[i].dev, dv))
		  break;
		else if (!discs[i].dev[0] && freei < 0)
		  freei = i;
	      if (i == MAXD)
		{
		  if (freei < 0)
		    continue;
		  i = freei;
		  memset (&discs[i], 0, sizeof discs[i]);
		  snprintf (discs[i].dev, sizeof discs[i].dev, "%.31s", dv);
		}
	      present[i] = 1;
	      snprintf (p, sizeof p, "/sys/block/%.64s/removable", e->d_name);
	      discs[i].removable = (read_file (p, t, sizeof t) > 0 && t[0] == '1') || sd_card (e->d_name);
	      if (!discs[i].mounted && !discs[i].ejected && !discs[i].given && discs[i].tries < 3)
		{
		  discs[i].tries++;
		  if (!give_if_filecore (&discs[i]))
		    try_mount (&discs[i]);
		}
	    }
	}
      if (s)
	closedir (s);
    }
  if (b)
    closedir (b);
  /* gone: pulled out, or the card taken out of its reader */
  for (int i = 0; i < MAXD; i++)
    if (discs[i].dev[0] && !present[i])
      {
	if (discs[i].mounted)
	  unmount (&discs[i], 1);
	int was_given = discs[i].given;
	discs[i].dev[0] = 0;
	discs[i].given = 0;
	if (was_given)
	  write_filecore_list ();
      }
  /* RISC OS asking for one to be ejected */
  DIR *c = opendir (CTL);
  while (c && (e = readdir (c)))
    if (!strncmp (e->d_name, "eject-", 6))
      {
	char p[300];
	for (int i = 0; i < MAXD; i++)
	  if (discs[i].dev[0] && discs[i].mounted && !strcasecmp (discs[i].name, e->d_name + 6))
	    {
	      /* a program may be just finishing with it: try for a second */
	      for (int t = 0; t < 10 && discs[i].mounted; t++)
		{
		  unmount (&discs[i], 0);
		  if (discs[i].mounted)
		    usleep (100000);
		}
	      if (!discs[i].mounted)
		discs[i].ejected = 1;
	      else
		{
		  /* still in use (a file on it open): say so to RISC OS
		     (!DiscIcon reports it), and leave it mounted */
		  char b[300];
		  snprintf (b, sizeof b, CTL "/busy-%s", discs[i].name);
		  int f = open (b, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		  if (f >= 0)
		    close (f);
		  fprintf (stderr, "rol-usbd: %s (%s) is in use: not ejected\n", discs[i].dev, discs[i].name);
		}
	    }
	snprintf (p, sizeof p, CTL "/%s", e->d_name);
	unlink (p);
      }
  if (c)
    closedir (c);
}

/* --- every USB device, for *USBDevices ------------------------------ */

/* sysfs FILE of device N, one line, or "" */
static void
dev_str (const char *n, const char *leaf, char *out, size_t sz)
{
  char p[600];
  snprintf (p, sizeof p, "%s/%s/%s", SYS, n, leaf);
  out[0] = 0;
  if (read_file (p, out, sz) <= 0)
    {
      out[0] = 0;
      return;
    }
  for (char *c = out; *c; c++)
    if (*c == '\n' || *c == '\t')
      *c = 0;
  /* trailing spaces (some products pad their names) */
  size_t l = strlen (out);
  while (l && out[l - 1] == ' ')
    out[--l] = 0;
}

/* what's on block devices under USB device N (its sysfs path): the discs'
   names, or why not */
static void
dev_discs (const char *n, char *out, size_t sz)
{
  out[0] = 0;
  char want[80], p[300], t[1024];
  /* a disc's sysfs path runs through .../N/N:1.0/... (an interface of N
     itself, not of a device on a hub below it) */
  snprintf (want, sizeof want, "/%s/%s:", n, n);
  DIR *b = opendir ("/sys/block");
  struct dirent *e;
  while (b && (e = readdir (b)))
    {
      if (strncmp (e->d_name, "sd", 2))
	continue;
      snprintf (p, sizeof p, "/sys/block/%s", e->d_name);
      ssize_t l = readlink (p, t, sizeof t - 1);
      if (l <= 0)
	continue;
      t[l] = 0;
      if (!strstr (t, want))
	continue;
      snprintf (p, sizeof p, "/sys/block/%s/size", e->d_name);
      char what[200] = "";
      if (read_file (p, t, 64) <= 0 || atoll (t) == 0)
	snprintf (what, sizeof what, "no disc in it");
      else
	{
	  size_t bl = strlen (e->d_name);
	  for (int i = 0; i < MAXD; i++)
	    {
	      if (!discs[i].dev[0] || strncmp (discs[i].dev, e->d_name, bl))
		continue;
	      size_t w = strlen (what);
	      const char *sep = w ? ", " : "";
	      if (discs[i].mounted)
		snprintf (what + w, sizeof what - w, "%s%s", sep, discs[i].name);
	      else if (discs[i].given)
		snprintf (what + w, sizeof what - w, "%sFileCore disc %s (RISC OS's SCSIFS)", sep,
			  discs[i].fcname[0] ? discs[i].fcname : discs[i].dev);
	      else if (discs[i].ejected)
		snprintf (what + w, sizeof what - w, "%s%s ejected", sep, discs[i].dev);
	      else if (discs[i].unreadable)
		snprintf (what + w, sizeof what - w, "%s%s: %s", sep, discs[i].dev,
			  discs[i].filecore ? "FileCore (RISC OS's format), which Linux can't read"
			  : "no FAT, exFAT, NTFS or ext4 on it");
	    }
	  if (!what[0])
	    snprintf (what, sizeof what, "looking at it");
	}
      size_t o = strlen (out);
      snprintf (out + o, sz - o, "%s%s", o ? "; " : "", what);
    }
  if (b)
    closedir (b);
}

/* /riscos/mediactl/usb: a line for every USB device, tab-separated:
   R or L (RISC OS's or Linux's), vendor:product, speed (Mb/s), name, the
   Linux drivers, its discs; and a last line "H<tab>hubs" */
static void
write_usb_list (void)
{
  static char last[8192];
  char buf[8192];
  size_t o = 0;
  int hubs = 0;
  DIR *d = opendir (SYS);
  struct dirent *e;
  while (d && (e = readdir (d)))
    {
      const char *n = e->d_name;
      if (n[0] < '0' || n[0] > '9' || strchr (n, ':'))
	continue;
      if (read_int (n, "bDeviceClass", 16) == 9)
	{
	  hubs++;
	  continue;
	}
      char vid[16], pid[16], spd[16], man[128], prod[128], drv[200] = "", dsc[400];
      dev_str (n, "idVendor", vid, sizeof vid);
      dev_str (n, "idProduct", pid, sizeof pid);
      dev_str (n, "speed", spd, sizeof spd);
      dev_str (n, "manufacturer", man, sizeof man);
      dev_str (n, "product", prod, sizeof prod);
      /* the drivers of its interfaces */
      DIR *s = opendir (SYS);
      struct dirent *f;
      size_t nl = strlen (n);
      int ifaces = 0;
      while (s && (f = readdir (s)))
	if (!strncmp (f->d_name, n, nl) && f->d_name[nl] == ':')
	  {
	    char p[600], t[300];
	    ifaces++;
	    snprintf (p, sizeof p, "%s/%s/driver", SYS, f->d_name);
	    ssize_t l = readlink (p, t, sizeof t - 1);
	    if (l <= 0)
	      continue;
	    t[l] = 0;
	    const char *b = strrchr (t, '/') ? strrchr (t, '/') + 1 : t;
	    if (!strstr (drv, b))
	      {
		size_t dl = strlen (drv);
		snprintf (drv + dl, sizeof drv - dl, "%s%s", dl ? ", " : "", b);
	      }
	  }
      if (s)
	closedir (s);
      dev_discs (n, dsc, sizeof dsc);
      char name[260];
      if (man[0] && prod[0] && strncmp (prod, man, strlen (man)))
	snprintf (name, sizeof name, "%s %s", man, prod);
      else
	snprintf (name, sizeof name, "%s", prod[0] ? prod : man[0] ? man : "(no name)");
      int ro = ifaces > 0 && !drv[0];
      int w = snprintf (buf + o, sizeof buf - o, "%c\t%s:%s\t%s\t%s\t%s\t%s\n", ro ? 'R' : 'L',
			vid, pid, spd, name, drv, dsc);
      if (w < 0 || (size_t) w >= sizeof buf - o)
	break;
      o += (size_t) w;
    }
  if (d)
    closedir (d);
  snprintf (buf + o, sizeof buf - o, "H\t%d\n", hubs);
  if (!strcmp (buf, last))
    return;
  int f = open (CTL "/usb.new", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (f < 0)
    return;
  if (write (f, buf, strlen (buf)) < 0) { }
  close (f);
  if (rename (CTL "/usb.new", CTL "/usb") == 0)
    snprintf (last, sizeof last, "%s", buf);
}

int
main (void)
{
  struct stat st;
  if (stat ("/", &st) == 0)
    root_disk = whole_disc (st.st_dev);
  mkdir (CTL, 0755);
  if (chown (CTL, RO_UID, RO_GID)) { }
  write_list ();
  write_filecore_list ();
  disc_serve_start ();
  for (;;)
    {
      look ();
      discs_look ();
      write_usb_list ();
      /* a second between looks, answering SCSILinux meanwhile */
      struct timespec t0, t1;
      clock_gettime (CLOCK_MONOTONIC, &t0);
      for (;;)
	{
	  clock_gettime (CLOCK_MONOTONIC, &t1);
	  long ms = 1000 - ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
	  if (ms <= 0)
	    break;
	  struct pollfd p = { disc_listen, POLLIN, 0 };
	  if (disc_listen < 0)
	    {
	      poll (NULL, 0, (int) ms);
	      break;
	    }
	  if (poll (&p, 1, (int) ms) == 1)
	    disc_serve_one ();
	}
    }
}
