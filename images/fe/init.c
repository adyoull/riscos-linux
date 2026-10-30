/* init for the RISC OS for Linux UTM image: mounts the usual file
 * systems, runs RISC OS full screen (rol-fb + qemu-arm-riscos + the
 * port's ROM, HardDisc4 from /riscos/HardDisc4), and powers the machine
 * off when RISC OS ends (Shutdown in the Task manager's menu).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
static void net_time (const char *dns);

/* "Raspberry Pi 5 Model B Rev 1.0" etc., or "" (a VM) */
static char model[128];

static void
read_model (void)
{
  int fd = open ("/proc/device-tree/model", O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      ssize_t n = read (fd, model, sizeof model - 1);
      model[n > 0 ? n : 0] = 0;
      close (fd);
    }
}

static void
say (const char *fmt, const char *a)
{
  char buf[512];
  int n = snprintf (buf, sizeof buf, fmt, a);
  const char *outs[] = { "/dev/ttyAMA0", "/dev/tty1" };
  for (int i = 0; i < 2; i++)
    {
      int fd = open (outs[i], O_WRONLY | O_NOCTTY | O_CLOEXEC);
      if (fd >= 0)
	{
	  if (write (fd, buf, n) < 0) { }
	  close (fd);
	}
    }
}

static void
show_log (void)
{
  char buf[4096];
  int fd = open ("/riscos/log.txt", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  off_t end = lseek (fd, 0, SEEK_END);
  lseek (fd, end > 3000 ? end - 3000 : 0, SEEK_SET);
  ssize_t n = read (fd, buf, sizeof buf - 1);
  close (fd);
  if (n > 0)
    {
      buf[n] = 0;
      say ("%s\n", buf);
    }
}


/* --- network: rol-netd, which RISC OS's Configure plug-in (Linux network)
   controls through /riscos/net/config and reads /riscos/net/status from */
static void
network_up (void)
{
  pid_t p = fork ();
  if (p == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      dup2 (null, 0);
      dup2 (null, 1);
      dup2 (null, 2);
      setsid ();
      execl ("/opt/riscos/rol-netd", "rol-netd", (char *) NULL);
      _exit (127);
    }
  if (p < 0)
    return;
  /* wait (up to 12 seconds) for an address, so RISC OS starts with its
     name servers; a wired link takes a moment to come up, Wi-Fi longer */
  char buf[2048] = "", state[32] = "", dns[100] = "", addr[32] = "";
  for (int i = 0; i < 24; i++)
    {
      usleep (500000);
      int fd = open ("/riscos/net/status", O_RDONLY | O_CLOEXEC);
      if (fd < 0)
	continue;
      ssize_t n = read (fd, buf, sizeof buf - 1);
      close (fd);
      buf[n > 0 ? n : 0] = 0;
      state[0] = dns[0] = addr[0] = 0;
      for (char *l = strtok (buf, "\n"); l; l = strtok (NULL, "\n"))
	{
	  if (!strncmp (l, "state=", 6)) snprintf (state, sizeof state, "%s", l + 6);
	  if (!strncmp (l, "dns=", 4)) snprintf (dns, sizeof dns, "%s", l + 4);
	  if (!strncmp (l, "address=", 8)) snprintf (addr, sizeof addr, "%s", l + 8);
	}
      if (!strcmp (state, "configured") || !strcmp (state, "off")
	  || !strcmp (state, "nointerface") || (i >= 8 && !strcmp (state, "nolink")))
	break;
    }
  if (addr[0])
    say ("Network: %s", addr);
  else
    say ("Network: %s (no address yet)", state[0] ? state : "starting");
  if (dns[0])
    {
      say (", DNS %s", dns);
      /* RISC OS's Resolver reads Inet$Resolvers (servers separated by spaces) */
      setenv ("RISC_OS_Inet_Resolvers", dns, 1);
    }
  say ("\n", "");
  if (dns[0] && addr[0])
    net_time (dns);
}

/* --- the time from the network (SNTP), before RISC OS starts: a machine
   whose real-time clock isn't kept (the Dev Kit has no clock battery, and
   reads the offset Windows last left) would otherwise start days out.
   pool.ntp.org through the first name server; 2 s per try at most. */
static int
udp_ask (unsigned ip, int port, const void *q, int qn, void *r, int rn)
{
  int s = socket (AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return -1;
  struct timeval tv = { 2, 0 };
  setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons (port),
			   .sin_addr.s_addr = ip };
  int n = -1;
  if (sendto (s, q, qn, 0, (struct sockaddr *) &a, sizeof a) == qn)
    n = recv (s, r, rn, 0);
  close (s);
  return n;
}

static unsigned
dns_a (unsigned server, const char *name)
{
  unsigned char q[512], r[512];
  int n = 0;
  q[n++] = 0x52; q[n++] = 0x4f;			/* id */
  q[n++] = 1; q[n++] = 0;			/* recursion desired */
  q[n++] = 0; q[n++] = 1;			/* one question */
  for (int i = 0; i < 6; i++)
    q[n++] = 0;
  for (const char *p = name; *p; )
    {
      const char *d = strchr (p, '.');
      int l = d ? d - p : (int) strlen (p);
      q[n++] = l;
      memcpy (q + n, p, l);
      n += l;
      p += l + (d ? 1 : 0);
    }
  q[n++] = 0;
  q[n++] = 0; q[n++] = 1;			/* A */
  q[n++] = 0; q[n++] = 1;			/* IN */
  int m = udp_ask (server, 53, q, n, r, sizeof r);
  if (m < 12 || r[0] != 0x52 || r[1] != 0x4f || (r[3] & 15))
    return 0;
  int an = r[6] << 8 | r[7], o = 12;
  for (int qd = r[4] << 8 | r[5]; qd > 0; qd--)	/* skip the question */
    {
      while (o < m && r[o] && !(r[o] & 0xc0))
	o += r[o] + 1;
      o += (o < m && (r[o] & 0xc0)) ? 2 : 1;
      o += 4;
    }
  for (; an > 0 && o < m; an--)
    {
      while (o < m && r[o] && !(r[o] & 0xc0))
	o += r[o] + 1;
      o += (o < m && (r[o] & 0xc0)) ? 2 : 1;
      if (o + 10 > m)
	return 0;
      int type = r[o] << 8 | r[o + 1], len = r[o + 8] << 8 | r[o + 9];
      o += 10;
      if (type == 1 && len == 4 && o + 4 <= m)
	{
	  unsigned ip;
	  memcpy (&ip, r + o, 4);
	  return ip;
	}
      o += len;
    }
  return 0;
}

static void
net_time (const char *dns)
{
  char first[32];
  int i = 0;
  while (dns[i] && dns[i] != ' ' && i < 31)
    {
      first[i] = dns[i];
      i++;
    }
  first[i] = 0;
  struct in_addr srv;
  if (!inet_aton (first, &srv))
    return;
  for (int tries = 0; tries < 2; tries++)
    {
      unsigned ip = dns_a (srv.s_addr, "pool.ntp.org");
      if (!ip)
	continue;
      unsigned char q[48] = { 0x23 }, r[48];	/* v4, client */
      if (udp_ask (ip, 123, q, 48, r, 48) < 48 || (r[0] & 7) != 4)
	continue;
      unsigned secs = (unsigned) r[40] << 24 | r[41] << 16 | r[42] << 8 | r[43];
      unsigned frac = (unsigned) r[44] << 24 | r[45] << 16 | r[46] << 8 | r[47];
      long long ut = (long long) secs - 2208988800LL;
      if (ut < 1735689600LL)			/* before 2025: not believed */
	continue;
      struct timeval tv = { (time_t) ut, (long) (((unsigned long long) frac * 1000000) >> 32) };
      if (settimeofday (&tv, NULL) == 0)
	{
	  say ("Time: set from the network\n", "");
	  return;
	}
    }
  say ("Time: not set from the network (no answer)\n", "");
}

/* --- sound: rol-snd plays what RISC OS's LinuxAudio module sends to
   /run/riscos-sound (its messages: /riscos/sound/log.txt) */
static void
sound_up (void)
{
  mkdir ("/riscos/sound", 0755);
  pid_t p = fork ();
  if (p == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      int log = open ("/riscos/sound/log.txt", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
      dup2 (null, 0);
      dup2 (log >= 0 ? log : null, 1);
      dup2 (log >= 0 ? log : null, 2);
      setsid ();
      execl ("/opt/riscos/rol-snd", "rol-snd", (char *) NULL);
      _exit (127);
    }
}

/* Test bed job mode (development): a second disk (/dev/vdb, ext4) with
   "boot" (an Obey file RISC OS runs as its boot sequence), "mode" (native
   or qemu) and "timeout" (seconds). RISC OS runs without a screen, its
   console goes to "console" on that disk, and the machine powers off. */
static void
job_mode (void)
{
  mkdir ("/job", 0755);
  if (mount ("/dev/vdb", "/job", "ext4", 0, NULL))
    return;
  char m[32] = "native", t[16] = "600";
  int fd = open ("/job/mode", O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      ssize_t n = read (fd, m, sizeof m - 1);
      m[n > 0 ? n : 0] = 0;
      m[strcspn (m, "\r\n")] = 0;
      close (fd);
    }
  fd = open ("/job/timeout", O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      ssize_t n = read (fd, t, sizeof t - 1);
      t[n > 0 ? n : 0] = 0;
      close (fd);
    }
  int native = !strcmp (m, "native");
  fd = open ("/proc/sys/vm/mmap_min_addr", O_WRONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      if (write (fd, "4096", 4) < 0) { }
      close (fd);
    }
  setenv ("RISC_OS_Alias_IXFSBoot", "Obey IXFS:$.job.boot", 1);
  pid_t pid = fork ();
  if (pid == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      int out = open ("/job/console", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      dup2 (null, 0);
      dup2 (out, 1);
      dup2 (out, 2);
      setsid ();
      if (chdir ("/job")) { }
      if (native)
	execl ("/opt/riscos/RISCOS.IMG", "RISCOS.IMG", "--abort-on-input",
	       (char *) NULL);
      else
	execl ("/opt/riscos/qemu-arm-riscos", "qemu-arm-riscos",
	       "/opt/riscos/RISCOS.IMG", "--abort-on-input", (char *) NULL);
      _exit (127);
    }
  int limit = atoi (t), st = 0, done = 0;
  for (int i = 0; i < limit * 10 && !done; i++)
    {
      if (waitpid (pid, &st, WNOHANG) == pid)
	done = 1;
      else
	usleep (100000);
    }
  if (!done)
    {
      kill (-pid, SIGKILL);
      kill (pid, SIGKILL);
      waitpid (pid, &st, 0);
    }
  char r[64];
  snprintf (r, sizeof r, "%s %d\n", done ? "status" : "timeout",
	    WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st));
  fd = open ("/job/result", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0)
    {
      if (write (fd, r, strlen (r)) < 0) { }
      close (fd);
    }
  sync ();
  umount ("/job");
  sync ();
  reboot (RB_POWER_OFF);
}

/* Testing (riscos-linux-testbed's tools/native-vm): with a 9p share
   "host" (the host's whole file system), run one program from the host in
   a chroot there, natively, and power off: /tmp/rol-native-vm/job holds
   its argv and env (NUL-separated) and cwd; stdout, stderr, status and the
   kernel's messages (dmesg) come back there. Without the share (any real
   machine), nothing happens here. */
static int
read_file (const char *path, char *buf, size_t size)
{
  int fd = open (path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n = read (fd, buf, size - 1);
  close (fd);
  if (n < 0)
    return -1;
  buf[n] = 0;
  return n;
}

static void
host_job (void)
{
  {
    int t = open ("/dev/ttyAMA0", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (t >= 0 && write (t, "host_job\n", 9) < 0) { }
    if (t >= 0) close (t);
  }
  mkdir ("/host", 0755);
  if (mount ("host", "/host", "9p", 0, "trans=virtio,version=9p2000.L,msize=524288,cache=none"))
    {
      if (errno != ENOENT)
	{
	  int t = open ("/dev/ttyAMA0", O_WRONLY | O_NOCTTY | O_CLOEXEC);
	  char m[128];
	  snprintf (m, sizeof m, "host_job: mount: %s\n", strerror (errno));
	  if (t >= 0 && write (t, m, strlen (m)) < 0) { }
	}
      return;
    }
  const char *J = "/host/tmp/rol-native-vm/job/";
  static char av[65536], ev[65536], cwd[4096], path[4200];
  snprintf (path, sizeof path, "%sargv", J);
  int an = read_file (path, av, sizeof av);
  snprintf (path, sizeof path, "%senv", J);
  int en = read_file (path, ev, sizeof ev);
  snprintf (path, sizeof path, "%scwd", J);
  read_file (path, cwd, sizeof cwd);
  cwd[strcspn (cwd, "\n")] = 0;
  int fd = open ("/proc/sys/vm/mmap_min_addr", O_WRONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      if (write (fd, "4096", 4) < 0) { }
      close (fd);
    }
  /* the kernel's reports of fatal signals, for J/dmesg */
  {
    const char *f[] = { "/proc/sys/kernel/print-fatal-signals",
			"/proc/sys/debug/exception-trace" };
    for (int i = 0; i < 2; i++)
      {
	int k = open (f[i], O_WRONLY | O_CLOEXEC);
	if (k >= 0)
	  {
	    if (write (k, "1", 1) < 0) { }
	    close (k);
	  }
      }
  }
  mount ("proc", "/host/proc", "proc", 0, NULL);
  mount ("/dev", "/host/dev", NULL, MS_BIND | MS_REC, NULL);
  char *argv[256], *envp[512];
  int na = 0, ne = 0;
  for (int i = 0; i < an && na < 255; i += strlen (av + i) + 1)
    argv[na++] = av + i;
  argv[na] = NULL;
  for (int i = 0; en > 0 && i < en && ne < 511; i += strlen (ev + i) + 1)
    envp[ne++] = ev + i;
  envp[ne] = NULL;
  int st = 127 << 8;
  if (na > 0)
    {
      pid_t pid = fork ();
      if (pid == 0)
	{
	  int in = open ("/dev/null", O_RDONLY);
	  snprintf (path, sizeof path, "%sstdout", J);
	  int out = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	  snprintf (path, sizeof path, "%sstderr", J);
	  int err = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	  dup2 (in, 0);
	  dup2 (out, 1);
	  dup2 (err, 2);
	  setsid ();
	  if (chroot ("/host") || chdir (cwd[0] ? cwd : "/"))
	    _exit (126);
	  execve (argv[0], argv, envp);
	  _exit (127);
	}
      waitpid (pid, &st, 0);
    }
  {
    snprintf (path, sizeof path, "%sdmesg", J);
    int out = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int km = open ("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    static char kb[8192];
    ssize_t n;
    while (km >= 0 && out >= 0 && (n = read (km, kb, sizeof kb)) > 0)
      if (write (out, kb, n) < 0) { }
    if (km >= 0) close (km);
    if (out >= 0) close (out);
  }
  char r[32];
  snprintf (r, sizeof r, "%d\n",
	    WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st));
  snprintf (path, sizeof path, "%sstatus", J);
  fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0)
    {
      if (write (fd, r, strlen (r)) < 0) { }
      close (fd);
    }
  sync ();
  reboot (RB_POWER_OFF);
}

/* Windows Dev Kit 2023 (Qualcomm SC8280XP): the audio and compute DSPs' and
   the GPU's firmware are Microsoft's, signed for this machine, so not on
   this image: the first time, they're copied from Windows's driver store
   (the NVMe's Windows partition, read-only) to /lib/firmware, where Linux's
   device tree names them; then the DSPs are started (they failed to at
   boot, with no firmware). The display and the USB-A ports work without. */
#define WDK_FW "/lib/firmware/qcom/sc8280xp/microsoft/blackrock"
static const char *const wdk_fw[] = { "qcadsp8280.mbn", "qccdsp8280.mbn",
				      "qcdxkmsuc8280.mbn" };

static int
copy_file (const char *from, const char *to)
{
  int in = open (from, O_RDONLY | O_CLOEXEC);
  if (in < 0)
    return -1;
  char tmp[600];
  snprintf (tmp, sizeof tmp, "%s.new", to);
  int out = open (tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  int ok = out >= 0;
  char buf[65536];
  ssize_t n;
  while (ok && (n = read (in, buf, sizeof buf)) > 0)
    ok = write (out, buf, n) == n;
  close (in);
  if (out >= 0)
    close (out);
  if (ok)
    ok = rename (tmp, to) == 0;
  else
    unlink (tmp);
  return ok ? 0 : -1;
}

static void
wdk_firmware (void)
{
  char p[512];
  int missing = 0;
  for (int i = 0; i < 3; i++)
    {
      snprintf (p, sizeof p, WDK_FW "/%s", wdk_fw[i]);
      missing += access (p, F_OK) != 0;
    }
  if (missing)
    {
      mkdir ("/lib", 0755);
      mkdir ("/lib/firmware", 0755);
      mkdir ("/lib/firmware/qcom", 0755);
      mkdir ("/lib/firmware/qcom/sc8280xp", 0755);
      mkdir ("/lib/firmware/qcom/sc8280xp/microsoft", 0755);
      mkdir (WDK_FW, 0755);
      mkdir ("/run/win", 0755);
      const char *disks[] = { "/dev/nvme0n1p", "/dev/sda", "/dev/sdb" };
      for (int d = 0; d < 3 && missing; d++)
	for (int n = 1; n <= 8 && missing; n++)
	  {
	    char dev[64];
	    snprintf (dev, sizeof dev, "%s%d", disks[d], n);
	    if (access (dev, F_OK)
		|| mount (dev, "/run/win", "ntfs3", MS_RDONLY, NULL))
	      continue;
	    DIR *dr = opendir ("/run/win/Windows/System32/DriverStore/FileRepository");
	    struct dirent *e;
	    while (dr && missing && (e = readdir (dr)))
	      for (int i = 0; i < 3; i++)
		{
		  char to[512];
		  snprintf (to, sizeof to, WDK_FW "/%s", wdk_fw[i]);
		  if (access (to, F_OK) == 0)
		    continue;
		  snprintf (p, sizeof p,
			    "/run/win/Windows/System32/DriverStore/FileRepository/%s/%s",
			    e->d_name, wdk_fw[i]);
		  if (copy_file (p, to) == 0)
		    {
		      say ("Firmware from Windows: %s\n", wdk_fw[i]);
		      missing--;
		    }
		}
	    if (dr)
	      closedir (dr);
	    umount ("/run/win");
	  }
      sync ();
      if (missing)
	say ("Windows's firmware for the DSPs and GPU wasn't found: "
	     "no sound or USB-C until it is (see README.txt).\n", "");
    }
  /* start the DSPs whose firmware is now there; but not when this system
     is on a disc in a USB-C port: the audio DSP takes those ports over
     as it starts (its USB-C and charging service), which resets them and
     takes the disc away. Then the USB-A ports and the mini DisplayPort
     still work; USB-C needs the disc in a USB-A port. */
  {
    struct stat rs;
    char link[96], path[512] = "";
    if (stat ("/", &rs) == 0)
      {
	snprintf (link, sizeof link, "/sys/dev/block/%u:%u",
		  major (rs.st_dev), minor (rs.st_dev));
	ssize_t n = readlink (link, path, sizeof path - 1);
	path[n > 0 ? n : 0] = 0;
      }
    if (strstr (path, "a600000.usb") || strstr (path, "a800000.usb"))
      {
	say ("This system is on a disc in a USB-C port: the DSPs (USB-C, "
	     "sound over USB-C) are left off, as starting them resets "
	     "those ports. Use a USB-A port for the disc to have them.\n", "");
	return;
      }
  }
  for (int r = 0; r < 8; r++)
    {
      char st[16] = "", fw[256] = "";
      snprintf (p, sizeof p, "/sys/class/remoteproc/remoteproc%d/state", r);
      int fd = open (p, O_RDONLY | O_CLOEXEC);
      if (fd < 0)
	continue;
      ssize_t k = read (fd, st, sizeof st - 1);
      close (fd);
      st[k > 0 ? k : 0] = 0;
      snprintf (p, sizeof p, "/sys/class/remoteproc/remoteproc%d/firmware", r);
      fd = open (p, O_RDONLY | O_CLOEXEC);
      k = fd >= 0 ? read (fd, fw, sizeof fw - 1) : -1;
      if (fd >= 0)
	close (fd);
      fw[k > 0 ? k : 0] = 0;
      fw[strcspn (fw, "\n")] = 0;
      char path[512];
      snprintf (path, sizeof path, "/lib/firmware/%s", fw);
      if (strncmp (st, "offline", 7) || !fw[0] || access (path, F_OK))
	continue;
      snprintf (p, sizeof p, "/sys/class/remoteproc/remoteproc%d/state", r);
      fd = open (p, O_WRONLY | O_CLOEXEC);
      if (fd >= 0)
	{
	  if (write (fd, "start", 5) < 0) { }
	  close (fd);
	}
    }
}

/* The kernel's messages, kept in /riscos/kernel-log.txt as they come (each
   written straight to the disc), so that if the machine stops later, or the
   screen goes off, they can be read on another computer. */
static void
kernel_log (void)
{
  pid_t k = fork ();
  if (k != 0)
    return;
  int in = open ("/dev/kmsg", O_RDONLY | O_CLOEXEC);
  int out = open ("/riscos/kernel-log.txt", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (in < 0 || out < 0)
    _exit (0);
  char buf[8192];
  for (;;)
    {
      ssize_t n = read (in, buf, sizeof buf);
      if (n < 0 && errno == EPIPE)
	continue;		/* messages were lost: carry on */
      if (n <= 0)
	break;
      /* "level,seq,time,flags;text": keep the time and the text */
      char *semi = memchr (buf, ';', n);
      char *c1 = memchr (buf, ',', n);
      char *c2 = c1 ? memchr (c1 + 1, ',', buf + n - c1 - 1) : NULL;
      char line[8300];
      int len;
      if (semi && c2 && c2 < semi)
	{
	  char *c3 = memchr (c2 + 1, ',', semi - c2 - 1);
	  long us = atol (c2 + 1);
	  char *nl = memchr (semi + 1, '\n', buf + n - semi - 1);
	  int tl = nl ? nl - semi - 1 : buf + n - semi - 1;
	  len = snprintf (line, sizeof line, "[%5ld.%06ld] %.*s\n",
			  us / 1000000, us % 1000000, tl, semi + 1);
	  (void) c3;
	}
      else
	len = snprintf (line, sizeof line, "%.*s", (int) n, buf);
      if (len > (int) sizeof line - 1)
	len = sizeof line - 1;
      if (write (out, line, len) < 0) { }
      fdatasync (out);
    }
  _exit (0);
}

/* --- protected RISC OS: unless /etc/riscos/protect says "off", RISC OS
   runs as the user "riscos" (uid 1000) in its own view of the files (rol-fb
   sets that up: ROL_CONFINE). The switch is in /etc, which RISC OS can only
   read, so RISC OS can't turn it off. This gives that user the devices
   RISC OS uses itself (group video) and its own folders. */
#define RO_UID 1000
#define RO_GID 1000
#define VIDEO_GID 44
static int
protect_on (void)
{
  char b[8] = "";
  int fd = open ("/etc/riscos/protect", O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      if (read (fd, b, sizeof b - 1) < 0) { }
      close (fd);
    }
  return strncmp (b, "off", 3) != 0;
}

static void
protect_setup (void)
{
  const char *own[] = { "/riscos", "/riscos/net", "/riscos/sound", "/riscos/cmd", "/riscos/HardDisc4", NULL };
  for (int i = 0; own[i]; i++)
    {
      mkdir (own[i], 0755);
      if (lchown (own[i], RO_UID, RO_GID)) { }
    }
  DIR *d = opendir ("/dev");
  struct dirent *e;
  while (d && (e = readdir (d)))
    if (!strncmp (e->d_name, "video", 5) || !strncmp (e->d_name, "media", 5)
	|| !strcmp (e->d_name, "vchiq") || !strcmp (e->d_name, "vcio"))
      {
	char p[300];
	snprintf (p, sizeof p, "/dev/%s", e->d_name);
	if (lchown (p, 0, VIDEO_GID)) { }
	chmod (p, 0660);
      }
  if (d)
    closedir (d);
  /* the sound FIFO, there before RISC OS's view of /run is made */
  struct stat st;
  if (stat ("/run/riscos-sound", &st))
    mkfifo ("/run/riscos-sound", 0666);
  chmod ("/run/riscos-sound", 0666);
  char v[32];
  snprintf (v, sizeof v, "%d:%d:%d", RO_UID, RO_GID, VIDEO_GID);
  setenv ("ROL_CONFINE", v, 1);
}

/* --- rol-cmd: runs Linux commands for RISC OS (*Linux, the updates
   application), when the image has it */
static void
cmd_up (void)
{
  if (access ("/opt/riscos/rol-cmd", X_OK) != 0)
    return;
  pid_t p = fork ();
  if (p == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      dup2 (null, 0);
      dup2 (null, 1);
      dup2 (null, 2);
      setsid ();
      execl ("/opt/riscos/rol-cmd", "rol-cmd", (char *) NULL);
      _exit (127);
    }
  /* rol-usbd: gives RISC OS the USB devices Linux has no driver for */
  if (access ("/opt/riscos/rol-usbd", X_OK) == 0 && fork () == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      dup2 (null, 0);
      dup2 (null, 1);
      dup2 (null, 2);
      setsid ();
      execl ("/opt/riscos/rol-usbd", "rol-usbd", (char *) NULL);
      _exit (127);
    }
}

/* A forgotten root password: a file RESETPW (or RESETPW.TXT) on the card's
   FAT partition (the root file system's disk, partition 1) clears it, so
   the next *Linux or !Updates asks for a new one. ROLLBACK (or
   ROLLBACK.TXT) there puts back the RISC OS ROM the last update replaced.
   The file is removed. */
static void
reset_root_password (void)
{
  struct stat st;
  char sp[128], lk[512], disk[64] = "", dev[96] = "";
  if (stat ("/", &st))
    return;
  snprintf (sp, sizeof sp, "/sys/dev/block/%u:%u", major (st.st_dev), minor (st.st_dev));
  ssize_t n = readlink (sp, lk, sizeof lk - 1);
  if (n <= 0)
    return;
  lk[n] = 0;
  char *e = strrchr (lk, '/');
  if (!e)
    return;
  *e = 0;
  char *d = strrchr (lk, '/');
  snprintf (disk, sizeof disk, "%s", d ? d + 1 : lk);
  char bp[160];
  snprintf (bp, sizeof bp, "/sys/block/%s", disk);
  DIR *dir = opendir (bp);
  struct dirent *de;
  while (dir && (de = readdir (dir)))
    {
      char pp[400], b[16] = "";
      if (strncmp (de->d_name, disk, strlen (disk)))
	continue;
      snprintf (pp, sizeof pp, "%s/%s/partition", bp, de->d_name);
      int f = open (pp, O_RDONLY | O_CLOEXEC);
      if (f < 0)
	continue;
      if (read (f, b, sizeof b - 1) > 0 && atoi (b) == 1)
	snprintf (dev, sizeof dev, "/dev/%s", de->d_name);
      close (f);
    }
  if (dir)
    closedir (dir);
  if (!dev[0])
    return;
  mkdir ("/run/fat", 0700);
  if (mount (dev, "/run/fat", "vfat", MS_NOSUID | MS_NODEV | MS_NOEXEC, "shortname=mixed"))
    return;
  int found = 0, rollback = 0;
  if (unlink ("/run/fat/RESETPW") == 0)
    found = 1;
  if (unlink ("/run/fat/RESETPW.TXT") == 0)
    found = 1;
  if (unlink ("/run/fat/ROLLBACK") == 0)
    rollback = 1;
  if (unlink ("/run/fat/ROLLBACK.TXT") == 0)
    rollback = 1;
  sync ();
  umount ("/run/fat");
  rmdir ("/run/fat");
  /* ROLLBACK: the RISC OS ROM an update replaced (kept by riscos-rom's
     preinst in /opt/riscos/rollback) goes back in place; the one it
     replaces is kept as rollback/RISCOS.IMG.undone */
  if (rollback && access ("/opt/riscos/rollback/RISCOS.IMG", F_OK) == 0)
    {
      rename ("/opt/riscos/RISCOS.IMG", "/opt/riscos/rollback/RISCOS.IMG.undone");
      rename ("/opt/riscos/rollback/RISCOS.IMG", "/opt/riscos/RISCOS.IMG");
      sync ();
    }
  if (!found)
    return;
  /* root's hash -> "*" (no password: rol-cmd asks for a new one) */
  char buf[65536];
  int f = open ("/etc/shadow", O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return;
  struct stat ss;
  fstat (f, &ss);
  n = read (f, buf, sizeof buf - 1);
  close (f);
  if (n <= 0)
    return;
  buf[n] = 0;
  char *r = !strncmp (buf, "root:", 5) ? buf : strstr (buf, "\nroot:");
  if (!r)
    return;
  if (*r == '\n')
    r++;
  char *h = r + 5, *c = strchr (h, ':');
  if (!c)
    return;
  f = open ("/etc/shadow.rol", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0);
  if (f < 0)
    return;
  int ok = write (f, buf, (size_t) (h - buf)) == h - buf && write (f, "*", 1) == 1
	   && write (f, c, strlen (c)) == (ssize_t) strlen (c);
  if (fchown (f, ss.st_uid, ss.st_gid) || fchmod (f, ss.st_mode & 07777))
    ok = 0;
  fsync (f);
  close (f);
  if (ok)
    rename ("/etc/shadow.rol", "/etc/shadow");
  else
    unlink ("/etc/shadow.rol");
  memset (buf, 0, sizeof buf);
}

int
main (void)
{
  if (getpid () != 1)
    return 1;
  mount ("proc", "/proc", "proc", 0, NULL);
  mount ("sysfs", "/sys", "sysfs", 0, NULL);
  mount ("devtmpfs", "/dev", "devtmpfs", 0, NULL);	/* already, usually */
  mkdir ("/dev/shm", 01777);
  mount ("tmpfs", "/dev/shm", "tmpfs", 0, "mode=1777");
  mount ("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
  mount ("tmpfs", "/run", "tmpfs", 0, "mode=755");
  mount (NULL, "/", NULL, MS_REMOUNT, NULL);
  kernel_log ();
  reset_root_password ();
  if (sethostname ("riscos", 6)) { }
  /* overcommit: the port reserves big address ranges */
  int fd = open ("/proc/sys/vm/overcommit_memory", O_WRONLY);
  if (fd >= 0)
    {
      if (write (fd, "1", 1) < 0) { }
      close (fd);
    }

  setenv ("PATH", "/opt/riscos:/sbin:/bin", 1);
  setenv ("HOME", "/root", 1);
  setenv ("TERM", "linux", 1);
  setenv ("RISC_OS_IXFS_HardDisc4", "IXFS:$.riscos.HardDisc4", 1);
  setenv ("RISC_OS_Alias_IXFSBoot", "/<IXFS$HardDisc4>.!Boot", 1);

  /* the host name: Linux's (rol-netd sets it from /riscos/net/config) */
  char host[64] = "riscos";
  {
    char cb[2048];
    int fd = open ("/riscos/net/config", O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read (fd, cb, sizeof cb - 1) : -1;
    if (fd >= 0)
      close (fd);
    cb[n > 0 ? n : 0] = 0;
    char *h = strstr (cb, "hostname=");
    if (h && (h == cb || h[-1] == '\n') && h[9] && h[9] != '\n')
      snprintf (host, sizeof host, "%.*s", (int) strcspn (h + 9, "\r\n"), h + 9);
  }
  if (sethostname (host, strlen (host))) { }
  setenv ("RISC_OS_Inet_HostName", host, 1);
  if (access ("/dev/vdb", F_OK) == 0)
    job_mode ();
  host_job ();
  read_model ();
  int wdk = strstr (model, "Windows Dev Kit") != NULL;
  /* a real monitor (only its own modes; others scaled): a Pi, the Dev Kit */
  int pi = strstr (model, "Raspberry Pi") != NULL || wdk;
  if (wdk)
    wdk_firmware ();
  network_up ();
  cmd_up ();
  sound_up ();
  /* ROBench's processor tests as a native Linux program, for comparing
     with RISC OS's: create HardDisc4.NativeBench (e.g. *Create
     <IXFS$HardDisc4>.NativeBench) and restart; the results are written to
     HardDisc4.NativeResults before RISC OS starts, and NativeBench removed. */
  if (access ("/riscos/HardDisc4/NativeBench", F_OK) == 0)
    {
      unlink ("/riscos/HardDisc4/NativeBench");
      say ("Running ROBench natively (a few minutes)...\n", "");
      setenv ("ROBENCH_MACHINE", model[0] ? model : "this VM", 1);
      pid_t b = fork ();
      if (b == 0)
	{
	  int null = open ("/dev/null", O_RDWR);
	  dup2 (null, 0);
	  dup2 (null, 1);
	  dup2 (null, 2);
	  execl ("/opt/riscos/robench-native", "robench-native", "-o",
		 "/riscos/HardDisc4/NativeResults,fff", (char *) NULL);
	  _exit (127);
	}
      if (b > 0)
	waitpid (b, NULL, 0);
      sync ();
    }
  /* Native or QEMU: /riscos/native says "on", "off" or "auto" (the
     default): auto runs RISC OS straight on the processor when it can run
     32-bit ARM programs (a Pi 4 or 5; not Apple's processors), which
     /opt/riscos/a32test finds out. */
  int native = 0;
  {
    char nb[16] = "auto";
    int fd = open ("/riscos/native", O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read (fd, nb, sizeof nb - 1) : -1;
    if (fd >= 0)
      close (fd);
    if (n >= 0)
      nb[n] = 0;
    if (!strncmp (nb, "on", 2))
      native = 1;
    else if (strncmp (nb, "off", 3))
      {
	pid_t t = fork ();
	if (t == 0)
	  {
	    execl ("/opt/riscos/a32test", "a32test", (char *) NULL);
	    _exit (127);
	  }
	int ts;
	native = t > 0 && waitpid (t, &ts, 0) == t && WIFEXITED (ts)
		 && WEXITSTATUS (ts) == 0;
      }
    if (native)
      {
	/* the port maps from 0x1000 */
	int mfd = open ("/proc/sys/vm/mmap_min_addr", O_WRONLY | O_CLOEXEC);
	if (mfd >= 0)
	  {
	    if (write (mfd, "4096", 4) < 0) { }
	    close (mfd);
	  }
      }
  }
  say (native ? "Starting RISC OS (native)...\n"
	      : "Starting RISC OS...\n", "");
  /* natively, NativeSupport (the test bed's: what its QEMU does for code
     loaded from disc) before HardDisc4's !Boot */
  if (native && access ("/opt/riscos/NativeBoot,feb", F_OK) == 0)
    setenv ("RISC_OS_Alias_IXFSBoot", "Obey IXFS:$.opt.riscos.NativeBoot", 1);
  /* NativeSupport's fault reports in log.txt (as start-riscos.sh --debug
     does): create HardDisc4.NativeDebug (*Create <IXFS$HardDisc4>.NativeDebug)
     and restart; delete it to stop them. */
  if (native && access ("/riscos/HardDisc4/NativeDebug", F_OK) == 0)
    {
      setenv ("RISC_OS_NativeSupport_Debug", "faults", 1);
      say ("NativeSupport debug reports on (HardDisc4.NativeDebug)\n", "");
    }
  /* Logical address space: the port keeps RISC OS below 2GB unless it's
     given --highmem, and every dynamic area reserves its maximum size
     there (up to the 1GB the port says it has), so a few big ones (Iris's
     WebKit) fill it. Natively, on a 64-bit kernel, a 32-bit program has
     4GB: use it, unless /riscos/highmem says "off". */
  int highmem = native;
  {
    int fd = open ("/riscos/highmem", O_RDONLY | O_CLOEXEC);
    if (fd >= 0)
      {
	char hb[8] = "";
	if (read (fd, hb, sizeof hb - 1) > 0 && !strncmp (hb, "off", 3))
	  highmem = 0;
	close (fd);
      }
  }
  if (protect_on ())
    protect_setup ();
  time_t t0 = time (NULL);
  int pfd[2];
  if (pipe2 (pfd, O_CLOEXEC))
    pfd[0] = pfd[1] = -1;
  pid_t pid = fork ();
  if (pid == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      dup2 (null, 0);
      if (pfd[1] >= 0)
	{
	  dup2 (pfd[1], 1);
	  dup2 (pfd[1], 2);
	}
      setsid ();
      if (chdir ("/riscos")) { }
      /* -n on a Pi: a real monitor, so only its own modes (others scaled) */
      char *argv[10], **a = argv;
      *a++ = "rol-fb";
      if (pi)
	*a++ = "-n";
      *a++ = "--";
      if (!native)
	*a++ = "/opt/riscos/qemu-arm-riscos";
      *a++ = "/opt/riscos/RISCOS.IMG";
      if (highmem)
	*a++ = "--highmem";
      *a++ = "--nvram";
      *a++ = "/riscos/HardDisc4/!Boot/Loader/CMOS";
      *a = NULL;
      execv ("/opt/riscos/rol-fb", argv);
      _exit (127);
    }
  close (pfd[1]);
  /* RISC OS's (and rol-fb's) messages: to /riscos/log.txt and the serial port */
  int log = open ("/riscos/log.txt", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
  int ser = open ("/dev/ttyAMA0", O_WRONLY | O_NOCTTY | O_CLOEXEC);
  int status = 0;
  for (;;)
    {
      struct pollfd p = { pfd[0], POLLIN, 0 };
      if (pfd[0] >= 0 && poll (&p, 1, 1000) > 0)
	{
	  char buf[4096];
	  ssize_t n = read (pfd[0], buf, sizeof buf);
	  if (n > 0)
	    {
	      if (log >= 0 && write (log, buf, n) < 0) { }
	      if (ser >= 0 && write (ser, buf, n) < 0) { }
	    }
	  else if (n == 0)
	    {
	      close (pfd[0]);
	      pfd[0] = -1;
	    }
	}
      else if (pfd[0] < 0)
	sleep (1);
      int ws;
      pid_t w;
      int done = 0;
      while ((w = waitpid (-1, &ws, WNOHANG)) > 0)
	if (w == pid)
	  {
	    status = WIFEXITED (ws) ? WEXITSTATUS (ws) : 128 + WTERMSIG (ws);
	    done = 1;
	  }
      if (done || (w < 0 && errno == ECHILD))
	break;
    }
  if (log >= 0)
    close (log);
  sync ();
  if (time (NULL) - t0 < 20)
    {
      char s[16];
      snprintf (s, sizeof s, "%d", status);
      say ("\nRISC OS stopped (status %s). The last of /riscos/log.txt:\n\n", s);
      show_log ();
      say ("\nPowering off in 60 seconds.\n", "");
      sleep (60);
    }
  else
    say ("RISC OS has ended: powering off.\n", "");
  sync ();
  reboot (RB_POWER_OFF);
  return 0;
}
