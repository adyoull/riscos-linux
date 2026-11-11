/* rol-snd: plays RISC OS's sound on Linux, for the RISC OS for Linux
 * images.
 *
 * RISC OS's SoundDMA (RISC OS Open's, in the port's ROM) fills its buffers
 * through the LinuxAudio module, which writes each one to a FIFO
 * (/run/riscos-sound) as a 12-byte header (magic "RSND", sample rate in Hz,
 * byte count: little-endian words) and the samples (signed 16-bit stereo,
 * left first). This reads the FIFO and plays the sound through the
 * kernel's ALSA interface (/dev/snd/pcmC*D*p: no alsa-lib), resampling when
 * the sound card can't do RISC OS's rate. LinuxAudio keeps about 50 ms
 * ahead, so the card's own pace drives RISC OS's sound.
 *
 * The card: the one named in /riscos/sound/config ("device=" an ALSA card
 * id, as in /proc/asound/cards, or a number; "auto" or nothing for the
 * first of: a USB card, an HDMI port with a screen that takes sound, the
 * headphone socket, any other).
 *
 *   rol-snd [-f FIFO] [-d CARD] [-v]
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sound/asound.h>

static const char *fifo_path = "/run/riscos-sound";
static const char *config_path = "/riscos/sound/config";
static char want_card[64] = "auto";
static int verbose;

static void
logmsg (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  fprintf (stderr, "rol-snd: ");
  vfprintf (stderr, fmt, ap);
  fputc ('\n', stderr);
  va_end (ap);
}

static int
read_file (const char *p, char *buf, size_t n)
{
  int fd = open (p, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t r = read (fd, buf, n - 1);
  close (fd);
  buf[r > 0 ? r : 0] = 0;
  return r > 0 ? (int) r : 0;
}

/* --- choosing the card ---------------------------------------------------- */

struct card { int num, dev; char id[32], name[96]; int score; };

static int
card_score (int num, const char *id, const char *name)
{
  char p[128], buf[4096];
  if (strstr (name, "USB") || strstr (id, "USB") || strstr (id, "Device"))
    {
      snprintf (p, sizeof p, "/proc/asound/card%d/usbid", num);
      if (access (p, F_OK) == 0)
	return 400;
    }
  if (strstr (id, "hdmi") || strstr (id, "HDMI") || strstr (name, "HDMI"))
    {
      /* an HDMI port with a screen that takes sound: its ELD */
      DIR *d;
      snprintf (p, sizeof p, "/proc/asound/card%d", num);
      if ((d = opendir (p)))
	{
	  struct dirent *e;
	  int present = 0;
	  while ((e = readdir (d)))
	    if (!strncmp (e->d_name, "eld#", 4))
	      {
		snprintf (p, sizeof p, "/proc/asound/card%d/%s", num, e->d_name);
		/* a screen there that lists sound formats (sad_count above 0:
		   a monitor without speakers has an ELD too, with none) */
		if (read_file (p, buf, sizeof buf) > 0 && strstr (buf, "monitor_present\t\t1"))
		  {
		    char *sc = strstr (buf, "sad_count");
		    if (!sc || atoi (sc + strcspn (sc, "0123456789")) > 0)
		      present = 1;
		  }
	      }
	  closedir (d);
	  return present ? 300 : 50;
	}
      return 50;
    }
  if (strstr (id, "Headphones") || strstr (name, "Headphones"))
    return 200;
  return 100;
}

/* the playback device to use: fills c; -1 if there's none */
static int
choose_card (struct card *c)
{
  char buf[8192];
  struct card best = { .num = -1, .score = -1 };
  if (read_file ("/proc/asound/pcm", buf, sizeof buf) <= 0)
    return -1;
  /* lines: "00-00: name : name : playback 1 : capture 1" */
  for (char *l = strtok (buf, "\n"); l; l = strtok (NULL, "\n"))
    {
      int num, dev;
      if (sscanf (l, "%d-%d:", &num, &dev) != 2 || !strstr (l, "playback"))
	continue;
      char p[64], id[32] = "", cards[4096] = "", name[96] = "";
      snprintf (p, sizeof p, "/proc/asound/card%d/id", num);
      if (read_file (p, id, sizeof id) > 0)
	id[strcspn (id, "\n")] = 0;
      /* the long name: /proc/asound/cards, " N [id   ]: driver - name" */
      if (read_file ("/proc/asound/cards", cards, sizeof cards) > 0)
	{
	  char key[16];
	  snprintf (key, sizeof key, "%2d [", num);
	  char *k = strstr (cards, key);
	  if (k && (k = strstr (k, " - ")))
	    snprintf (name, sizeof name, "%.*s", (int) strcspn (k + 3, "\n"), k + 3);
	}
      int score;
      if (strcmp (want_card, "auto") && want_card[0])
	score = (!strcmp (want_card, id) || (isdigit ((unsigned char) want_card[0]) && atoi (want_card) == num))
		? 1000 : -1;
      else
	score = card_score (num, id, name);
      score -= dev;			/* device 0 first */
      if (score > best.score)
	{
	  best.num = num;
	  best.dev = dev;
	  best.score = score;
	  snprintf (best.id, sizeof best.id, "%s", id);
	  snprintf (best.name, sizeof best.name, "%s", name);
	}
    }
  if (best.num < 0 || best.score < 0)
    return -1;
  *c = best;
  return 0;
}

static void
read_config (void)
{
  char buf[1024];
  if (read_file (config_path, buf, sizeof buf) <= 0)
    return;
  for (char *l = strtok (buf, "\r\n"); l; l = strtok (NULL, "\r\n"))
    if (!strncmp (l, "device=", 7))
      snprintf (want_card, sizeof want_card, "%s", l + 7);
}

/* --- ALSA, through the kernel's interface ------------------------------- */

static struct
{
  int fd;
  unsigned rate;		/* the card's */
  snd_pcm_uframes_t period, buffer;
  struct card card;
} pcm = { .fd = -1 };

static void
mask_one (struct snd_pcm_hw_params *p, int param, unsigned bit)
{
  struct snd_mask *m = &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
  memset (m, 0, sizeof *m);
  m->bits[bit >> 5] |= 1u << (bit & 31);
}

static void
interval (struct snd_pcm_hw_params *p, int param, unsigned min, unsigned max)
{
  struct snd_interval *i = &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
  i->min = min;
  i->max = max;
  i->openmin = i->openmax = 0;
  i->integer = 0;
  i->empty = 0;
}

static unsigned
interval_value (struct snd_pcm_hw_params *p, int param)
{
  return p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
}

static void
any (struct snd_pcm_hw_params *p)
{
  memset (p, 0, sizeof *p);
  for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
    memset (&p->masks[i], 0xff, sizeof p->masks[i]);
  for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
    interval (p, i + SNDRV_PCM_HW_PARAM_FIRST_INTERVAL, 0, UINT_MAX);
  p->rmask = ~0u;
  p->cmask = 0;
  p->info = ~0u;
}

static void
pcm_close (void)
{
  if (pcm.fd >= 0)
    {
      ioctl (pcm.fd, SNDRV_PCM_IOCTL_DROP);
      close (pcm.fd);
    }
  pcm.fd = -1;
}

/* open the card for RATE (or the nearest it does: pcm.rate) */
static int
pcm_open (unsigned rate)
{
  pcm_close ();
  read_config ();
  if (choose_card (&pcm.card))
    {
      logmsg ("no sound card");
      return -1;
    }
  char path[64];
  snprintf (path, sizeof path, "/dev/snd/pcmC%dD%dp", pcm.card.num, pcm.card.dev);
  int fd = open (path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      logmsg ("%s: %s", path, strerror (errno));
      return -1;
    }
  /* the rates to try: RISC OS's, then the usual ones */
  unsigned tries[] = { rate, 48000, 44100, 32000, 96000 };
  struct snd_pcm_hw_params hw;
  int ok = 0;
  for (unsigned t = 0; t < sizeof tries / sizeof tries[0] && !ok; t++)
    {
      any (&hw);
      mask_one (&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
      mask_one (&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
      mask_one (&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
      interval (&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
      interval (&hw, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16, 16);
      interval (&hw, SNDRV_PCM_HW_PARAM_FRAME_BITS, 32, 32);
      interval (&hw, SNDRV_PCM_HW_PARAM_RATE, tries[t], tries[t]);
      /* about 10 ms periods, 4 to 8 of them */
      unsigned per = tries[t] / 100;
      interval (&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, per / 2, per * 2);
      interval (&hw, SNDRV_PCM_HW_PARAM_PERIODS, 4, 8);
      if (ioctl (fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) == 0)
	ok = 1;
    }
  if (!ok)
    {
      /* let the card choose everything but the format */
      any (&hw);
      mask_one (&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
      mask_one (&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
      interval (&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
      if (ioctl (fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw))
	{
	  logmsg ("%s won't take 16-bit stereo: %s", path, strerror (errno));
	  close (fd);
	  return -1;
	}
    }
  pcm.rate = interval_value (&hw, SNDRV_PCM_HW_PARAM_RATE);
  pcm.period = interval_value (&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
  pcm.buffer = interval_value (&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);

  struct snd_pcm_sw_params sw;
  memset (&sw, 0, sizeof sw);
  sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
  sw.period_step = 1;
  sw.avail_min = pcm.period;
  sw.start_threshold = pcm.period * 2;
  sw.stop_threshold = pcm.buffer;
  sw.silence_threshold = 0;
  sw.silence_size = 0;
  sw.boundary = pcm.buffer;
  while (sw.boundary * 2 <= LONG_MAX - pcm.buffer)
    sw.boundary *= 2;
  if (ioctl (fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw))
    logmsg ("sw params: %s", strerror (errno));
  if (ioctl (fd, SNDRV_PCM_IOCTL_PREPARE))
    logmsg ("prepare: %s", strerror (errno));
  pcm.fd = fd;
  logmsg ("playing on card %d (%s, %s) device %d at %u Hz (RISC OS: %u Hz), period %lu, buffer %lu",
	  pcm.card.num, pcm.card.id, pcm.card.name, pcm.card.dev, pcm.rate, rate,
	  (unsigned long) pcm.period, (unsigned long) pcm.buffer);
  return 0;
}

static void
pcm_write (const int16_t *s, unsigned frames)
{
  while (frames && pcm.fd >= 0)
    {
      struct snd_xferi x = { .buf = (void *) s, .frames = frames };
      if (ioctl (pcm.fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) == 0)
	{
	  s += 2 * x.result;
	  frames -= (unsigned) x.result;
	  continue;
	}
      if (errno == EPIPE || errno == ESTRPIPE)
	{
	  /* an underrun (RISC OS was late, or had stopped): start again */
	  if (verbose)
	    logmsg ("underrun");
	  ioctl (pcm.fd, SNDRV_PCM_IOCTL_PREPARE);
	  continue;
	}
      if (errno == EINTR || errno == EAGAIN)
	continue;
      logmsg ("write: %s", strerror (errno));
      pcm_close ();
    }
}

/* --- resampling (linear), when the card can't do RISC OS's rate -------- */

static struct
{
  unsigned from, to;
  uint64_t pos;			/* in 1/65536 input frames */
  int16_t last[2];
} rs;

static unsigned
resample (const int16_t *in, unsigned n, int16_t *out, unsigned max)
{
  unsigned o = 0;
  uint64_t step = ((uint64_t) rs.from << 16) / rs.to;
  /* position 0 is the last frame of the previous buffer */
  while (o < max)
    {
      uint64_t i = rs.pos >> 16;
      if (i >= n)
	break;
      unsigned f = (unsigned) (rs.pos & 0xffff);
      for (int c = 0; c < 2; c++)
	{
	  int a = i == 0 ? rs.last[c] : in[2 * (i - 1) + c];
	  int b = in[2 * i + c];
	  out[2 * o + c] = (int16_t) (a + ((b - a) * (int) f >> 16));
	}
      o++;
      rs.pos += step;
    }
  rs.pos -= (uint64_t) n << 16;
  rs.last[0] = in[2 * (n - 1)];
  rs.last[1] = in[2 * (n - 1) + 1];
  return o;
}

/* --- main -------------------------------------------------------------- */

static int
read_all (int fd, void *buf, size_t n)
{
  size_t done = 0;
  while (done < n)
    {
      ssize_t r = read (fd, (char *) buf + done, n - done);
      if (r == 0)
	return 0;
      if (r < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -1;
	}
      done += (size_t) r;
    }
  return 1;
}

int
main (int argc, char **argv)
{
  int opt;
  while ((opt = getopt (argc, argv, "f:d:c:v")) != -1)
    switch (opt)
      {
      case 'f': fifo_path = optarg; break;
      case 'c': config_path = optarg; break;
      case 'd': snprintf (want_card, sizeof want_card, "%s", optarg); config_path = "/nonexistent"; break;
      case 'v': verbose = 1; break;
      default:
	fprintf (stderr, "usage: rol-snd [-f FIFO] [-d CARD] [-c CONFIG] [-v]\n");
	return 2;
      }
  signal (SIGPIPE, SIG_IGN);
  struct stat st;
  if (stat (fifo_path, &st) || !S_ISFIFO (st.st_mode))
    {
      unlink (fifo_path);
      if (mkfifo (fifo_path, 0666))
	{
	  logmsg ("%s: %s", fifo_path, strerror (errno));
	  return 1;
	}
    }
  chmod (fifo_path, 0666);
  static int16_t buf[65536], out[3 * 65536];
  unsigned cur_rate = 0;
  for (;;)
    {
      /* wait for RISC OS (LinuxAudio opens the FIFO when sound starts) */
      int fd = open (fifo_path, O_RDONLY | O_CLOEXEC);
      if (fd < 0)
	{
	  logmsg ("%s: %s", fifo_path, strerror (errno));
	  sleep (1);
	  continue;
	}
      if (verbose)
	logmsg ("RISC OS connected");
      for (;;)
	{
	  uint32_t h[3];
	  if (read_all (fd, h, sizeof h) <= 0)
	    break;
	  if (h[0] != 0x444E5352 || h[2] > sizeof buf || (h[2] & 3))
	    {
	      logmsg ("bad stream from RISC OS: starting again");
	      break;
	    }
	  if (read_all (fd, buf, h[2]) <= 0)
	    break;
	  unsigned frames = h[2] / 4;
	  if (h[1] != cur_rate || pcm.fd < 0)
	    {
	      cur_rate = h[1];
	      if (pcm_open (cur_rate))
		{
		  /* no card: read on (RISC OS then runs at the FIFO's pace,
		     which is as fast as it can: so wait a little) */
		  usleep (frames * 1000000ull / (cur_rate ? cur_rate : 48000));
		  continue;
		}
	      rs.from = cur_rate;
	      rs.to = pcm.rate;
	      rs.pos = 0;
	      rs.last[0] = rs.last[1] = 0;
	    }
	  if (pcm.rate == cur_rate)
	    pcm_write (buf, frames);
	  else
	    {
	      unsigned n = resample (buf, frames, out, sizeof out / 4);
	      pcm_write (out, n);
	    }
	}
      close (fd);
      /* RISC OS stopped its sound (or went): let the card stop */
      pcm_close ();
      cur_rate = 0;
      if (verbose)
	logmsg ("RISC OS disconnected");
    }
}
