/* LinuxAudio: sound for RISC OS for Linux.
 *
 * The port's ROM has RISC OS Open's own SoundDMA (1.88, the HAL version),
 * but no audio hardware for it, so it never starts. This module gives it
 * some: a HAL audio controller device (API 3, "custom DMA", synchronous),
 * of the same kind as the Raspberry Pi's BCMSound. SoundDMA, SoundChannels,
 * the linear handlers (SharedSound and the programs that use it) all work
 * as they do on real hardware; the device just hands each 16-bit stereo
 * buffer SoundDMA fills to Linux.
 *
 * To Linux: through a FIFO (default /run/riscos-sound; LinuxAudio$Path sets
 * another) that a Linux program reads (rol-snd plays it through ALSA; the
 * test bed's rol-run can save it as a WAV file). Each buffer goes as a
 * 12-byte header (magic "RSND", sample rate in Hz, byte count) and then the
 * samples (signed 16-bit, left then right, little-endian). The reader paces
 * it: on each centisecond tick (in a callback) the device fills and sends
 * buffers until about 50 ms are waiting in the FIFO, but no faster than
 * the sample rate allows over time (a machine too slow to keep up then
 * loses sound, not its responsiveness). With no reader, the buffers are
 * filled at the sample rate and dropped, so programs that wait for sound to
 * play still run at the right speed.
 *
 * Linux calls are made with the port's IXSupport_LinuxSyscall SWI.
 *
 * It also registers SharedSound's Messages file in ResourceFS, so that RISC
 * OS Open's SharedSound (from its Pi ROM: the port's ROM hasn't got it) can
 * be loaded from disc.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
#include "cmodule.h"

#define OS_ReadVarVal		0x23
#define OS_CallEvery		0x3C
#define OS_RemoveTickerEvent	0x3D
#define OS_ReadMonotonicTime	0x42
#define IXSupport_LinuxSyscall	0x59EC0
#define ResourceFS_RegisterFiles   0x41B40
#define ResourceFS_DeregisterFiles 0x41B41

/* ARM EABI Linux system calls and constants */
#define NR_write	4
#define NR_open		5
#define NR_close	6
#define NR_ioctl	54
#define NR_fcntl	55
#define NR_clock_gettime 263
#define L_O_WRONLY	1
#define L_O_NONBLOCK	0x800
#define L_O_CLOEXEC	0x80000
#define L_FIONREAD	0x541B
#define L_F_SETPIPE_SZ	1031
#define L_EAGAIN	11

/* --- the HAL device ------------------------------------------------------ */

typedef struct audiodev audiodev;
typedef void (*fill_fn) (unsigned index, void *param);

struct rate { uint32_t freq1024; uint8_t period, reserved[3]; };

struct audiodev
{
  uint16_t type, id;
  uint32_t location, version;
  const char *description;
  void *address;
  uint32_t reserved1[3];
  int (*activate) (audiodev *);
  void (*deactivate) (audiodev *);
  void (*reset) (audiodev *);
  int (*sleep) (audiodev *, int);
  int device;
  int (*testirq) (audiodev *);
  void (*clearirq) (audiodev *);
  uint32_t reserved2;
  /* audio, API 0 */
  void *mixer;
  uint32_t channels_out, channels_in;
  /* API 1 */
  uint32_t dma_params[5];
  void (*pre_enable) (audiodev *, uint32_t);
  void (*post_enable) (audiodev *, uint32_t);
  void (*pre_disable) (audiodev *);
  void (*post_disable) (audiodev *);
  int (*irq_handle) (audiodev *);
  uint32_t num_rates;
  const struct rate *rate_table;
  void (*set_rate) (audiodev *, uint32_t);
  /* API 2 */
  oserror *(*custom_dma_enable) (audiodev *, uint32_t, void *, void *, void *, fill_fn);
  uint32_t flags, min_buff_size, buff_align;
};

#define HALDeviceType_Audio_AudC	0x201
#define HALDeviceBus_Soft_Hyper		0x41000000	/* software, hypervisor */
#define AudioFlag_StereoReverse		1	/* we want left, right */
#define AudioFlag_Synchronous		2	/* fill from our callback */

/* The mixer (HAL mixer device API 0.1), which SoundControl (and so
   *MixVolume, SoundCtrl_SetMix and Configure's sound volume) talks to:
   channel 0 is the system volume, channel 1 the "speaker" (Sound_Speaker
   turns it off and on). The gain is applied to the samples here. */
typedef struct mixerdev mixerdev;
struct limits { int32_t mingain, maxgain, step; };
struct mixerdev
{
  uint16_t type, id;
  uint32_t location, version;
  const char *description;
  void *address;
  uint32_t reserved1[3];
  int (*activate) (mixerdev *);
  void (*deactivate) (mixerdev *);
  void (*reset) (mixerdev *);
  int (*sleep) (mixerdev *, int);
  int device;
  int (*testirq) (mixerdev *);
  void (*clearirq) (mixerdev *);
  uint32_t reserved2;
  audiodev *controller;
  uint32_t nchannels;
  uint32_t (*get_features) (mixerdev *, uint32_t);
  void (*set_mix) (mixerdev *, uint32_t, uint32_t, int32_t);
  uint64_t (*get_mix) (mixerdev *, uint32_t);	/* r0 = mute, r1 = gain */
  void (*get_mix_limits) (mixerdev *, uint32_t, struct limits *);
};
#define HALDeviceType_Audio_Mixer	0x202
#define MIX_CHANNELS	2
#define MIN_GAIN	(-48 * 16)	/* gains in 1/16 dB */
/* 65536 * 10^(-dB/20) for 0 to -48 dB in half-dB steps */
static const uint16_t gain_q16[97] = {
  65535, 61870, 58409, 55142, 52057, 49145, 46396, 43801, 41350, 39037, 36854,
  34792, 32846, 31008, 29274, 27636, 26090, 24631, 23253, 21952, 20724, 19565,
  18471, 17437, 16462, 15541, 14672, 13851, 13076, 12345, 11654, 11002, 10387,
  9806, 9257, 8739, 8250, 7789, 7353, 6942, 6554, 6187, 5841, 5514, 5206, 4915,
  4640, 4380, 4135, 3904, 3685, 3479, 3285, 3101, 2927, 2764, 2609, 2463, 2325,
  2195, 2072, 1957, 1847, 1744, 1646, 1554, 1467, 1385, 1308, 1234, 1165, 1100,
  1039, 981, 926, 874, 825, 779, 735, 694, 655, 619, 584, 551, 521, 491, 464,
  438, 414, 390, 369, 348, 328, 310, 293, 276, 261 };

static const uint32_t rates_hz[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000 };
#define NRATES (sizeof rates_hz / sizeof rates_hz[0])

static struct
{
  audiodev dev;
  mixerdev mixer;
  int mute[MIX_CHANNELS];
  int32_t gain[MIX_CHANNELS];
  uint32_t scale;		/* Q16, 0 = silent; from the mixer's settings */
  struct rate rates[NRATES];
  void *pw;
  /* while SoundDMA has it enabled */
  volatile int enabled;
  uint32_t bufsize;		/* bytes per buffer */
  uint8_t *buf[2];
  void *param;
  fill_fn fill;
  unsigned next;		/* buffer to fill next */
  uint32_t rate;		/* Hz */
  /* the FIFO */
  char path[200];
  int fd;
  unsigned last_try;
  unsigned last_tick;
  uint32_t clock_frac;		/* sample frames that may be filled now */
  uint8_t *out;			/* header + one buffer */
  uint32_t out_len, out_done;	/* bytes in out, bytes of it written */
  /* counts, for *LinuxAudio */
  unsigned sent, dropped, ticks, connects, slow;
  unsigned rest_until;		/* micros(): no work before this */
  volatile int callback_pending;
} la;

ERROR_BLOCK (no_ixsupport, 0x81A0, "LinuxAudio needs RISC OS for Linux (IXSupport)");
ERROR_BLOCK (no_memory, 0x81A1, "LinuxAudio: not enough memory");

static int
linux_call (int nr, unsigned a, unsigned b, unsigned c)
{
  regs r = {{ a, b, c, 0, 0, 0, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;			/* ENOSYS */
  return (int) r.r[0];
}

static unsigned
monotonic (void)
{
  regs r = {{ 0 }};
  swix_regs (OS_ReadMonotonicTime, &r);
  return r.r[0];
}

/* microseconds, from Linux's monotonic clock (wraps: only differences) */
static unsigned
micros (void)
{
  int ts[2] = { 0, 0 };
  linux_call (NR_clock_gettime, 1, (unsigned) ts, 0);	/* CLOCK_MONOTONIC */
  return (unsigned) ts[0] * 1000000u + (unsigned) ts[1] / 1000u;
}

static void
read_path (void)
{
  regs r = {{ (unsigned) "LinuxAudio$Path", (unsigned) la.path, sizeof la.path - 1, 0, 3 }};
  if (swix_regs (OS_ReadVarVal, &r) || r.r[2] == 0)
    {
      static const char def[] = "/run/riscos-sound";
      memcpy (la.path, def, sizeof def);
    }
  else
    la.path[r.r[2]] = 0;
}

/* --- the device's entry points (called by SoundDMA) --------------------- */

static int
dev_activate (audiodev *d)
{
  (void) d;
  return 1;
}

static void
dev_nothing (audiodev *d)
{
  (void) d;
}

static int
dev_sleep (audiodev *d, int state)
{
  (void) d, (void) state;
  return 0;
}

static int
dev_testirq (audiodev *d)
{
  (void) d;
  return 0;
}

static void
dev_enable (audiodev *d, uint32_t size)
{
  (void) d, (void) size;
}

static int
dev_irq (audiodev *d)
{
  (void) d;
  return 0;
}

static void
dev_set_rate (audiodev *d, uint32_t index)
{
  (void) d;
  if (index < NRATES)
    la.rate = rates_hz[index];
}

static void
close_fifo (void)
{
  if (la.fd >= 0)
    linux_call (NR_close, (unsigned) la.fd, 0, 0);
  la.fd = -1;
  la.out_len = la.out_done = 0;
}

static oserror *
dev_custom_dma_enable (audiodev *d, uint32_t size, void *b0, void *b1,
		       void *param, fill_fn fill)
{
  (void) d;
  if (size > la.bufsize || !la.out)
    {
      rma_free (la.out);
      la.out = rma_claim (size + 12);
      if (!la.out)
	return ERR (no_memory);
    }
  la.enabled = 0;
  la.bufsize = size;
  la.buf[0] = b0;
  la.buf[1] = b1;
  la.param = param;
  la.fill = fill;
  la.next = 0;
  la.clock_frac = 0;
  la.out_len = la.out_done = 0;
  read_path ();
  la.last_tick = monotonic ();
  la.last_try = la.last_tick - 1000;
  la.enabled = 1;
  return 0;
}

static void
dev_pre_disable (audiodev *d)
{
  (void) d;
  la.enabled = 0;
}

/* --- the mixer's entry points (called by SoundControl and SoundDMA) ----- */

static void
update_scale (void)
{
  uint32_t scale = 65536;
  for (int c = 0; c < MIX_CHANNELS; c++)
    {
      if (la.mute[c])
	scale = 0;
      int32_t g = la.gain[c];
      if (g > 0)
	g = 0;
      int i = (-g + 4) / 8;
      scale = i > 96 ? 0 : (uint32_t) (((uint64_t) scale * gain_q16[i]) >> 16);
    }
  la.scale = scale;
}

static int
mix_activate (mixerdev *m)
{
  (void) m;
  return 1;
}

static void
mix_nothing (mixerdev *m)
{
  (void) m;
}

static int
mix_sleep (mixerdev *m, int state)
{
  (void) m, (void) state;
  return 0;
}

static int
mix_testirq (mixerdev *m)
{
  (void) m;
  return 0;
}

/* features: bits 16-31 the category (0 = system, -1 = speaker) */
static uint32_t
mix_get_features (mixerdev *m, uint32_t c)
{
  (void) m;
  return c == 1 ? 0xFFFF0000u : 0;
}

static void
mix_set_mix (mixerdev *m, uint32_t c, uint32_t mute, int32_t gain)
{
  (void) m;
  if (c >= MIX_CHANNELS)
    return;
  if (gain < MIN_GAIN)
    gain = MIN_GAIN;
  if (gain > 0)
    gain = 0;
  la.mute[c] = mute & 1;
  la.gain[c] = gain;
  update_scale ();
}

static uint64_t
mix_get_mix (mixerdev *m, uint32_t c)
{
  (void) m;
  if (c >= MIX_CHANNELS)
    return 0;
  return (uint64_t) (uint32_t) la.gain[c] << 32 | (uint32_t) la.mute[c];
}

static void
mix_get_mix_limits (mixerdev *m, uint32_t c, struct limits *l)
{
  (void) m, (void) c;
  l->mingain = MIN_GAIN;
  l->maxgain = 0;
  l->step = 8;			/* half a dB */
}

/* --- the tick: fill buffers and send them -------------------------------- */

/* fill the next buffer; into la.out with its header */
static void
fill_next (void)
{
  unsigned i = la.next;
  la.next ^= 1;
  la.fill (i, la.param);
  uint32_t *h = (uint32_t *) la.out;
  h[0] = 0x444E5352;		/* "RSND" */
  h[1] = la.rate;
  h[2] = la.bufsize;
  if (la.scale == 65536)
    memcpy (la.out + 12, la.buf[i], la.bufsize);
  else
    {
      const int16_t *in = (const int16_t *) la.buf[i];
      int16_t *o = (int16_t *) (la.out + 12);
      for (uint32_t n = 0; n < la.bufsize / 2; n++)
	o[n] = (int16_t) ((in[n] * (int32_t) la.scale) >> 16);
    }
  la.out_len = la.bufsize + 12;
  la.out_done = 0;
}

/* write what's left of la.out: 1 when it's all gone */
static int
flush (void)
{
  while (la.out_done < la.out_len)
    {
      int n = linux_call (NR_write, (unsigned) la.fd, (unsigned) (la.out + la.out_done),
			  la.out_len - la.out_done);
      if (n == -L_EAGAIN)
	return 0;
      if (n <= 0)
	{
	  close_fifo ();	/* the reader went (EPIPE): carry on without */
	  return 0;
	}
      la.out_done += (unsigned) n;
    }
  la.out_len = la.out_done = 0;
  la.sent++;
  return 1;
}

extern void la_callback (void);
#define OS_AddCallBack 0x54

/* each centisecond, from the ticker: ask for a callback (unless one is
   still to come) to do the work */
void la_tick_irq (void);
void
la_tick_irq (void)
{
  la.ticks++;
  if (!la.enabled || la.callback_pending)
    return;
  la.callback_pending = 1;
  regs r = {{ (unsigned) la_callback, (unsigned) la.pw }};
  if (swix_regs (OS_AddCallBack, &r))
    la.callback_pending = 0;
}

void la_tick (void);
void
la_tick (void)
{
  la.callback_pending = 0;
  if (!la.enabled || !la.fill)
    return;
  unsigned now = monotonic ();
  unsigned elapsed = now - la.last_tick;
  la.last_tick = now;
  if (elapsed > 20)
    elapsed = 20;		/* after a stall, don't try to catch up */
  uint32_t frame_bytes = 4, buf_frames = la.bufsize / frame_bytes;
  if (!buf_frames)
    return;

  /* A budget of sample frames: what the time since the last tick is worth
     (plus enough to fill the FIFO when it's just been opened). A machine
     too slow to keep up loses sound, not its responsiveness. */
  uint32_t target = la.rate * frame_bytes / 20;	/* ~50 ms in the FIFO */
  if (target < 2 * (la.bufsize + 12))
    target = 2 * (la.bufsize + 12);
  la.clock_frac += la.rate * elapsed / 100;
  uint32_t cap = target / frame_bytes + buf_frames;
  if (la.clock_frac > cap)
    la.clock_frac = cap;

  if (la.fd < 0 && now - la.last_try >= 100)
    {
      la.last_try = now;
      int fd = linux_call (NR_open, (unsigned) la.path, L_O_WRONLY | L_O_NONBLOCK | L_O_CLOEXEC, 0);
      if (fd >= 0)
	{
	  la.fd = fd;
	  la.connects++;
	  linux_call (NR_fcntl, (unsigned) fd, L_F_SETPIPE_SZ, 65536);
	  la.clock_frac = cap;
	}
    }

  /* and RISC OS keeps at least half the time: after a tick's work, none
     until as long again has passed, and no more than 4 ms of it at once.
     A machine (or an emulator) that can't fill buffers in real time loses
     sound, but RISC OS mustn't stop for it. */
  unsigned t0 = micros ();
  int wait = (int) (la.rest_until - t0);
  if (wait > 0 && wait < 1000000)	/* (more than a second ahead: stale) */
    return;
  int n;
  for (n = 0; n < 16 && la.clock_frac >= buf_frames; n++)
    {
      if (n && micros () - t0 > 4000)
	{
	  la.clock_frac = 0;
	  la.slow++;
	  break;
	}
      if (la.fd >= 0)
	{
	  /* keep about 50 ms waiting for the reader */
	  if (la.out_len && !flush ())
	    break;
	  int queued = 0;
	  if (linux_call (NR_ioctl, (unsigned) la.fd, L_FIONREAD, (unsigned) &queued) < 0)
	    queued = 0;
	  if ((uint32_t) queued >= target)
	    break;
	  fill_next ();
	}
      else
	{
	  /* no reader: fill at the sample rate and drop */
	  fill_next ();
	  la.out_len = 0;
	  la.dropped++;
	}
      la.clock_frac -= buf_frames;
    }
  if (n)
    {
      unsigned t1 = micros ();
      la.rest_until = t1 + (t1 - t0);
    }
}

/* --- module ---------------------------------------------------------------- */

extern void la_ticker (void);
extern void *la_resources (void);

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail, (void) instance;
  /* make sure IXSupport answers (getpid) */
  regs r = {{ 0 }};
  r.r[7] = 20;			/* getpid */
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return ERR (no_ixsupport);
  memset (&la, 0, sizeof la);
  la.pw = pw;
  la.fd = -1;
  la.rate = rates_hz[0];	/* SoundDMA starts out assuming index 0 */
  for (unsigned i = 0; i < NRATES; i++)
    {
      la.rates[i].freq1024 = rates_hz[i] * 1024;
      la.rates[i].period = (uint8_t) ((1000000 + rates_hz[i] / 2) / rates_hz[i]);
    }
  la.scale = 65536;
  mixerdev *m = &la.mixer;
  m->type = HALDeviceType_Audio_Mixer;
  m->id = 0x7f00;
  m->location = HALDeviceBus_Soft_Hyper;
  m->version = 1;			/* API 0.1 (with GetMixLimits) */
  m->description = "RISC OS for Linux sound mixer";
  m->activate = mix_activate;
  m->deactivate = mix_nothing;
  m->reset = mix_nothing;
  m->sleep = mix_sleep;
  m->device = -1;
  m->testirq = mix_testirq;
  m->clearirq = mix_nothing;
  m->controller = &la.dev;
  m->nchannels = MIX_CHANNELS;
  m->get_features = mix_get_features;
  m->set_mix = mix_set_mix;
  m->get_mix = mix_get_mix;
  m->get_mix_limits = mix_get_mix_limits;
  audiodev *d = &la.dev;
  d->type = HALDeviceType_Audio_AudC;
  d->id = 0x7f00;
  d->location = HALDeviceBus_Soft_Hyper;
  d->version = 3 << 16;
  d->description = "RISC OS for Linux sound (to the Linux host)";
  d->activate = dev_activate;
  d->deactivate = dev_nothing;
  d->reset = dev_nothing;
  d->sleep = dev_sleep;
  d->device = -1;
  d->testirq = dev_testirq;
  d->clearirq = dev_nothing;
  d->mixer = m;
  d->channels_out = 1;
  d->channels_in = 0;
  d->pre_enable = dev_enable;
  d->post_enable = dev_enable;
  d->pre_disable = dev_pre_disable;
  d->post_disable = dev_nothing;
  d->irq_handle = dev_irq;
  d->num_rates = NRATES;
  d->rate_table = la.rates;
  d->set_rate = dev_set_rate;
  d->custom_dma_enable = dev_custom_dma_enable;
  d->flags = AudioFlag_StereoReverse | AudioFlag_Synchronous;
  d->min_buff_size = 1024;
  d->buff_align = 16;

  /* SharedSound's messages, for the Pi ROM's SharedSound (see header.s) */
  r = (regs) {{ (unsigned) la_resources () }};
  oserror *e = swix_regs (ResourceFS_RegisterFiles, &r);
  if (e)
    return e;
  r = (regs) {{ 1, (unsigned) la_ticker, (unsigned) pw }};
  e = swix_regs (OS_CallEvery, &r);
  if (e)
    {
      r = (regs) {{ (unsigned) la_resources () }};
      swix_regs (ResourceFS_DeregisterFiles, &r);
      return e;
    }
  r = (regs) {{ (unsigned) m }};
  r.r[8] = 2;			/* OS_Hardware 2: add device (the mixer, then the controller) */
  e = swix_regs (OS_Hardware, &r);
  if (!e)
    {
      r = (regs) {{ (unsigned) d }};
      r.r[8] = 2;
      e = swix_regs (OS_Hardware, &r);
      if (e)
	{
	  r = (regs) {{ (unsigned) m }};
	  r.r[8] = 3;
	  swix_regs (OS_Hardware, &r);
	}
    }
  if (e)
    {
      r = (regs) {{ (unsigned) la_ticker, (unsigned) pw }};
      swix_regs (OS_RemoveTickerEvent, &r);
      r = (regs) {{ (unsigned) la_resources () }};
      swix_regs (ResourceFS_DeregisterFiles, &r);
    }
  return e;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  regs r = {{ (unsigned) &la.dev }};
  r.r[8] = 3;			/* OS_Hardware 3: remove device (SoundDMA lets go) */
  swix_regs (OS_Hardware, &r);
  r = (regs) {{ (unsigned) &la.mixer }};
  r.r[8] = 3;
  swix_regs (OS_Hardware, &r);
  la.enabled = 0;
  r = (regs) {{ (unsigned) la_ticker, (unsigned) la.pw }};
  swix_regs (OS_RemoveTickerEvent, &r);
  if (la.callback_pending)
    {
      r = (regs) {{ (unsigned) la_callback, (unsigned) la.pw }};
      swix_regs (0x5F, &r);	/* OS_RemoveCallBack */
    }
  close_fifo ();
  r = (regs) {{ (unsigned) la_resources () }};
  swix_regs (ResourceFS_DeregisterFiles, &r);
  rma_free (la.out);
  la.out = 0;
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number, (void) r;
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
  write0 ("Sound to:     ");
  write0 (la.path[0] ? la.path : "/run/riscos-sound");
  write0 (la.fd >= 0 ? " (playing)" : " (no Linux program reading: sound is dropped)");
  newline ();
  write0 ("SoundDMA:     ");
  if (la.enabled)
    {
      write_dec (la.rate);
      write0 (" Hz, buffers of ");
      write_dec (la.bufsize / 4);
      write0 (" samples");
    }
  else
    write0 ("sound off");
  newline ();
  write0 ("Buffers sent: ");
  write_dec (la.sent);
  write0 (", dropped: ");
  write_dec (la.dropped);
  write0 (", connections: ");
  write_dec (la.connects);
  newline ();
  if (la.slow)
    {
      write0 ("Too slow to keep up: ");
      write_dec (la.slow);
      write0 (" times");
      newline ();
    }
  return 0;
}

/* libgcc's division by zero calls raise(SIGFPE): the result is then 0 */
int raise (int sig);
int
raise (int sig)
{
  (void) sig;
  return 0;
}
