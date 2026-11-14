/* rol-modd: loads Linux's driver modules for the RISC OS for Linux images.
 *
 * The images have no udev and no modprobe. The kernels have the drivers
 * the machines themselves need built in; drivers for things people plug
 * in or fit (USB and PCI Wi-Fi and Ethernet adapters, mobile broadband
 * sticks) are modules in /usr/lib/modules/<kernel release>, and this loads
 * the ones the hardware asks for:
 *
 *   - at start-up, for every device Linux has found (each device's
 *     "modalias" in /sys/devices);
 *   - afterwards, as devices appear (the kernel's uevents).
 *
 * A device's modalias is looked up in the kernel's modules.alias (made by
 * depmod when the kernel package is built); the modules it names are
 * loaded, each after the ones modules.dep says it needs.
 *
 *   /riscos/modules/blacklist   modules never to load, one name a line
 *   /riscos/modules/load        modules to load at start-up whatever the
 *                               hardware says, one name a line
 *   (lines starting # are comments; RISC OS sees both as HardDisc4's
 *   neighbours, in IXFS:$.riscos.modules)
 *
 * Huawei mobile broadband sticks start as a CD drive holding Windows
 * software; they are sent Huawei's message that turns them into a network
 * device (what usb_modeswitch does).
 *
 * Options: -v say more; -1 start-up pass only, then exit; -n load
 * nothing, only say what would be; -m DIR the modules directory (instead
 * of /usr/lib/modules/<release>); -a ALIAS look one alias up and load for
 * it, then exit.
 *
 * What it does goes to the kernel's log (kernel-log.txt in the FAT
 * partition's LOGS), or to stderr with -n or -a.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

static int verbose, dry, to_stderr;
static char moddir[300];
static int kmsg = -1;

static void
say (const char *fmt, ...)
{
  char b[600];
  va_list ap;
  va_start (ap, fmt);
  int n = snprintf (b, sizeof b, "rol-modd: ");
  n += vsnprintf (b + n, sizeof b - n - 2, fmt, ap);
  va_end (ap);
  if (n > (int) sizeof b - 2)
    n = sizeof b - 2;
  b[n++] = '\n';
  if (to_stderr || kmsg < 0)
    {
      if (write (2, b, n)) { }
    }
  else if (write (kmsg, b, n)) { }
}

/* --- the kernel's module tables ----------------------------------------- */

struct mod { char *name; char *path; char *deps; };     /* deps: paths */
struct alias { char *pattern; char *name; };
static struct mod *mods;
static int nmods;
static struct alias *aliases;
static int naliases;
static char **black;
static int nblack;

/* module names: '-' and '_' are the same */
static void
norm (char *s)
{
  for (; *s; s++)
    if (*s == '-')
      *s = '_';
}

static char *
slurp (const char *path)
{
  int fd = open (path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return NULL;
  size_t cap = 1 << 16, len = 0;
  char *b = malloc (cap);
  ssize_t n;
  while (b && (n = read (fd, b + len, cap - len - 1)) > 0)
    {
      len += n;
      if (cap - len < 4096)
        b = realloc (b, cap *= 2);
    }
  close (fd);
  if (b)
    b[len] = 0;
  return b;
}

/* the name a path stands for: kernel/drivers/x/foo-bar.ko -> foo_bar */
static char *
name_of (const char *path)
{
  const char *b = strrchr (path, '/');
  char *n = strdup (b ? b + 1 : path);
  char *dot = strchr (n, '.');
  if (dot)
    *dot = 0;
  norm (n);
  return n;
}

static struct mod *
find_mod (const char *name)
{
  char n[100];
  snprintf (n, sizeof n, "%s", name);
  norm (n);
  for (int i = 0; i < nmods; i++)
    if (!strcmp (mods[i].name, n))
      return &mods[i];
  return NULL;
}

static void
read_tables (void)
{
  char p[400];
  snprintf (p, sizeof p, "%s/modules.dep", moddir);
  char *t = slurp (p);
  for (char *l = t; l && *l; )
    {
      char *e = strchr (l, '\n');
      if (e)
        *e = 0;
      char *c = strchr (l, ':');
      if (c)
        {
          *c = 0;
          mods = realloc (mods, (nmods + 1) * sizeof *mods);
          mods[nmods].path = l;
          mods[nmods].name = name_of (l);
          mods[nmods].deps = c + 1;
          nmods++;
        }
      if (!e)
        break;
      l = e + 1;
    }
  snprintf (p, sizeof p, "%s/modules.alias", moddir);
  t = slurp (p);
  for (char *l = t; l && *l; )
    {
      char *e = strchr (l, '\n');
      if (e)
        *e = 0;
      if (!strncmp (l, "alias ", 6))
        {
          char *pat = l + 6, *sp = strrchr (pat, ' ');
          if (sp)
            {
              *sp = 0;
              norm (sp + 1);
              aliases = realloc (aliases, (naliases + 1) * sizeof *aliases);
              aliases[naliases].pattern = pat;
              aliases[naliases].name = sp + 1;
              naliases++;
            }
        }
      if (!e)
        break;
      l = e + 1;
    }
}

/* a file of names, one a line; FN is called with each */
static void
each_line (const char *path, void (*fn) (char *))
{
  char *t = slurp (path);
  for (char *l = t; l && *l; )
    {
      char *e = strchr (l, '\n');
      if (e)
        *e = 0;
      while (*l == ' ' || *l == '\t')
        l++;
      l[strcspn (l, " \t\r")] = 0;
      if (*l && *l != '#')
        fn (l);
      if (!e)
        break;
      l = e + 1;
    }
}

static void
add_black (char *n)
{
  norm (n);
  black = realloc (black, (nblack + 1) * sizeof *black);
  black[nblack++] = n;
}

static int
is_black (const char *n)
{
  for (int i = 0; i < nblack; i++)
    if (!strcmp (black[i], n))
      return 1;
  return 0;
}

/* --- loading ------------------------------------------------------------ */

static int
is_loaded (const char *name)
{
  char p[200];
  snprintf (p, sizeof p, "/sys/module/%s", name);
  return access (p, F_OK) == 0;
}

static int
insert (const char *rel, const char *name, const char *why)
{
  if (is_loaded (name))
    return 0;
  if (dry)
    {
      say ("would load %s (%s)", name, why);
      return 0;
    }
  char p[700];
  snprintf (p, sizeof p, "%s/%s", moddir, rel);
  int fd = open (p, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      say ("%s: %s", p, strerror (errno));
      return -1;
    }
  int r = syscall (SYS_finit_module, fd, "", 0);
  int err = errno;
  close (fd);
  if (r && err != EEXIST)
    {
      say ("%s not loaded: %s (%s)", name, strerror (err), why);
      return -1;
    }
  say ("loaded %s (%s)", name, why);
  return 0;
}

static int
load (const char *name, const char *why)
{
  struct mod *m = find_mod (name);
  if (!m)
    {
      if (verbose && !is_loaded (name))
        say ("no module %s", name);
      return -1;
    }
  if (is_black (m->name))
    {
      if (verbose)
        say ("%s is in the blacklist: not loaded (%s)", m->name, why);
      return -1;
    }
  if (is_loaded (m->name))
    return 0;
  /* modules.dep lists everything needed, the nearest first: load from
     the far end */
  char *d = strdup (m->deps), *list[64];
  int n = 0;
  for (char *s = strtok (d, " \t"); s && n < 64; s = strtok (NULL, " \t"))
    list[n++] = s;
  while (n-- > 0)
    {
      char *dn = name_of (list[n]);
      insert (list[n], dn, m->name);
      free (dn);
    }
  free (d);
  return insert (m->path, m->name, why);
}

static void
load_alias (const char *alias)
{
  int found = 0;
  for (int i = 0; i < naliases; i++)
    if (fnmatch (aliases[i].pattern, alias, 0) == 0)
      {
        found = 1;
        load (aliases[i].name, alias);
      }
  if (!found && (verbose || to_stderr) && !dry)
    say ("nothing for %s", alias);
  else if (!found && dry && verbose)
    say ("nothing for %s", alias);
}

static void
load_listed (char *n)
{
  load (n, "/riscos/modules/load");
}

/* --- Huawei mobile broadband sticks ------------------------------------- */

static long
read_hex (const char *dir, const char *leaf)
{
  char p[600], b[32];
  snprintf (p, sizeof p, "%s/%s", dir, leaf);
  int fd = open (p, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n = read (fd, b, sizeof b - 1);
  close (fd);
  if (n <= 0)
    return -1;
  b[n] = 0;
  return strtol (b, NULL, 16);
}

static long
read_num (const char *dir, const char *leaf)
{
  char p[600], b[32];
  snprintf (p, sizeof p, "%s/%s", dir, leaf);
  int fd = open (p, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n = read (fd, b, sizeof b - 1);
  close (fd);
  if (n <= 0)
    return -1;
  b[n] = 0;
  return strtol (b, NULL, 10);
}

/* SYS: a USB interface's directory in /sys (.../1-1.2:1.0). If it is a
   Huawei device's mass storage interface, and the device has nothing but
   mass storage (so it is still pretending to be a CD drive, or is a plain
   memory stick: the message does a memory stick no harm, it is a SCSI
   command it rejects), send the switch message to its bulk OUT endpoint. */
static void
huawei (const char *sys)
{
  char dev[600];
  snprintf (dev, sizeof dev, "%s/..", sys);
  if (read_hex (dev, "idVendor") != 0x12d1 || read_hex (sys, "bInterfaceClass") != 8)
    return;
  long nif = read_num (dev, "bNumInterfaces");
  int ifnum = (int) read_hex (sys, "bInterfaceNumber");
  /* only mass storage interfaces on the device? */
  DIR *d = opendir (dev);
  struct dirent *e;
  int other = 0, out_ep = -1;
  while (d && (e = readdir (d)))
    if (strchr (e->d_name, ':'))
      {
        char p[900];
        snprintf (p, sizeof p, "%s/%s", dev, e->d_name);
        if (read_hex (p, "bInterfaceClass") != 8)
          other = 1;
      }
  if (d)
    closedir (d);
  if (other || nif < 1)
    return;
  d = opendir (sys);
  while (d && (e = readdir (d)))
    if (!strncmp (e->d_name, "ep_", 3))
      {
        char p[900];
        snprintf (p, sizeof p, "%s/%s", sys, e->d_name);
        long addr = read_hex (p, "bEndpointAddress"), attr = read_hex (p, "bmAttributes");
        if (addr >= 0 && !(addr & 0x80) && (attr & 3) == 2)
          out_ep = (int) addr;
      }
  if (d)
    closedir (d);
  long bus = read_num (dev, "busnum"), num = read_num (dev, "devnum");
  if (out_ep < 0 || bus < 0 || num < 0)
    return;
  if (dry)
    {
      say ("would switch the Huawei stick at USB %ld/%ld to its network mode", bus, num);
      return;
    }
  char node[64];
  snprintf (node, sizeof node, "/dev/bus/usb/%03ld/%03ld", bus, num);
  int fd = open (node, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      say ("Huawei stick: %s: %s", node, strerror (errno));
      return;
    }
  /* Huawei's "new mode" message, as usb_modeswitch sends it */
  static unsigned char msg[31] = {
    0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78, 0, 0, 0, 0, 0, 0, 0, 0x11,
    0x06, 0x20, 0, 0, 0, 0x01, 0x01, 0, 0x01, 0, 0, 0, 0, 0, 0 };
  struct usbdevfs_ioctl dis = { .ifno = ifnum, .ioctl_code = USBDEVFS_DISCONNECT };
  ioctl (fd, USBDEVFS_IOCTL, &dis);
  unsigned int i = ifnum;
  ioctl (fd, USBDEVFS_CLAIMINTERFACE, &i);
  struct usbdevfs_bulktransfer bt = { .ep = out_ep, .len = sizeof msg, .timeout = 2000, .data = msg };
  int r = ioctl (fd, USBDEVFS_BULK, &bt);
  say ("Huawei stick at USB %ld/%ld: asked to switch to its network mode%s%s", bus, num,
       r < 0 ? ": " : "", r < 0 ? strerror (errno) : "");
  ioctl (fd, USBDEVFS_RELEASEINTERFACE, &i);
  close (fd);
}

/* --- devices ------------------------------------------------------------ */

static void
device (const char *sys, const char *modalias)
{
  if (modalias && *modalias)
    load_alias (modalias);
  if (strstr (sys, "/usb"))
    huawei (sys);
}

/* every device there is: each directory in /sys/devices with a modalias */
static void
coldplug (char *dir, size_t cap, int depth)
{
  size_t l = strlen (dir);
  DIR *d = opendir (dir);
  if (!d)
    return;
  struct dirent *e;
  int has = 0;
  while ((e = readdir (d)))
    {
      if (e->d_name[0] == '.')
        continue;
      if (!strcmp (e->d_name, "modalias"))
        {
          has = 1;
          continue;
        }
      if (e->d_type != DT_DIR || depth > 24 || l + strlen (e->d_name) + 2 > cap)
        continue;
      snprintf (dir + l, cap - l, "/%s", e->d_name);
      coldplug (dir, cap, depth + 1);
      dir[l] = 0;
    }
  closedir (d);
  if (has)
    {
      char p[1100], a[512];
      snprintf (p, sizeof p, "%s/modalias", dir);
      int fd = open (p, O_RDONLY | O_CLOEXEC);
      ssize_t n = fd >= 0 ? read (fd, a, sizeof a - 1) : -1;
      if (fd >= 0)
        close (fd);
      if (n > 0)
        {
          a[n] = 0;
          a[strcspn (a, "\n")] = 0;
          device (dir, a);
        }
    }
}

int
main (int argc, char **argv)
{
  int once = 0, o;
  const char *one = NULL;
  struct utsname u;
  uname (&u);
  snprintf (moddir, sizeof moddir, "/usr/lib/modules/%s", u.release);
  while ((o = getopt (argc, argv, "v1nm:a:")) != -1)
    switch (o)
      {
      case 'v': verbose = 1; break;
      case '1': once = 1; break;
      case 'n': dry = to_stderr = 1; break;
      case 'm': snprintf (moddir, sizeof moddir, "%s", optarg); break;
      case 'a': one = optarg; to_stderr = 1; break;
      default:
        fprintf (stderr, "rol-modd [-v] [-1] [-n] [-m DIR] [-a ALIAS]\n");
        return 2;
      }
  if (!to_stderr)
    kmsg = open ("/dev/kmsg", O_WRONLY | O_CLOEXEC);

  /* the uevent socket first, so nothing that appears during the start-up
     pass is missed */
  int nl = -1;
  if (!once && !one)
    {
      nl = socket (AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
      struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
      int size = 1 << 20;
      if (nl >= 0)
        setsockopt (nl, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof size);
      if (nl < 0 || bind (nl, (struct sockaddr *) &sa, sizeof sa))
        say ("no uevents (%s): devices plugged in later get no driver", strerror (errno));
    }

  read_tables ();
  each_line ("/riscos/modules/blacklist", add_black);
  if (nmods == 0)
    {
      /* a kernel without modules (or its package isn't installed): nothing
         to do, and nothing to stay for */
      say ("no modules in %s", moddir);
      return one ? 1 : 0;
    }
  if (verbose)
    say ("%d modules, %d aliases in %s", nmods, naliases, moddir);
  if (one)
    {
      load_alias (one);
      return 0;
    }
  each_line ("/riscos/modules/load", load_listed);
  char dir[1024] = "/sys/devices";
  coldplug (dir, sizeof dir, 0);
  if (once || nl < 0)
    return 0;

  for (;;)
    {
      char b[8192];
      ssize_t n = recv (nl, b, sizeof b - 1, 0);
      if (n < 0)
        {
          if (errno == EINTR || errno == ENOBUFS)
            continue;
          say ("uevents: %s", strerror (errno));
          return 1;
        }
      b[n] = 0;
      /* "add@/devices/..." then KEY=value strings, each ended by a NUL */
      if (strncmp (b, "add@", 4))
        continue;
      char sys[sizeof b + 8];
      snprintf (sys, sizeof sys, "/sys%s", b + 4);
      const char *alias = NULL;
      for (char *s = b + strlen (b) + 1; s < b + n; s += strlen (s) + 1)
        if (!strncmp (s, "MODALIAS=", 9))
          alias = s + 9;
      device (sys, alias);
    }
}
