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
 * It looks every second (there's no udev on these images).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

int
main (void)
{
  for (;;)
    {
      look ();
      sleep (1);
    }
}
