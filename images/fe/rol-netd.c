/* rol-netd: the network for the RISC OS for Linux images, controlled from
 * RISC OS.
 *
 * RISC OS's Configure plug-in (LinuxNet) writes the settings to
 * /riscos/net/config, and reads what the network is doing from
 * /riscos/net/status, which this rewrites every two seconds:
 *
 *   config (key=value lines; missing keys take the defaults shown)
 *     interface=auto      auto, wired, wifi or off (auto: wired if it has a
 *                         link, else Wi-Fi if a network is set)
 *     mode=dhcp           dhcp or manual
 *     address= netmask= gateway= dns=      for manual (dns: up to three,
 *                         separated by commas or spaces)
 *     hostname=riscos
 *     wifi_ssid= wifi_psk= wifi_country=GB
 *     wifi_powersave=off  on: let the Wi-Fi save power between packets.
 *                         Off by default: some Wi-Fi (the Pinebook Pro's
 *                         Broadcom) drops its connection with it on.
 *     wifi_pmf=on         off: no protected management frames and plain
 *                         WPA-PSK only (ieee80211w=0, no WPA-PSK-SHA256).
 *                         To try where a network is joined and then
 *                         dropped, over and over: some Wi-Fi chips'
 *                         firmware (Broadcom's) and some access points
 *                         don't agree on them.
 *
 *   status: interface, type, mac, link, state, address, netmask, gateway,
 *     dns, lease, rx, tx, ssid, signal, freq, interfaces, message, time
 *
 *   request: "scan" asks for a Wi-Fi scan; the networks found go to
 *     /riscos/net/scan, one per line: signal (dBm) TAB security TAB name
 *
 * Wi-Fi is wpa_supplicant's (/opt/riscos/wpa_supplicant, nl80211), run by
 * this and spoken to through its control socket. Run as "rol-netd" by init;
 * "rol-netd -d DIR -r FILE" uses DIR instead of /riscos/net and FILE
 * instead of /etc/resolv.conf (for testing).
 *
 * What happens (links up and down, addresses, wpa_supplicant) goes to
 * /riscos/net/log.txt, with wpa_supplicant's own messages in
 * /run/wpa_supplicant.log; init copies both to the FAT partition's LOGS.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

static const char *dir = "/riscos/net";
static const char *wpa_bin = "/opt/riscos/wpa_supplicant";
static const char *resolv = "/etc/resolv.conf";

static double
now (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

static void
path (char *buf, size_t n, const char *leaf)
{
  snprintf (buf, n, "%s/%s", dir, leaf);
}

/* write a whole file by way of a temporary one, so readers never see half */
static void
put_file (const char *leaf, const char *text)
{
  char p[300], t[310];
  path (p, sizeof p, leaf);
  snprintf (t, sizeof t, "%s.new", p);
  unlink (t);
  int fd = open (t, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  size_t len = strlen (text);
  if (write (fd, text, len) != (ssize_t) len) { }
  close (fd);
  rename (t, p);
}

static int
read_small (const char *p, char *buf, size_t n)
{
  int fd = open (p, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t r = read (fd, buf, n - 1);
  close (fd);
  buf[r > 0 ? r : 0] = 0;
  return r > 0 ? (int) r : 0;
}

static long long
read_num (const char *fmt, const char *ifn)
{
  char p[200], b[32];
  snprintf (p, sizeof p, fmt, ifn);
  return read_small (p, b, sizeof b) > 0 ? atoll (b) : -1;
}

/* --- the settings ------------------------------------------------------ */

struct config
{
  char interface[16], mode[16], address[32], netmask[32], gateway[32],
       dns[100], hostname[64], ssid[64], psk[100], country[8], powersave[8], pmf[8];
};

static void
config_defaults (struct config *c)
{
  memset (c, 0, sizeof *c);
  strcpy (c->interface, "auto");
  strcpy (c->mode, "dhcp");
  strcpy (c->hostname, "riscos");
  strcpy (c->country, "GB");
}

static void
config_read (struct config *c)
{
  char p[300], buf[2048];
  config_defaults (c);
  path (p, sizeof p, "config");
  if (read_small (p, buf, sizeof buf) < 0)
    return;
  for (char *l = strtok (buf, "\r\n"); l; l = strtok (NULL, "\r\n"))
    {
      char *v = strchr (l, '=');
      if (!v)
        continue;
      *v++ = 0;
      struct { const char *k; char *f; size_t n; } keys[] = {
        { "interface", c->interface, sizeof c->interface },
        { "mode", c->mode, sizeof c->mode },
        { "address", c->address, sizeof c->address },
        { "netmask", c->netmask, sizeof c->netmask },
        { "gateway", c->gateway, sizeof c->gateway },
        { "dns", c->dns, sizeof c->dns },
        { "hostname", c->hostname, sizeof c->hostname },
        { "wifi_ssid", c->ssid, sizeof c->ssid },
        { "wifi_psk", c->psk, sizeof c->psk },
        { "wifi_country", c->country, sizeof c->country },
        { "wifi_powersave", c->powersave, sizeof c->powersave },
        { "wifi_pmf", c->pmf, sizeof c->pmf },
      };
      for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (!strcmp (l, keys[i].k))
          snprintf (keys[i].f, keys[i].n, "%s", v);
    }
  for (char *s = c->interface; *s; s++)
    *s = (char) tolower ((unsigned char) *s);
  for (char *s = c->mode; *s; s++)
    *s = (char) tolower ((unsigned char) *s);
  if (!c->hostname[0])
    strcpy (c->hostname, "riscos");
}

/* --- interfaces -------------------------------------------------------- */

static int sock = -1;           /* for the ioctls */

static int
is_wireless (const char *ifn)
{
  char p[200];
  struct stat st;
  snprintf (p, sizeof p, "/sys/class/net/%s/wireless", ifn);
  if (stat (p, &st) == 0)
    return 1;
  snprintf (p, sizeof p, "/sys/class/net/%s/phy80211", ifn);
  return stat (p, &st) == 0;
}

/* on a USB socket (an adapter someone plugged in), not built into the
   machine */
static int
is_usb (const char *ifn)
{
  char p[200], l[300];
  snprintf (p, sizeof p, "/sys/class/net/%s/device/subsystem", ifn);
  ssize_t n = readlink (p, l, sizeof l - 1);
  if (n <= 0)
    return 0;
  l[n] = 0;
  const char *b = strrchr (l, '/');
  return !strcmp (b ? b + 1 : l, "usb");
}

static int if_up (const char *ifn, int up);

/* the Ethernet-type interfaces (not lo, sit0 and the like): "eth0 wlan0".
   WIRED and WIFI are the ones to use when there are several:
   - wired: the first (in name order) with a link; none has: the first.
     With PROBE each is brought up first, since an interface that is down
     shows no link;
   - Wi-Fi: a USB adapter before the machine's own (whoever plugs one in
     wants it used), then name order. */
static void
list_interfaces (char *wired, char *wifi, char *all, size_t n, int probe)
{
  wired[0] = wifi[0] = all[0] = 0;
  DIR *d = opendir ("/sys/class/net");
  struct dirent *e;
  char names[16][IFNAMSIZ];
  int count = 0;
  while (d && (e = readdir (d)) && count < 16)
    {
      if (e->d_name[0] == '.')
        continue;
      if (read_num ("/sys/class/net/%s/type", e->d_name) != 1)
        continue;
      snprintf (names[count++], IFNAMSIZ, "%.15s", e->d_name);
    }
  if (d)
    closedir (d);
  /* in name order, so eth0 comes before eth1 */
  qsort (names, count, IFNAMSIZ, (int (*) (const void *, const void *)) strcmp);
  int wired_link = 0, wifi_usb = 0, nwired = 0;
  for (int i = 0; i < count; i++)
    if (!is_wireless (names[i]))
      nwired++;
  for (int i = 0; i < count; i++)
    {
      int w = is_wireless (names[i]);
      if (w)
        {
          int usb = is_usb (names[i]);
          if (!wifi[0] || (usb && !wifi_usb))
            {
              snprintf (wifi, IFNAMSIZ, "%.15s", names[i]);
              wifi_usb = usb;
            }
        }
      else
        {
          if (probe && nwired > 1)
            if_up (names[i], 1);
          int link = read_num ("/sys/class/net/%s/carrier", names[i]) == 1;
          if (!wired[0] || (link && !wired_link))
            {
              snprintf (wired, IFNAMSIZ, "%.15s", names[i]);
              wired_link = link;
            }
        }
      size_t l = strlen (all);
      snprintf (all + l, n - l, "%s%s", l ? " " : "", names[i]);
    }
}

static int
if_up (const char *ifn, int up)
{
  struct ifreq r = { 0 };
  snprintf (r.ifr_name, IFNAMSIZ, "%s", ifn);
  if (ioctl (sock, SIOCGIFFLAGS, &r))
    return -1;
  if (up)
    r.ifr_flags |= IFF_UP;
  else
    r.ifr_flags &= ~IFF_UP;
  return ioctl (sock, SIOCSIFFLAGS, &r);
}

static int
if_addr (const char *ifn, unsigned long req, uint32_t a)
{
  struct ifreq r = { 0 };
  snprintf (r.ifr_name, IFNAMSIZ, "%s", ifn);
  struct sockaddr_in *sin = (struct sockaddr_in *) &r.ifr_addr;
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = a;
  return ioctl (sock, req, &r);
}

static int
default_route (const char *ifn, uint32_t gw, int add)
{
  struct rtentry rt = { 0 };
  struct sockaddr_in *a;
  a = (struct sockaddr_in *) &rt.rt_dst; a->sin_family = AF_INET;
  a = (struct sockaddr_in *) &rt.rt_genmask; a->sin_family = AF_INET;
  a = (struct sockaddr_in *) &rt.rt_gateway; a->sin_family = AF_INET;
  a->sin_addr.s_addr = gw;
  rt.rt_flags = RTF_UP | RTF_GATEWAY;
  rt.rt_dev = (char *) ifn;
  return ioctl (sock, add ? SIOCADDRT : SIOCDELRT, &rt);
}

static void
get_mac (const char *ifn, uint8_t mac[6])
{
  struct ifreq r = { 0 };
  memset (mac, 0, 6);
  snprintf (r.ifr_name, IFNAMSIZ, "%s", ifn);
  if (!ioctl (sock, SIOCGIFHWADDR, &r))
    memcpy (mac, r.ifr_hwaddr.sa_data, 6);
}

/* --- the address in use ------------------------------------------------ */

static struct
{
  char ifn[IFNAMSIZ];           /* the interface set up, or "" */
  uint32_t ip, mask, gw, dns[3];
  int ndns;
  double expires;               /* DHCP: when the lease ends (0: manual) */
} cur;

static void
write_resolv (void)
{
  char text[256] = "", a[INET_ADDRSTRLEN];
  for (int i = 0; i < cur.ndns; i++)
    {
      inet_ntop (AF_INET, &cur.dns[i], a, sizeof a);
      size_t l = strlen (text);
      snprintf (text + l, sizeof text - l, "nameserver %s\n", a);
    }
  int f = open (resolv, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (f >= 0)
    {
      if (write (f, text, strlen (text)) < 0) { }
      close (f);
    }
}

static void
unconfigure (void)
{
  if (!cur.ifn[0])
    return;
  if (cur.gw)
    default_route (cur.ifn, cur.gw, 0);
  if (cur.ip)
    if_addr (cur.ifn, SIOCSIFADDR, 0);
  memset (&cur, 0, sizeof cur);
}

static void
configure (const char *ifn, uint32_t ip, uint32_t mask, uint32_t gw,
           const uint32_t *dns, int ndns, double expires)
{
  if (strcmp (cur.ifn, ifn) || cur.ip != ip || cur.mask != mask || cur.gw != gw)
    {
      unconfigure ();
      if_addr (ifn, SIOCSIFADDR, ip);
      if_addr (ifn, SIOCSIFNETMASK, mask);
      if (gw)
        default_route (ifn, gw, 1);
    }
  snprintf (cur.ifn, sizeof cur.ifn, "%s", ifn);
  cur.ip = ip;
  cur.mask = mask;
  cur.gw = gw;
  cur.ndns = ndns;
  memcpy (cur.dns, dns, sizeof cur.dns);
  cur.expires = expires;
  write_resolv ();
}

/* --- DHCP -------------------------------------------------------------- */

struct dhcp { uint8_t op, htype, hlen, hops; uint32_t xid; uint16_t secs, flags;
  uint32_t ciaddr, yiaddr, siaddr, giaddr; uint8_t chaddr[16]; uint8_t sname[64];
  uint8_t file[128]; uint32_t magic; uint8_t opt[312]; } __attribute__ ((packed));

enum { D_IDLE, D_DISCOVER, D_REQUEST, D_BOUND, D_RENEW };

static struct
{
  int state, fd, tries;
  char ifn[IFNAMSIZ];
  uint8_t mac[6];
  uint32_t xid, offer, server;
  double next;                  /* when to send (again) */
  double renew;                 /* bound: when to renew */
} dh = { .fd = -1 };

static void
dhcp_stop (void)
{
  if (dh.fd >= 0)
    close (dh.fd);
  dh.fd = -1;
  dh.state = D_IDLE;
  dh.ifn[0] = 0;
}

static int
dhcp_open (const char *ifn)
{
  int u = socket (AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (u < 0)
    return -1;
  int one = 1;
  setsockopt (u, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
  setsockopt (u, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  setsockopt (u, SOL_SOCKET, SO_BINDTODEVICE, ifn, strlen (ifn) + 1);
  struct sockaddr_in me = { .sin_family = AF_INET, .sin_port = htons (68) };
  if (bind (u, (struct sockaddr *) &me, sizeof me))
    {
      close (u);
      return -1;
    }
  return u;
}

static void
dhcp_send (int type, uint32_t ciaddr, uint32_t req_ip, uint32_t server,
           const char *hostname)
{
  struct dhcp d = { 0 };
  d.op = 1; d.htype = 1; d.hlen = 6; d.xid = dh.xid;
  d.flags = ciaddr ? 0 : htons (0x8000);
  d.ciaddr = ciaddr;
  memcpy (d.chaddr, dh.mac, 6);
  d.magic = htonl (0x63825363);
  uint8_t *o = d.opt;
  *o++ = 53; *o++ = 1; *o++ = (uint8_t) type;
  if (req_ip) { *o++ = 50; *o++ = 4; memcpy (o, &req_ip, 4); o += 4; }
  if (server) { *o++ = 54; *o++ = 4; memcpy (o, &server, 4); o += 4; }
  size_t hl = strlen (hostname);
  if (hl > 60)
    hl = 60;
  *o++ = 12; *o++ = (uint8_t) hl; memcpy (o, hostname, hl); o += hl;
  *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 51;
  *o++ = 255;
  struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons (67),
                            .sin_addr.s_addr = INADDR_BROADCAST };
  sendto (dh.fd, &d, sizeof d, 0, (struct sockaddr *) &to, sizeof to);
}

static char message[200];

/* The network's log: /riscos/net/log.txt (new at each start, up to 256 KB) */
static void path (char *p, size_t n, const char *name);
static void
netlog (const char *fmt, ...)
{
  static FILE *f;
  static long written;
  if (!f)
    {
      char p[300];
      path (p, sizeof p, "log.txt");
      f = fopen (p, "w");
      if (!f)
        return;
    }
  if (written > 256 * 1024)
    return;
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  va_list ap;
  va_start (ap, fmt);
  written += fprintf (f, "[%6ld.%01ld] ", (long) t.tv_sec, t.tv_nsec / 100000000);
  written += vfprintf (f, fmt, ap);
  written += fprintf (f, "\n");
  va_end (ap);
  fflush (f);
}

static void
say (const char *fmt, ...)
{
  char m[sizeof message];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (m, sizeof m, fmt, ap);
  va_end (ap);
  if (strcmp (m, message))
    netlog ("%s", m);
  strcpy (message, m);
}

static void
dhcp_start (const char *ifn)
{
  dhcp_stop ();
  dh.fd = dhcp_open (ifn);
  if (dh.fd < 0)
    {
      say ("Can't use DHCP on %s: %s", ifn, strerror (errno));
      return;
    }
  snprintf (dh.ifn, sizeof dh.ifn, "%s", ifn);
  get_mac (ifn, dh.mac);
  dh.xid = (uint32_t) time (NULL) ^ (uint32_t) getpid () ^
           (uint32_t) (dh.mac[5] << 24 | dh.mac[4] << 16 | dh.mac[3] << 8 | dh.mac[2]);
  dh.state = D_DISCOVER;
  dh.tries = 0;
  dh.next = 0;
  say ("Asking for an address (DHCP) on %s", ifn);
}

/* time passing: send what's due */
static void
dhcp_tick (const char *hostname)
{
  double t = now ();
  if (dh.state == D_IDLE || dh.fd < 0)
    return;
  if (dh.state == D_BOUND)
    {
      if (t >= dh.renew)
        {
          dh.state = D_RENEW;
          dh.tries = 0;
          dh.next = 0;
          dh.xid++;
        }
      else
        return;
    }
  if (dh.state == D_RENEW && cur.expires && t >= cur.expires)
    {
      unconfigure ();
      say ("The DHCP lease on %s ran out", dh.ifn);
      dh.state = D_DISCOVER;
      dh.tries = 0;
      dh.next = 0;
    }
  if (t < dh.next)
    return;
  /* 2, 4, 8, 16, then every 30 seconds */
  double wait = dh.tries < 4 ? 2 << dh.tries : 30;
  dh.tries++;
  dh.next = t + wait;
  if (dh.state == D_DISCOVER)
    {
      if (dh.tries == 5)
        say ("No answer from a DHCP server on %s (still asking)", dh.ifn);
      dhcp_send (1, 0, 0, 0, hostname);
    }
  else if (dh.state == D_REQUEST)
    {
      if (dh.tries > 3)
        {
          dh.state = D_DISCOVER;
          dh.tries = 0;
          dh.next = 0;
          return;
        }
      dhcp_send (3, 0, dh.offer, dh.server, hostname);
    }
  else if (dh.state == D_RENEW)
    dhcp_send (3, cur.ip, 0, 0, hostname);
}

static void
dhcp_input (const char *hostname)
{
  struct dhcp d;
  ssize_t n = recv (dh.fd, &d, sizeof d, MSG_DONTWAIT);
  if (n < 240 || d.op != 2 || d.xid != dh.xid || d.magic != htonl (0x63825363)
      || memcmp (d.chaddr, dh.mac, 6))
    return;
  int type = 0;
  uint32_t mask = htonl (0xFFFFFF00), gw = 0, server = 0, dns[3] = { 0 };
  int ndns = 0;
  uint32_t lease = 0;
  for (uint8_t *o = d.opt; o + 1 < (uint8_t *) &d + n && *o != 255;)
    {
      if (*o == 0) { o++; continue; }
      uint8_t code = o[0], len = o[1], *v = o + 2;
      if (v + len > (uint8_t *) &d + n)
        break;
      if (code == 53 && len >= 1) type = v[0];
      if (code == 1 && len >= 4) memcpy (&mask, v, 4);
      if (code == 3 && len >= 4) memcpy (&gw, v, 4);
      if (code == 6)
        for (int i = 0; i + 4 <= len && ndns < 3; i += 4)
          memcpy (&dns[ndns++], v + i, 4);
      if (code == 54 && len >= 4) memcpy (&server, v, 4);
      if (code == 51 && len >= 4) lease = (uint32_t) v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
      o += 2 + len;
    }
  if (dh.state == D_DISCOVER && type == 2)
    {
      dh.offer = d.yiaddr;
      dh.server = server;
      dh.state = D_REQUEST;
      dh.tries = 0;
      dh.next = 0;
      dhcp_tick (hostname);
    }
  else if ((dh.state == D_REQUEST || dh.state == D_RENEW) && type == 5)
    {
      if (!lease || lease > 0x7FFFFFFF)
        lease = 0x7FFFFFFF;
      double t = now ();
      configure (dh.ifn, d.yiaddr, mask, gw, dns, ndns, t + lease);
      dh.state = D_BOUND;
      dh.renew = t + lease / 2.0;
      char a[INET_ADDRSTRLEN];
      inet_ntop (AF_INET, &d.yiaddr, a, sizeof a);
      say ("%s has address %s (DHCP)", dh.ifn, a);
    }
  else if ((dh.state == D_REQUEST || dh.state == D_RENEW) && type == 6)
    {
      /* NAK: start again */
      unconfigure ();
      dh.state = D_DISCOVER;
      dh.tries = 0;
      dh.next = 0;
      dh.xid++;
    }
}

/* --- Wi-Fi: wpa_supplicant --------------------------------------------- */

static struct
{
  pid_t pid;
  char ifn[IFNAMSIZ];
  int ctrl;                     /* our end of its control socket */
  char local[108];
  double started;
  char conf_text[1024];         /* what its configuration says */
  int scanning;
  double scan_asked;
  int ps_set;                   /* power saving set since the link came up */
} wpa = { .ctrl = -1 };

static void
wpa_close_ctrl (void)
{
  if (wpa.ctrl >= 0)
    {
      close (wpa.ctrl);
      unlink (wpa.local);
    }
  wpa.ctrl = -1;
}

static void
wpa_stop (void)
{
  wpa_close_ctrl ();
  if (wpa.pid > 0)
    {
      kill (wpa.pid, SIGTERM);
      for (int i = 0; i < 30 && waitpid (wpa.pid, NULL, WNOHANG) == 0; i++)
        usleep (100000);
      kill (wpa.pid, SIGKILL);
      waitpid (wpa.pid, NULL, WNOHANG);
    }
  wpa.pid = 0;
  wpa.ifn[0] = 0;
}

static void
hex (char *out, const char *s)
{
  for (; *s; s++, out += 2)
    sprintf (out, "%02x", (unsigned char) *s);
  *out = 0;
}

static void
wpa_conf_text (const struct config *c, char *t, size_t n)
{
  char country[8] = "";
  for (int i = 0; c->country[i] && i < 2; i++)
    country[i] = (char) toupper ((unsigned char) c->country[i]);
  int l = snprintf (t, n, "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\n%s%s%s",
                    country[0] ? "country=" : "", country, country[0] ? "\n" : "");
  if (!c->ssid[0])
    return;
  char ssid[140];
  hex (ssid, c->ssid);
  size_t pl = strlen (c->psk);
  int hexpsk = pl == 64 && strspn (c->psk, "0123456789abcdefABCDEF") == 64;
  const char *km = !strcasecmp (c->pmf, "off")
    ? "key_mgmt=WPA-PSK\n ieee80211w=0" : "key_mgmt=WPA-PSK WPA-PSK-SHA256\n ieee80211w=1";
  if (!pl)
    snprintf (t + l, n - l, "network={\n ssid=%s\n scan_ssid=1\n key_mgmt=NONE\n}\n", ssid);
  else if (hexpsk)
    snprintf (t + l, n - l, "network={\n ssid=%s\n scan_ssid=1\n %s\n psk=%s\n}\n", ssid, km, c->psk);
  else
    snprintf (t + l, n - l, "network={\n ssid=%s\n scan_ssid=1\n %s\n psk=\"%s\"\n}\n", ssid, km, c->psk);
}

static int
wpa_open_ctrl (void)
{
  if (wpa.ctrl >= 0)
    return 0;
  int s = socket (AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (s < 0)
    return -1;
  struct sockaddr_un me = { .sun_family = AF_UNIX }, it = { .sun_family = AF_UNIX };
  snprintf (wpa.local, sizeof wpa.local, "/run/rol-netd-%d", (int) getpid ());
  unlink (wpa.local);
  snprintf (me.sun_path, sizeof me.sun_path, "%s", wpa.local);
  snprintf (it.sun_path, sizeof it.sun_path, "/run/wpa_supplicant/%s", wpa.ifn);
  if (bind (s, (struct sockaddr *) &me, sizeof me)
      || connect (s, (struct sockaddr *) &it, sizeof it))
    {
      close (s);
      unlink (wpa.local);
      return -1;
    }
  wpa.ctrl = s;
  return 0;
}

/* a command and its reply (unsolicited "<3>..." event messages skipped) */
static int
wpa_cmd (const char *cmd, char *reply, size_t n)
{
  reply[0] = 0;
  if (wpa_open_ctrl ())
    return -1;
  /* drop anything left over */
  char junk[4096];
  while (recv (wpa.ctrl, junk, sizeof junk, MSG_DONTWAIT) > 0)
    ;
  if (send (wpa.ctrl, cmd, strlen (cmd), 0) < 0)
    {
      wpa_close_ctrl ();
      return -1;
    }
  double end = now () + 2;
  while (now () < end)
    {
      struct pollfd p = { wpa.ctrl, POLLIN, 0 };
      if (poll (&p, 1, 200) <= 0)
        continue;
      ssize_t r = recv (wpa.ctrl, reply, n - 1, 0);
      if (r < 0)
        break;
      reply[r] = 0;
      if (reply[0] == '<')
        continue;
      return 0;
    }
  reply[0] = 0;
  return -1;
}

static void
wpa_start (const char *ifn, const struct config *c)
{
  wpa_stop ();
  mkdir ("/run/wpa_supplicant", 0700);
  wpa_conf_text (c, wpa.conf_text, sizeof wpa.conf_text);
  int fd = open ("/run/wpa_supplicant.conf", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd >= 0)
    {
      if (write (fd, wpa.conf_text, strlen (wpa.conf_text)) < 0) { }
      close (fd);
    }
  snprintf (wpa.ifn, sizeof wpa.ifn, "%s", ifn);
  if_up (ifn, 1);
  pid_t p = fork ();
  if (p == 0)
    {
      int null = open ("/dev/null", O_RDWR);
      int log = open ("/run/wpa_supplicant.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      dup2 (null, 0);
      dup2 (log >= 0 ? log : null, 1);
      dup2 (log >= 0 ? log : null, 2);
      execl (wpa_bin, "wpa_supplicant", "-t", "-Dnl80211", "-i", ifn,
             "-c", "/run/wpa_supplicant.conf", (char *) NULL);
      _exit (127);
    }
  wpa.pid = p > 0 ? p : 0;
  wpa.started = now ();
  wpa.ps_set = 0;
  netlog ("wpa_supplicant started on %s%s%s", ifn, c->ssid[0] ? " for " : "",
          c->ssid);
  if (p < 0)
    say ("Can't start wpa_supplicant: %s", strerror (errno));
}

/* settings changed: tell it (or start it if it isn't running) */
static void
wpa_update (const char *ifn, const struct config *c)
{
  char t[1024];
  wpa_conf_text (c, t, sizeof t);
  if (wpa.pid > 0 && !strcmp (wpa.ifn, ifn) && !strcmp (t, wpa.conf_text))
    return;
  if (wpa.pid > 0 && !strcmp (wpa.ifn, ifn))
    {
      int fd = open ("/run/wpa_supplicant.conf", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
      if (fd >= 0)
        {
          if (write (fd, t, strlen (t)) < 0) { }
          close (fd);
        }
      char r[64];
      strcpy (wpa.conf_text, t);
      if (!wpa_cmd ("RECONFIGURE", r, sizeof r) && !strncmp (r, "OK", 2))
        return;
    }
  wpa_start (ifn, c);
}

static int
kv (const char *text, const char *key, char *out, size_t n)
{
  size_t kl = strlen (key);
  for (const char *l = text; l && *l; l = strchr (l, '\n') ? strchr (l, '\n') + 1 : NULL)
    if (!strncmp (l, key, kl) && l[kl] == '=')
      {
        const char *v = l + kl + 1;
        size_t len = strcspn (v, "\n");
        if (len >= n)
          len = n - 1;
        memcpy (out, v, len);
        out[len] = 0;
        return 0;
      }
  out[0] = 0;
  return -1;
}

struct net { int signal; char flags[160], ssid[100]; };

static int
net_cmp (const void *a, const void *b)
{
  return ((const struct net *) b)->signal - ((const struct net *) a)->signal;
}

static void
wpa_scan_results (void)
{
  static char r[16384];
  if (wpa_cmd ("SCAN_RESULTS", r, sizeof r))
    return;
  static struct net nets[64];
  int count = 0;
  /* bssid / frequency / signal level / flags / ssid */
  char *save = NULL;
  for (char *l = strtok_r (r, "\n", &save); l; l = strtok_r (NULL, "\n", &save))
    {
      char *f[5] = { 0 };
      int i = 0;
      for (char *p = l; i < 5; i++)
        {
          f[i] = p;
          char *tab = i < 4 ? strchr (p, '\t') : NULL;
          if (!tab)
            break;
          *tab = 0;
          p = tab + 1;
        }
      if (i < 4 || !f[4] || !f[4][0] || !strncmp (l, "bssid", 5))
        continue;
      int sig = atoi (f[2]);
      int j;
      for (j = 0; j < count; j++)
        if (!strcmp (nets[j].ssid, f[4]))
          break;
      if (j < count)
        {
          if (sig > nets[j].signal)
            nets[j].signal = sig;
          continue;
        }
      if (count == 64)
        continue;
      nets[count].signal = sig;
      snprintf (nets[count].ssid, sizeof nets[count].ssid, "%s", f[4]);
      const char *sec = strstr (f[3], "SAE") && !strstr (f[3], "PSK") ? "WPA3"
                      : strstr (f[3], "EAP") ? "Enterprise"
                      : strstr (f[3], "WPA2") || strstr (f[3], "RSN") ? "WPA2"
                      : strstr (f[3], "WPA") ? "WPA"
                      : strstr (f[3], "WEP") ? "WEP" : "Open";
      snprintf (nets[count].flags, sizeof nets[count].flags, "%s", sec);
      count++;
    }
  qsort (nets, count, sizeof nets[0], net_cmp);
  static char out[8192];
  int l = snprintf (out, sizeof out, "# %d networks\n", count);
  for (int i = 0; i < count && l < (int) sizeof out - 200; i++)
    l += snprintf (out + l, sizeof out - l, "%d\t%s\t%s\n", nets[i].signal,
                   nets[i].flags, nets[i].ssid);
  put_file ("scan", out);
}

/* --- the main loop ----------------------------------------------------- */

static struct config conf;
static time_t conf_mtime = -1;
static char want_ifn[IFNAMSIZ]; /* the interface we're using */
static char state[32] = "starting";

static void
status_write (const char *all, const char *type)
{
  char s[2048], a[INET_ADDRSTRLEN];
  int l = 0;
#define ADD(...) l += snprintf (s + l, sizeof s - l, __VA_ARGS__)
  ADD ("interface=%s\ntype=%s\n", want_ifn, type);
  uint8_t mac[6] = { 0 };
  if (want_ifn[0])
    get_mac (want_ifn, mac);
  ADD ("mac=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  int carrier = want_ifn[0] ? (int) read_num ("/sys/class/net/%s/carrier", want_ifn) : 0;
  ADD ("link=%s\nstate=%s\nmode=%s\n", carrier == 1 ? "up" : "down", state, conf.mode);
  inet_ntop (AF_INET, &cur.ip, a, sizeof a);
  ADD ("address=%s\n", cur.ip ? a : "");
  inet_ntop (AF_INET, &cur.mask, a, sizeof a);
  ADD ("netmask=%s\n", cur.ip ? a : "");
  inet_ntop (AF_INET, &cur.gw, a, sizeof a);
  ADD ("gateway=%s\ndns=", cur.gw ? a : "");
  for (int i = 0; i < cur.ndns; i++)
    {
      inet_ntop (AF_INET, &cur.dns[i], a, sizeof a);
      ADD ("%s%s", i ? " " : "", a);
    }
  ADD ("\nlease=%ld\n", cur.expires ? (long) (cur.expires - now ()) : 0L);
  ADD ("rx=%lld\ntx=%lld\n",
       want_ifn[0] ? read_num ("/sys/class/net/%s/statistics/rx_bytes", want_ifn) : 0,
       want_ifn[0] ? read_num ("/sys/class/net/%s/statistics/tx_bytes", want_ifn) : 0);
  char ssid[100] = "", sig[16] = "", freq[16] = "";
  if (wpa.pid > 0 && !strcmp (wpa.ifn, want_ifn))
    {
      static char r[2048];
      if (!wpa_cmd ("STATUS", r, sizeof r))
        {
          char st[32];
          kv (r, "wpa_state", st, sizeof st);
          if (!strcmp (st, "COMPLETED"))
            {
              kv (r, "ssid", ssid, sizeof ssid);
              kv (r, "freq", freq, sizeof freq);
              if (!wpa_cmd ("SIGNAL_POLL", r, sizeof r))
                kv (r, "RSSI", sig, sizeof sig);
            }
        }
    }
  ADD ("ssid=%s\nsignal=%s\nfreq=%s\n", ssid, sig, freq);
  ADD ("interfaces=%s\nhostname=%s\nmessage=%s\ntime=%ld\n", all, conf.hostname,
       message, (long) time (NULL));
#undef ADD
  put_file ("status", s);
}

static int
parse_ip (const char *s, uint32_t *a)
{
  struct in_addr in;
  if (!s[0] || inet_pton (AF_INET, s, &in) != 1)
    return -1;
  *a = in.s_addr;
  return 0;
}

static void
apply_manual (const char *ifn)
{
  uint32_t ip, mask, gw = 0, dns[3] = { 0 };
  int ndns = 0;
  if (parse_ip (conf.address, &ip))
    {
      say ("Manual: the address \"%s\" isn't one", conf.address);
      unconfigure ();
      return;
    }
  if (parse_ip (conf.netmask, &mask))
    mask = htonl (0xFFFFFF00);
  if (conf.gateway[0] && parse_ip (conf.gateway, &gw))
    gw = 0;
  char d[100];
  snprintf (d, sizeof d, "%s", conf.dns);
  for (char *t = strtok (d, ", "); t && ndns < 3; t = strtok (NULL, ", "))
    if (!parse_ip (t, &dns[ndns]))
      ndns++;
  configure (ifn, ip, mask, gw, dns, ndns, 0);
  say ("%s has address %s (manual)", ifn, conf.address);
}

static void
wifi_stop (void)
{
  if (wpa.pid > 0)
    wpa_stop ();
}

int
main (int argc, char **argv)
{
  int opt;
  while ((opt = getopt (argc, argv, "d:w:r:")) != -1)
    if (opt == 'd')
      dir = optarg;
    else if (opt == 'w')
      wpa_bin = optarg;
    else if (opt == 'r')
      resolv = optarg;
  signal (SIGPIPE, SIG_IGN);
  mkdir (dir, 0755);
  sock = socket (AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if_up ("lo", 1);
  char all[200], wired[IFNAMSIZ], wifi[IFNAMSIZ], cfgp[300], reqp[300];
  path (cfgp, sizeof cfgp, "config");
  path (reqp, sizeof reqp, "request");
  /* the settings as they stand, for RISC OS's plug-in to start from */
  if (access (cfgp, F_OK))
    put_file ("config", "interface=auto\nmode=dhcp\nhostname=riscos\nwifi_country=GB\n");
  double last_status = 0, link_since = 0;
  int last_carrier = -1, applied = 0;
  for (;;)
    {
      /* reap wpa_supplicant if it went */
      int ws;
      pid_t w;
      while ((w = waitpid (-1, &ws, WNOHANG)) > 0)
        if (w == wpa.pid)
          {
            wpa.pid = 0;
            wpa_close_ctrl ();
            say ("wpa_supplicant stopped (%d): see /run/wpa_supplicant.log",
                 WIFEXITED (ws) ? WEXITSTATUS (ws) : 128 + WTERMSIG (ws));
            netlog ("wpa_supplicant stopped (%d)",
                    WIFEXITED (ws) ? WEXITSTATUS (ws) : 128 + WTERMSIG (ws));
          }
      struct stat st;
      time_t mt = stat (cfgp, &st) == 0 ? st.st_mtime : 0;
      int changed = 0;
      if (mt != conf_mtime)
        {
          conf_mtime = mt;
          config_read (&conf);
          if (sethostname (conf.hostname, strlen (conf.hostname))) { }
          changed = 1;
        }
      list_interfaces (wired, wifi, all, sizeof all, strcmp (conf.interface, "off") != 0);

      /* which interface */
      char pick[IFNAMSIZ] = "";
      int off = !strcmp (conf.interface, "off");
      int wired_carrier = wired[0] && !off
        ? (if_up (wired, 1), (int) read_num ("/sys/class/net/%s/carrier", wired)) : 0;
      if (off)
        ;
      else if (!strcmp (conf.interface, "wired"))
        snprintf (pick, sizeof pick, "%s", wired);
      else if (!strcmp (conf.interface, "wifi"))
        snprintf (pick, sizeof pick, "%s", wifi);
      else if (wired_carrier == 1 || !wifi[0] || (!conf.ssid[0] && wired[0]))
        snprintf (pick, sizeof pick, "%s", wired[0] ? wired : wifi);
      else
        snprintf (pick, sizeof pick, "%s", wifi);

      if (strcmp (pick, want_ifn) || changed)
        {
          if (strcmp (pick, want_ifn))
            {
              unconfigure ();
              dhcp_stop ();
              if (want_ifn[0] && (off || strcmp (want_ifn, wired)))
                if_up (want_ifn, 0);
            }
          snprintf (want_ifn, sizeof want_ifn, "%s", pick);
          last_carrier = -1;
          applied = 0;
          if (changed)
            {
              unconfigure ();
              dhcp_stop ();
            }
        }
      int wireless = want_ifn[0] && is_wireless (want_ifn);
      /* Wi-Fi: wpa_supplicant runs whenever there's a wireless interface
         and the network isn't off, so a scan can be made */
      if (wifi[0] && strcmp (conf.interface, "off") && strcmp (conf.interface, "wired"))
        wpa_update (wifi, &conf);
      else
        wifi_stop ();

      if (!want_ifn[0])
        {
          strcpy (state, strcmp (conf.interface, "off") ? "nointerface" : "off");
          if (!strcmp (conf.interface, "off"))
            say ("The network is off");
          else
            say ("No %s interface", !strcmp (conf.interface, "wifi") ? "Wi-Fi"
                 : !strcmp (conf.interface, "wired") ? "wired" : "network");
        }
      else
        {
          if_up (want_ifn, 1);
          int carrier = (int) read_num ("/sys/class/net/%s/carrier", want_ifn);
          if (carrier != last_carrier)
            {
              netlog ("%s: link %s", want_ifn, carrier == 1 ? "up" : "down");
              if (wireless)
                wpa.ps_set = 0;
              if (carrier != 1)
                {
                  unconfigure ();
                  dhcp_stop ();
                  applied = 0;
                }
              else
                link_since = now ();
              last_carrier = carrier;
            }
          if (carrier != 1)
            {
              strcpy (state, wireless && conf.ssid[0] ? "associating" : "nolink");
              if (wireless && !conf.ssid[0])
                say ("Choose a Wi-Fi network");
              else if (wireless)
                say ("Joining %s...", conf.ssid);
              else
                say ("%s: no cable (no link)", want_ifn);
            }
          else
            {
              (void) link_since;
              /* Wi-Fi power saving, as the settings say (off unless
                 wifi_powersave=on), each time the link comes up: drivers
                 can set it again as they connect */
              if (wireless && wpa.pid > 0 && !wpa.ps_set)
                {
                  char r[64];
                  int on = !strcasecmp (conf.powersave, "on");
                  if (!wpa_cmd (on ? "SET ps 1" : "SET ps 0", r, sizeof r))
                    netlog ("%s: power saving %s (%s)", want_ifn, on ? "on" : "off",
                            strncmp (r, "OK", 2) ? "not done" : "done");
                  wpa.ps_set = 1;
                }
              if (!applied)
                {
                  if (!strcmp (conf.mode, "manual"))
                    apply_manual (want_ifn);
                  else
                    dhcp_start (want_ifn);
                  applied = 1;
                }
              strcpy (state, cur.ip ? "configured" : "dhcp");
            }
        }

      /* requests from RISC OS */
      char req[64];
      if (read_small (reqp, req, sizeof req) >= 0)
        {
          unlink (reqp);
          if (!strncmp (req, "scan", 4))
            {
              char r[64];
              if (wpa.pid > 0 && !wpa_cmd ("SCAN", r, sizeof r))
                {
                  wpa.scanning = 1;
                  wpa.scan_asked = now ();
                  say ("Looking for Wi-Fi networks...");
                }
              else
                put_file ("scan", wifi[0] ? "# Wi-Fi isn't ready (try again)\n"
                                          : "# No Wi-Fi interface\n");
            }
          last_status = 0;
        }
      if (wpa.scanning && now () - wpa.scan_asked > 4)
        {
          wpa.scanning = 0;
          wpa_scan_results ();
        }

      if (dh.state != D_IDLE)
        dhcp_tick (conf.hostname);
      if (now () - last_status >= 2)
        {
          status_write (all, !want_ifn[0] ? "" : wireless ? "wifi" : "wired");
          last_status = now ();
        }
      /* wait for DHCP or a second */
      struct pollfd p = { dh.fd, POLLIN, 0 };
      if (dh.fd >= 0 && poll (&p, 1, 500) > 0)
        dhcp_input (conf.hostname);
      else if (dh.fd < 0)
        usleep (500000);
    }
}
