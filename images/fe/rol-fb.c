/* rol-fb: a full-screen front end for RISC OS for Linux on a Linux
 * console: the screen on the framebuffer (/dev/fb0), the keyboard and
 * mouse (or tablet) from evdev (/dev/input/event*). No X, no SDL.
 *
 *   rol-fb [-p] [-n] -- COMMAND [ARGS...]
 *
 * Runs COMMAND (qemu-arm-riscos with the port's ROM) with a SocketKVM
 * connection, as the port's SDL front end does, and asks RISC OS for a
 * screen mode the size of the framebuffer (at the start through the
 * ROL$ScreenWidth and ROL$ScreenHeight variables, so that a mode saved in
 * Configure wins; then whenever the host's window changes size). RISC
 * OS's pointer is drawn in
 * software. Mouse buttons as on RISC OS: left = Select, middle = Menu,
 * right = Adjust (-p: right = Menu, middle = Adjust, for a mouse or
 * trackpad without a middle button). -n: a real monitor (a Raspberry Pi's
 * HDMI): never set a display mode of its own, only the monitor's preferred
 * one, and scale other RISC OS modes to fit it (a virtual GPU takes any
 * size; a monitor shows only the modes it supports).
 * The scroll wheel goes as key numbers &1F0-&1F3 (up, down, left, right),
 * for the KVMWheel module. The display is set to the RISC OS mode's size,
 * so RISC OS's pixels aren't scaled here; RISC OS is asked for a mode the
 * size of the host's window when that changes.
 * Exits with COMMAND's exit status.
 *
 * The protocol (SocketKVM_Protocol.h) and the key numbers are Timothy
 * Baldwin's, from the Linux port's SocketKVMFrontends (Apache-2.0).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <grp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <elf.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <drm/drm_fourcc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "linux2ro.h"

enum { R_MOUSE = 0, R_KEYDOWN = 1, R_KEYUP = 2, R_MODE_SYNC = 3,
       R_RESIZE = 4, R_CLOSE = 5, R_VERSION = 6, R_MOUSE_REL = 7 };
enum { C_MODE_CHANGE = 0, C_SET_PALETTE = 1, C_POINTER = 2, C_SUSPEND = 3,
       C_CLOSE_CTL = 4, C_VERSION = 6, C_ACTIVESCREEN = 7, C_STARTSCREEN = 8,
       C_LEDS = 9, C_POINTER_POS = 10 };
/* The protocol version this front end reports: 2 = it sets the keyboard
   LEDs (C_LEDS), so SocketKVM sends them; 3 = it draws the pointer where
   RISC OS says (C_POINTER_POS), so SocketKVM sends that */
#define FE_VERSION 3
/* SocketKVM's version (its C_VERSION): from 3 (RISC OS 5.31-19) a mouse's
   movement goes to RISC OS as relative moves (R_MOUSE_REL), which RISC OS
   scales by its mouse speed (Configure's Mouse, *Configure MouseStep), and
   the pointer is drawn where RISC OS then has it (C_POINTER_POS). Before,
   and for tablets (absolute: UTM, VMs), rol-fb gives RISC OS the pointer's
   position itself (R_MOUSE), which the mouse speed doesn't apply to. */
static unsigned ro_version;
static int rel_mouse;			/* the pointer is RISC OS's (C_POINTER_POS) */
static int clampi (int v, int lo, int hi);
/* The keyboard LEDs as RISC OS has them (C_LEDS), -1 until it says */
static int ro_leds = -1;
static void set_leds (int fd);
enum { RO_SELECT = 0x70, RO_MENU = 0x71, RO_ADJUST = 0x72 };

struct report { int32_t reason; uint32_t a, b, c; };

#define SCREEN_MAX (1024 * 1024 * 100)
#define MAX_INPUTS 32

static int sock = -1, screen_fd = -1;
static unsigned char *pixels;
static int width, height, log2bpp = -1, mode_changed;
static int row_bytes;				/* width * bytes per pixel + extra */
static uint32_t ncolour, modeflags;
#define MF_64K 0x80				/* 16 bpp: 5:6:5 */
#define MF_RGB 0x4000				/* &xRGB, not &xBGR */
/* scaling: output pixel (x, y) shows RISC OS pixel (xmap[x], ymap[y]) */
static int *xmap, *ymap, map_w, map_h, map_valid;
static int ox, oy, sw, sh;			/* the picture's place on the output */
static uint32_t palette[256];			/* 0x00BBGGRR */
static uint32_t ptr_pal[4];			/* 0x00RRGGBB; [0] unused */
static unsigned char ptr_shape[32][32];		/* colour 0-3 */
static int ptr_h, ptr_ax, ptr_ay, ptr_changed;
/* the pointer's scale: the desktop's, in whole multiples (a 1920 x 1080
   desktop on a 4K monitor is shown twice the size, so is the pointer) */
static int ps = 1;
static uint32_t start_addr, active_addr;
static int have_start, have_active;
static int pcmouse, realmonitor;

/* framebuffer */
static int fb_fd = -1, fb_w, fb_h, fb_bpp, fb_stride;
static int drm_fd = -1;
static uint32_t drm_conn, drm_crtc, drm_fb, drm_handle;
static unsigned host_hz;	/* the display mode's frame rate (DRM) */
static size_t fb_size;
static unsigned char *fb;
static uint32_t *back;				/* fb_w x fb_h, 0x00RRGGBB */
static uint32_t *last_src;			/* copy of what was drawn */
static size_t last_len;

/* input */
static struct { int fd; char name[32]; int absmax_x, absmax_y; } inputs[MAX_INPUTS];
static int ninputs;
static int mx, my;				/* pointer, RISC OS pixels from top left */
static unsigned buttons;			/* bit 0 select, 1 menu, 2 adjust */
static int key_down[256];

static void
send_report (int reason, uint32_t a, uint32_t b, uint32_t c)
{
  struct report r = { reason, a, b, c };
  if (write (sock, &r, sizeof r) < 0 && errno != EAGAIN && errno != EPIPE)
    perror ("rol-fb: write");
}

#define OV_KEY 0x080008u
#define OV_BUFS 32
static int ov_listen = -1, ov_client = -1;
static uint32_t ov_plane, prim_plane, ov_zpos_ok;
/* Drivers whose primary plane must stay at the bottom (the Pi's vc4: zpos
   0, fixed; every overlay above it): with a video, the desktop goes on a
   second overlay plane above the video's, and the primary shows black. */
static uint32_t desk_plane, blk_fb, blk_handle;
static int desk_on;
static int ov_fake;			/* /riscos/overlay-test: no plane, log only */
static int ov_need, ov_shown = -1;		/* ARGB desktop; index on screen */
static int ov_x, ov_y, ov_w, ov_h;		/* RISC OS pixels: the key area */
static struct { uint32_t fb, handle; } ov_buf[OV_BUFS];
static struct drm_mode_modeinfo cur_mode;
static int have_cur_mode;


/* --- the output: DRM/KMS (follows the VM window's size), else fbdev --- */

/* The connector's preferred mode (the host's window size with UTM's
   dynamic resolution), or its first. */
static int
drm_pick (struct drm_mode_modeinfo *mode, uint32_t *conn_id, uint32_t *crtc_id)
{
  struct drm_mode_card_res res = { 0 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
    return -1;
  uint32_t conns[16], crtcs[16], encs[16], fbs[16];
  if (res.count_connectors > 16 || res.count_crtcs > 16 || res.count_encoders > 16 || res.count_fbs > 16)
    return -1;
  res.connector_id_ptr = (uintptr_t) conns;
  res.crtc_id_ptr = (uintptr_t) crtcs;
  res.encoder_id_ptr = (uintptr_t) encs;
  res.fb_id_ptr = (uintptr_t) fbs;
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
    return -1;
  for (uint32_t i = 0; i < res.count_connectors; i++)
    {
      struct drm_mode_get_connector c = { .connector_id = conns[i] };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.connection != 1 || !c.count_modes)
	continue;
      struct drm_mode_modeinfo modes[64];
      uint32_t cencs[16];
      if (c.count_modes > 64)
	c.count_modes = 64;
      if (c.count_encoders > 16)
	c.count_encoders = 16;
      c.modes_ptr = (uintptr_t) modes;
      c.encoders_ptr = (uintptr_t) cencs;
      c.count_props = 0;
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c))
	continue;
      int best = 0;
      for (uint32_t m = 0; m < c.count_modes; m++)
	if (modes[m].type & DRM_MODE_TYPE_PREFERRED)
	  {
	    best = m;
	    break;
	  }
      *mode = modes[best];
      /* (said once, and again only when it changes: this is asked often) */
      static uint32_t said_conn, said_w, said_h;
      int say_it = conns[i] != said_conn || modes[best].hdisplay != said_w
		   || modes[best].vdisplay != said_h;
      if (say_it)
	fprintf (stderr, "rol-fb: connector %u (type %u-%u): %u modes, using %ux%u %u Hz%s\n",
		 conns[i], c.connector_type, c.connector_type_id, c.count_modes,
		 modes[best].hdisplay, modes[best].vdisplay, modes[best].vrefresh,
		 modes[best].type & DRM_MODE_TYPE_PREFERRED ? " (preferred)" : "");
      said_conn = conns[i];
      said_w = modes[best].hdisplay;
      said_h = modes[best].vdisplay;
      *conn_id = conns[i];
      *crtc_id = crtcs[0];
      if (c.encoder_id)
	{
	  struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id };
	  if (!ioctl (drm_fd, DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id)
	    *crtc_id = e.crtc_id;
	}
      if (say_it)
	fprintf (stderr, "rol-fb: encoder %u, CRTC %u\n", c.encoder_id, *crtc_id);
      return 0;
    }
  return -1;
}

static void
drm_release (void)
{
  if (fb && fb != MAP_FAILED)
    munmap (fb, fb_size);
  fb = NULL;
  if (drm_fb)
    ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &drm_fb);
  if (drm_handle)
    {
      struct drm_mode_destroy_dumb d = { .handle = drm_handle };
      ioctl (drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
  drm_fb = drm_handle = 0;
}

/* The display's own (hardware) cursor: a clear one, so that a VM's host
   shows no pointer of its own over RISC OS's (UTM draws its arrow where
   the guest has set none), and a monitor shows no stray one either. */
static void
drm_hide_cursor (void)
{
  static uint32_t handle;
  if (!handle)
    {
      struct drm_mode_create_dumb cd = { .width = 64, .height = 64, .bpp = 32 };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
	return;
      struct drm_mode_map_dumb md = { .handle = cd.handle };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0)
	{
	  void *p = mmap (NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, md.offset);
	  if (p != MAP_FAILED)
	    {
	      memset (p, 0, cd.size);	/* (fully transparent ARGB) */
	      munmap (p, cd.size);
	    }
	}
      handle = cd.handle;
    }
  struct drm_mode_cursor c = { .flags = DRM_MODE_CURSOR_BO, .crtc_id = drm_crtc,
			       .width = 64, .height = 64, .handle = handle };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_CURSOR, &c))
    fprintf (stderr, "rol-fb: no hardware cursor to hide (%s)\n", strerror (errno));
}

static void ov_under (void);
static void blk_free (void);

static int
drm_setup (const struct drm_mode_modeinfo *mode)
{
  drm_release ();
  struct drm_mode_create_dumb cd = { .width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
    return -1;
  drm_handle = cd.handle;
  if (ov_need)
    {
      /* with a video overlay: ARGB, so that the key colour can be clear */
      struct drm_mode_fb_cmd2 f2 = { .width = cd.width, .height = cd.height,
				     .pixel_format = DRM_FORMAT_ARGB8888 };
      f2.handles[0] = cd.handle;
      f2.pitches[0] = cd.pitch;
      if (ioctl (drm_fd, DRM_IOCTL_MODE_ADDFB2, &f2))
	return -1;
      drm_fb = f2.fb_id;
    }
  else
    {
      struct drm_mode_fb_cmd fc = { .width = cd.width, .height = cd.height, .pitch = cd.pitch,
				    .bpp = 32, .depth = 24, .handle = cd.handle };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_ADDFB, &fc))
	return -1;
      drm_fb = fc.fb_id;
    }
  cur_mode = *mode;
  have_cur_mode = 1;
  struct drm_mode_map_dumb md = { .handle = cd.handle };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &md))
    return -1;
  fb_size = cd.size;
  fb = mmap (NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, md.offset);
  if (fb == MAP_FAILED)
    return -1;
  memset (fb, 0, fb_size);
  struct drm_mode_crtc cr = { .crtc_id = drm_crtc, .fb_id = drm_fb,
			      .set_connectors_ptr = (uintptr_t) &drm_conn, .count_connectors = 1,
			      .mode = *mode, .mode_valid = 1 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_SETCRTC, &cr))
    {
      perror ("rol-fb: SETCRTC");
      return -1;
    }
  drm_hide_cursor ();
  if (desk_plane && !ov_need && desk_on)
    {
      /* the desktop back on the primary (just set): its plane off */
      struct drm_mode_set_plane off = { .plane_id = desk_plane, .crtc_id = drm_crtc };
      ioctl (drm_fd, DRM_IOCTL_MODE_SETPLANE, &off);
      desk_on = 0;
      blk_free ();
    }
  if (ov_need)
    ov_under ();
  fb_w = mode->hdisplay;
  fb_h = mode->vdisplay;
  fb_stride = cd.pitch;
  fb_bpp = 32;
  free (back);
  back = calloc ((size_t) fb_w * fb_h, 4);
  map_valid = 0;
  fprintf (stderr, "rol-fb: display %dx%d (DRM)\n", fb_w, fb_h);
  return back ? 0 : -1;
}

/* The size the host asks for (its window, with UTM's dynamic resolution):
   followed by asking RISC OS for a mode that size. The display itself is
   set to the RISC OS mode's size (drm_match_mode), so that nothing is
   scaled here: the host scales it to its window, smoothly. */
static int host_w, host_h, failed_w, failed_h;

static int
drm_open (void)
{
  struct drm_mode_modeinfo mode;
  /* the first card with a screen connected: a GPU that only renders
     (Panfrost, Lima, V3D) may be card0, with the display on card1 */
  for (int n = 0; n < 8 && host_w == 0; n++)
    {
      char path[32];
      snprintf (path, sizeof path, "/dev/dri/card%d", n);
      if (drm_fd >= 0)
	close (drm_fd);
      drm_fd = open (path, O_RDWR | O_CLOEXEC);
      if (drm_fd >= 0)
	fprintf (stderr, "rol-fb: trying %s\n", path);
      if (drm_fd >= 0 && drm_pick (&mode, &drm_conn, &drm_crtc) == 0)
	{
	  host_w = mode.hdisplay;
	  host_h = mode.vdisplay;
	  host_hz = mode.vrefresh;
	}
    }
  if (drm_fd < 0)
    return -1;
  if (host_w == 0 || drm_setup (&mode))
    {
      drm_release ();
      close (drm_fd);
      drm_fd = -1;
      return -1;
    }
  return 0;
}

/* The window changed size? Then a new framebuffer, and 1 to tell RISC OS. */
static int
drm_check_resize (void)
{
  struct drm_mode_modeinfo mode;
  uint32_t conn, crtc;
  if (drm_fd < 0 || drm_pick (&mode, &conn, &crtc))
    return 0;
  if (mode.hdisplay == host_w && mode.vdisplay == host_h)
    return 0;
  host_w = mode.hdisplay;
  host_h = mode.vdisplay;
  drm_conn = conn;
  drm_crtc = crtc;
  return drm_setup (&mode) == 0;
}

/* A display mode exactly w x h (60 Hz timings made up: a virtual GPU
   takes any size), so the RISC OS mode isn't scaled here. 0 if done. */
static int
drm_match_mode (int w, int h)
{
  if (drm_fd < 0 || (w == fb_w && h == fb_h) || (w == failed_w && h == failed_h))
    return -1;
  if (realmonitor)
    {
      failed_w = w;
      failed_h = h;
      return -1;
    }
  struct drm_mode_modeinfo m;
  memset (&m, 0, sizeof m);
  m.hdisplay = w;
  m.hsync_start = w + 48;
  m.hsync_end = w + 80;
  m.htotal = w + 160;
  m.vdisplay = h;
  m.vsync_start = h + 3;
  m.vsync_end = h + 9;
  m.vtotal = h + 35;
  m.vrefresh = 60;
  m.clock = (uint32_t) ((uint64_t) m.htotal * m.vtotal * 60 / 1000);
  m.type = DRM_MODE_TYPE_USERDEF;
  snprintf (m.name, sizeof m.name, "%dx%d", w, h);
  if (drm_setup (&m) == 0)
    return 0;
  fprintf (stderr, "rol-fb: no %dx%d display mode: scaling\n", w, h);
  failed_w = w;
  failed_h = h;
  struct drm_mode_modeinfo pref;
  uint32_t conn, crtc;
  if (drm_pick (&pref, &conn, &crtc) == 0)
    drm_setup (&pref);
  return -1;
}

/* --- the video overlay: a YUV plane under the desktop (Raspberry Pi) ---

   LinuxCodec (a RISC OS module) connects to the abstract socket
   "rol-fb-overlay" and hands over the decoder's picture buffers (dmabufs,
   SCM_RIGHTS). Each is made a DRM framebuffer (YUV 4:2:0) once; showing a
   picture puts it on a KMS overlay plane at the window's place, scaled by
   the display hardware. The overlay goes under the desktop's plane, which
   then has an alpha channel: the program paints its video area in the key
   colour, and those pixels (inside the overlay's rectangle) are made
   transparent, so windows and menus on top of the video still show. If the
   driver can't put the plane underneath (no zpos), it goes on top.

   Messages: 16 words, [0] = command, replies 16 words, [0] = 0 or -errno.
     1 OPEN                       -> [1] key colour 0x00RRGGBB, [2] under (1/0)
     2 ADDBUF index w h fourcc pitch0 off0 pitch1 off1 pitch2 off2 (+ fd)
     3 SHOW index x y w h sx sy sw sh  (RISC OS pixels from the top left;
                                       source rectangle in picture pixels)
                                  -> [1] the index shown before (now free) or -1
     4 HIDE                       -> [1] the index shown before or -1
     5 FORGET                     (all buffers: the decoder's were remade)
     6 ALLOC index w h fourcc     a buffer made here (a dumb buffer, for
                                  VideoOverlay through LinuxOverlay)
                                  -> [1] size, [2..7] pitch/offset of each
                                     plane (+ its dmabuf fd, to map)
   SHOW's [10]: bit 0 BT.709 (else 601), bit 1 full range (else video). */
static int
prop_id (uint32_t obj, const char *name, uint64_t *value)
{
  uint32_t ids[64];
  uint64_t vals[64];
  struct drm_mode_obj_get_properties gp = { .props_ptr = (uintptr_t) ids, .prop_values_ptr = (uintptr_t) vals,
					    .count_props = 64, .obj_id = obj, .obj_type = DRM_MODE_OBJECT_PLANE };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gp))
    return 0;
  for (uint32_t i = 0; i < gp.count_props && i < 64; i++)
    {
      struct drm_mode_get_property pr = { .prop_id = ids[i] };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPROPERTY, &pr))
	continue;
      if (!strcmp (pr.name, name))
	{
	  if (value)
	    *value = vals[i];
	  return (int) ids[i];
	}
    }
  return 0;
}

static int
set_prop (uint32_t obj, const char *name, uint64_t value)
{
  int id = prop_id (obj, name, NULL);
  if (!id)
    return -1;
  struct drm_mode_obj_set_property sp = { .value = value, .prop_id = (uint32_t) id, .obj_id = obj,
					  .obj_type = DRM_MODE_OBJECT_PLANE };
  return ioctl (drm_fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &sp);
}

/* An overlay plane for our CRTC that takes YUV 4:2:0, and the primary */
static void
ov_find_planes (void)
{
  struct drm_set_client_cap cap = { DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1 };
  ioctl (drm_fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
  uint32_t crtcs[16];
  struct drm_mode_card_res res = { .crtc_id_ptr = (uintptr_t) crtcs, .count_crtcs = 16 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
    return;
  int ci = -1;
  for (uint32_t i = 0; i < res.count_crtcs && i < 16; i++)
    if (crtcs[i] == drm_crtc)
      ci = (int) i;
  if (ci < 0)
    return;
  uint32_t planes[64];
  struct drm_mode_get_plane_res pr = { .plane_id_ptr = (uintptr_t) planes, .count_planes = 64 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr))
    return;
  ov_plane = prim_plane = desk_plane = 0;
  for (uint32_t i = 0; i < pr.count_planes && i < 64; i++)
    {
      uint32_t fmts[128];
      struct drm_mode_get_plane gp = { .plane_id = planes[i], .format_type_ptr = (uintptr_t) fmts, .count_format_types = 128 };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPLANE, &gp) || !(gp.possible_crtcs & (1u << ci)))
	continue;
      uint64_t type = 0;
      prop_id (planes[i], "type", &type);
      if (type == 1 /* primary */ && !prim_plane)
	prim_plane = planes[i];
      if (type != 0 /* overlay */)
	continue;
      int yuv = 0, argb = 0;
      for (uint32_t f = 0; f < gp.count_format_types && f < 128; f++)
	{
	  yuv |= fmts[f] == DRM_FORMAT_YUV420;
	  argb |= fmts[f] == DRM_FORMAT_ARGB8888;
	}
      if (yuv && !ov_plane)
	ov_plane = planes[i];
      else if (argb && !desk_plane)
	desk_plane = planes[i];
    }
  if (ov_plane)
    fprintf (stderr, "rol-fb: video overlay plane %u (primary %u, desktop over video %u)\n",
	     ov_plane, prim_plane, desk_plane);
}

static void
ov_reply (int32_t a, int32_t b, int32_t c)
{
  int32_t m[16] = { a, b, c };
  if (ov_client >= 0)
    send (ov_client, m, sizeof m, MSG_NOSIGNAL);
}

/* A reply carrying a file descriptor */
static void
ov_reply_fd (const int32_t *m, int fd)
{
  union { char buf[CMSG_SPACE (sizeof (int))]; struct cmsghdr align; } ctl;
  struct iovec iov = { (void *) m, 16 * sizeof (int32_t) };
  struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl.buf, .msg_controllen = sizeof ctl.buf };
  struct cmsghdr *c = CMSG_FIRSTHDR (&msg);
  c->cmsg_level = SOL_SOCKET;
  c->cmsg_type = SCM_RIGHTS;
  c->cmsg_len = CMSG_LEN (sizeof (int));
  memcpy (CMSG_DATA (c), &fd, sizeof fd);
  if (ov_client >= 0)
    sendmsg (ov_client, &msg, MSG_NOSIGNAL);
}

/* The colours of what's on the overlay: BT.601/709, video/full range */
static void
ov_colours (int flags)
{
  static int now = -1;
  if (flags == now || !ov_plane)
    return;
  now = flags;
  set_prop (ov_plane, "COLOR_ENCODING", flags & 1 ? 1 : 0);
  set_prop (ov_plane, "COLOR_RANGE", flags & 2 ? 1 : 0);
}

static void
ov_forget (void)
{
  for (int i = 0; i < OV_BUFS; i++)
    {
      if (ov_buf[i].fb)
	ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &ov_buf[i].fb);
      if (ov_buf[i].handle)
	{
	  struct drm_gem_close gc = { .handle = ov_buf[i].handle };
	  ioctl (drm_fd, DRM_IOCTL_GEM_CLOSE, &gc);
	}
      ov_buf[i].fb = ov_buf[i].handle = 0;
    }
}

static int
ov_hide (void)
{
  int prev = ov_shown;
  if (ov_plane && ov_shown >= 0)
    {
      struct drm_mode_set_plane sp = { .plane_id = ov_plane, .crtc_id = drm_crtc };
      ioctl (drm_fd, DRM_IOCTL_MODE_SETPLANE, &sp);
    }
  ov_shown = -1;
  ov_w = ov_h = 0;
  return prev;
}

/* The desktop on desk_plane, over the video, with a black primary under
   both (drm_setup has just set the CRTC with the desktop's framebuffer). */
static void
blk_free (void)
{
  if (blk_fb)
    ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &blk_fb);
  if (blk_handle)
    {
      struct drm_mode_destroy_dumb d = { .handle = blk_handle };
      ioctl (drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
  blk_fb = blk_handle = 0;
}

static int
desk_setup (void)
{
  desk_on = 0;
  blk_free ();
  if (!desk_plane || !have_cur_mode)
    return -1;
  struct drm_mode_create_dumb cd = { .width = cur_mode.hdisplay, .height = cur_mode.vdisplay, .bpp = 32 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
    return -1;
  blk_handle = cd.handle;		/* (dumb buffers start zeroed: black) */
  struct drm_mode_fb_cmd fc = { .width = cd.width, .height = cd.height, .pitch = cd.pitch,
				.bpp = 32, .depth = 24, .handle = cd.handle };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_ADDFB, &fc))
    return -1;
  blk_fb = fc.fb_id;
  if (set_prop (ov_plane, "zpos", 1) || set_prop (desk_plane, "zpos", 2))
    return -1;
  struct drm_mode_set_plane sp = { .plane_id = desk_plane, .crtc_id = drm_crtc, .fb_id = drm_fb,
				   .crtc_w = cur_mode.hdisplay, .crtc_h = cur_mode.vdisplay,
				   .src_w = (uint32_t) cur_mode.hdisplay << 16,
				   .src_h = (uint32_t) cur_mode.vdisplay << 16 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_SETPLANE, &sp))
    {
      fprintf (stderr, "rol-fb: desktop plane: %s\n", strerror (errno));
      return -1;
    }
  struct drm_mode_crtc cr = { .crtc_id = drm_crtc, .fb_id = blk_fb,
			      .set_connectors_ptr = (uintptr_t) &drm_conn, .count_connectors = 1,
			      .mode = cur_mode, .mode_valid = 1 };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_SETCRTC, &cr))
    {
      /* back to the desktop on the primary */
      struct drm_mode_set_plane off = { .plane_id = desk_plane, .crtc_id = drm_crtc };
      ioctl (drm_fd, DRM_IOCTL_MODE_SETPLANE, &off);
      return -1;
    }
  desk_on = 1;
  return 0;
}

/* Under the desktop if the driver allows it: the primary over the video,
   else (vc4) the desktop on its own plane over the video's */
static void
ov_under (void)
{
  ov_zpos_ok = 0;
  if (!ov_plane)
    return;
  if (prim_plane && set_prop (ov_plane, "zpos", 0) == 0 && set_prop (prim_plane, "zpos", 1) == 0)
    ov_zpos_ok = 1;
  else
    ov_zpos_ok = desk_setup () == 0;
  fprintf (stderr, "rol-fb: video %s the desktop%s\n", ov_zpos_ok ? "under" : "over",
	   desk_on ? " (the desktop on its own plane)" : "");
}

/* The desktop's plane needs an alpha channel for the key: remake it */
static void
ov_want_alpha (int on)
{
  if (ov_need == on)
    return;
  ov_need = on;
  if (have_cur_mode)
    drm_setup (&cur_mode);
  map_valid = 0;
}

static void
ov_message (void)
{
  int32_t m[16];
  union { char buf[CMSG_SPACE (sizeof (int))]; struct cmsghdr align; } ctl;
  struct iovec iov = { m, sizeof m };
  struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl.buf, .msg_controllen = sizeof ctl.buf };
  memset (m, 0, sizeof m);
  ssize_t n = recvmsg (ov_client, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
  if (n == 0 || (n < 0 && errno != EAGAIN))
    {
      /* LinuxCodec went: take the video off the screen */
      ov_hide ();
      ov_forget ();
      close (ov_client);
      ov_client = -1;
      ov_want_alpha (0);
      return;
    }
  if (n < 4)
    return;
  int fd = -1;
  for (struct cmsghdr *c = CMSG_FIRSTHDR (&msg); c; c = CMSG_NXTHDR (&msg, c))
    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
      memcpy (&fd, CMSG_DATA (c), sizeof fd);
  switch (m[0])
    {
    case 1:				/* OPEN */
      ov_fake = (drm_fd < 0 || !ov_plane) && access ("/riscos/overlay-test", F_OK) == 0;
      if (ov_fake)
	{
	  fprintf (stderr, "rol-fb: overlay test: OPEN\n");
	  ov_reply (0, (int32_t) OV_KEY, 1);
	  break;
	}
      if (drm_fd < 0 || !ov_plane)
	{
	  ov_reply (-ENODEV, 0, 0);
	  break;
	}
      ov_want_alpha (1);
      ov_reply (0, (int32_t) OV_KEY, (int32_t) ov_zpos_ok);
      break;
    case 2:				/* ADDBUF */
      {
	int i = m[1];
	if (fd < 0 || i < 0 || i >= OV_BUFS)
	  {
	    ov_reply (-EINVAL, 0, 0);
	    break;
	  }
	if (ov_buf[i].fb)
	  ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &ov_buf[i].fb);
	ov_buf[i].fb = 0;
	struct drm_prime_handle ph = { .fd = fd };
	int r = ioctl (drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph);
	if (r == 0)
	  {
	    struct drm_mode_fb_cmd2 fc = { .width = (uint32_t) m[2], .height = (uint32_t) m[3],
					   .pixel_format = (uint32_t) m[4] };
	    for (int p = 0; p < 3; p++)
	      {
		fc.handles[p] = ph.handle;
		fc.pitches[p] = (uint32_t) m[5 + p * 2];
		fc.offsets[p] = (uint32_t) m[6 + p * 2];
	      }
	    r = ioctl (drm_fd, DRM_IOCTL_MODE_ADDFB2, &fc);
	    ov_buf[i].handle = ph.handle;
	    ov_buf[i].fb = r == 0 ? fc.fb_id : 0;
	  }
	ov_reply (r ? -errno : 0, 0, 0);
	break;
      }
    case 6:				/* ALLOC */
      {
	int i = m[1], w = m[2], h = m[3];
	uint32_t fourcc = (uint32_t) m[4];
	int32_t r[16];
	memset (r, 0, sizeof r);
	if (ov_fake)
	  {
	    /* (memory only: what a dumb buffer's layout would be) */
	    uint32_t yp = ((uint32_t) w + 63) & ~63u, ch = (uint32_t) (h + 1) / 2;
	    r[1] = (int32_t) (yp * ((uint32_t) h + ch));
	    r[2] = (int32_t) yp;
	    r[4] = (int32_t) (fourcc == DRM_FORMAT_NV12 ? yp : yp / 2);
	    r[5] = (int32_t) (yp * (uint32_t) h);
	    r[6] = fourcc == DRM_FORMAT_NV12 ? 0 : (int32_t) (yp / 2);
	    r[7] = fourcc == DRM_FORMAT_NV12 ? 0 : (int32_t) (yp * (uint32_t) h + yp / 2 * ch);
	    int mf = (int) syscall (SYS_memfd_create, "rol-fb-overlay-test", 1u);
	    if (mf < 0 || ftruncate (mf, r[1]) < 0)
	      {
		ov_reply (-ENOMEM, 0, 0);
		if (mf >= 0)
		  close (mf);
		break;
	      }
	    fprintf (stderr, "rol-fb: overlay test: ALLOC %d %dx%d %.4s size %d\n", i, w, h, (char *) &fourcc, r[1]);
	    ov_reply_fd (r, mf);
	    close (mf);
	    break;
	  }
	if (drm_fd < 0 || i < 0 || i >= OV_BUFS || w <= 0 || h <= 0 || w > 8192 || h > 8192
	    || (fourcc != DRM_FORMAT_YUV420 && fourcc != DRM_FORMAT_NV12))
	  {
	    ov_reply (-EINVAL, 0, 0);
	    break;
	  }
	if (ov_shown == i)
	  ov_hide ();
	if (ov_buf[i].fb)
	  ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &ov_buf[i].fb);
	if (ov_buf[i].handle)
	  {
	    struct drm_gem_close gc = { .handle = ov_buf[i].handle };
	    ioctl (drm_fd, DRM_IOCTL_GEM_CLOSE, &gc);
	  }
	ov_buf[i].fb = ov_buf[i].handle = 0;
	/* one 8-bit buffer: Y rows, then the chroma (each chroma row of the
	   two 4:2:0 planes is half a Y row) */
	uint32_t ch = (uint32_t) (h + 1) / 2;
	struct drm_mode_create_dumb cd = { .width = ((uint32_t) w + 63) & ~63u, .height = (uint32_t) h + ch, .bpp = 8 };
	if (ioctl (drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
	  {
	    ov_reply (-errno, 0, 0);
	    break;
	  }
	uint32_t yp = cd.pitch & ~1u;
	struct drm_mode_fb_cmd2 fc = { .width = (uint32_t) w, .height = (uint32_t) h, .pixel_format = fourcc };
	fc.handles[0] = fc.handles[1] = cd.handle;
	fc.pitches[0] = yp;
	fc.offsets[1] = yp * (uint32_t) h;
	if (fourcc == DRM_FORMAT_NV12)
	  fc.pitches[1] = yp;
	else
	  {
	    fc.handles[2] = cd.handle;
	    fc.pitches[1] = fc.pitches[2] = yp / 2;
	    fc.offsets[2] = fc.offsets[1] + yp / 2 * ch;
	  }
	struct drm_prime_handle ph = { .handle = cd.handle, .flags = DRM_CLOEXEC | DRM_RDWR, .fd = -1 };
	if (ioctl (drm_fd, DRM_IOCTL_MODE_ADDFB2, &fc) || ioctl (drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph))
	  {
	    int e = errno;
	    if (fc.fb_id)
	      ioctl (drm_fd, DRM_IOCTL_MODE_RMFB, &fc.fb_id);
	    struct drm_gem_close gc = { .handle = cd.handle };
	    ioctl (drm_fd, DRM_IOCTL_GEM_CLOSE, &gc);
	    ov_reply (-e, 0, 0);
	    break;
	  }
	ov_buf[i].handle = cd.handle;
	ov_buf[i].fb = fc.fb_id;
	r[1] = (int32_t) cd.size;
	for (int p = 0; p < 3; p++)
	  {
	    r[2 + p * 2] = (int32_t) fc.pitches[p];
	    r[3 + p * 2] = (int32_t) fc.offsets[p];
	  }
	ov_reply_fd (r, ph.fd);
	close (ph.fd);
	break;
      }
    case 3:				/* SHOW */
      {
	int i = m[1];
	if (ov_fake)
	  {
	    static int nshow;
	    if (nshow++ < 40 || nshow % 100 == 0)
	      fprintf (stderr, "rol-fb: overlay test: SHOW %d (#%d) at %d,%d %dx%d from %d,%d %dx%d colours %d\n",
		       i, nshow, m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10]);
	    ov_reply (0, -1, 0);
	    break;
	  }
	ov_colours (m[10]);
	if (i < 0 || i >= OV_BUFS || !ov_buf[i].fb || width <= 0 || height <= 0)
	  {
	    ov_reply (-EINVAL, -1, 0);
	    break;
	  }
	ov_x = m[2]; ov_y = m[3]; ov_w = m[4]; ov_h = m[5];
	/* RISC OS pixels to the display's: as the desktop is scaled */
	long dx = ox + (long) ov_x * sw / width, dy = oy + (long) ov_y * sh / height;
	long dw = (long) ov_w * sw / width, dh = (long) ov_h * sh / height;
	struct drm_mode_set_plane sp = { .plane_id = ov_plane, .crtc_id = drm_crtc, .fb_id = ov_buf[i].fb,
					 .crtc_x = (int32_t) dx, .crtc_y = (int32_t) dy,
					 .crtc_w = (uint32_t) dw, .crtc_h = (uint32_t) dh,
					 .src_x = (uint32_t) m[6] << 16, .src_y = (uint32_t) m[7] << 16,
					 .src_w = (uint32_t) m[8] << 16, .src_h = (uint32_t) m[9] << 16 };
	int prev = ov_shown;
	if (dw <= 0 || dh <= 0 || ioctl (drm_fd, DRM_IOCTL_MODE_SETPLANE, &sp))
	  {
	    ov_reply (-errno ? -errno : -EINVAL, -1, 0);
	    break;
	  }
	ov_shown = i;
	{
	  /* redraw with the key area, when it moved or the video wasn't shown */
	  static int lx = -1, ly, lw, lh;
	  if (prev < 0 || lx != ov_x || ly != ov_y || lw != ov_w || lh != ov_h)
	    map_valid = 0;
	  lx = ov_x; ly = ov_y; lw = ov_w; lh = ov_h;
	}
	ov_reply (0, prev == i ? -1 : prev, 0);
	break;
      }
    case 4:				/* HIDE */
      if (ov_fake)
	fprintf (stderr, "rol-fb: overlay test: HIDE\n");
      ov_reply (0, ov_hide (), 0);
      map_valid = 0;
      break;
    case 5:				/* FORGET */
      ov_hide ();
      ov_forget ();
      ov_reply (0, 0, 0);
      break;
    default:
      ov_reply (-EINVAL, 0, 0);
    }
  if (fd >= 0)
    close (fd);
}

static void
ov_accept (void)
{
  int c = accept4 (ov_listen, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (c < 0)
    return;
  if (ov_client >= 0)
    {
      /* one program's video at a time: the new one takes over */
      ov_hide ();
      ov_forget ();
      close (ov_client);
    }
  ov_client = c;
}

static void
ov_start (void)
{
  if (drm_fd < 0)
    return;
  ov_find_planes ();
  if (!ov_plane && access ("/riscos/overlay-test", F_OK) != 0)
    return;
  ov_listen = socket (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  struct sockaddr_un a = { .sun_family = AF_UNIX };
  memcpy (a.sun_path, "\0rol-fb-overlay", 15);
  if (ov_listen < 0 || bind (ov_listen, (struct sockaddr *) &a, offsetof (struct sockaddr_un, sun_path) + 15)
      || listen (ov_listen, 2))
    {
      perror ("rol-fb: overlay socket");
      if (ov_listen >= 0)
	close (ov_listen);
      ov_listen = -1;
    }
}

static int
fb_open (void)
{
  struct fb_var_screeninfo v;
  struct fb_fix_screeninfo f;
  if (drm_open () == 0)
    return 0;
  fb_fd = open ("/dev/fb0", O_RDWR | O_CLOEXEC);
  if (fb_fd < 0)
    {
      perror ("rol-fb: /dev/fb0");
      return -1;
    }
  if (ioctl (fb_fd, FBIOGET_VSCREENINFO, &v) || ioctl (fb_fd, FBIOGET_FSCREENINFO, &f))
    {
      perror ("rol-fb: fb ioctl");
      return -1;
    }
  fb_w = v.xres;
  fb_h = v.yres;
  fb_bpp = v.bits_per_pixel;
  fb_stride = f.line_length;
  if (fb_bpp != 32 && fb_bpp != 16)
    {
      fprintf (stderr, "rol-fb: %d bits per pixel framebuffer not supported\n", fb_bpp);
      return -1;
    }
  fb = mmap (NULL, (size_t) fb_stride * fb_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
  if (fb == MAP_FAILED)
    {
      perror ("rol-fb: mmap fb");
      return -1;
    }
  back = calloc ((size_t) fb_w * fb_h, 4);
  fprintf (stderr, "rol-fb: framebuffer %dx%d, %d bpp\n", fb_w, fb_h, fb_bpp);
  return back ? 0 : -1;
}

static void
console_graphics (int on)
{
  int t = open ("/dev/tty0", O_RDWR | O_CLOEXEC);
  if (t < 0)
    t = open ("/dev/tty1", O_RDWR | O_CLOEXEC);
  if (t >= 0)
    {
      ioctl (t, KDSETMODE, on ? KD_GRAPHICS : KD_TEXT);
      close (t);
    }
}

static inline uint32_t
src_pixel (const unsigned char *base, int x, int y)
{
  const unsigned char *row = base + (size_t) y * row_bytes;
  uint32_t r, g, b;
  switch (log2bpp)
    {
    case 3:
      {
	uint32_t c = palette[row[x]];
	return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
      }
    case 4:
      {
	unsigned v = row[x * 2] | (row[x * 2 + 1] << 8);
	if (ncolour == 4095)			/* 4:4:4:4 */
	  {
	    r = (v & 15) * 17; g = ((v >> 4) & 15) * 17; b = ((v >> 8) & 15) * 17;
	  }
	else if (modeflags & MF_64K)		/* 5:6:5 */
	  {
	    r = v & 31; g = (v >> 5) & 63; b = (v >> 11) & 31;
	    r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
	  }
	else					/* 1:5:5:5 */
	  {
	    r = v & 31; g = (v >> 5) & 31; b = (v >> 10) & 31;
	    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
	  }
	break;
      }
    case 5:
      {
	const unsigned char *q = row + x * 4;
	r = q[0]; g = q[1]; b = q[2];
	break;
      }
    default:
      return 0;
    }
  if (modeflags & MF_RGB)
    {
      uint32_t t = r; r = b; b = t;
    }
  return (r << 16) | (g << 8) | b;
}

/* Fit the RISC OS screen in the output: as big as it goes, keeping its
   shape (and its pixels square, as RISC OS's are), in the middle. */
static void
make_maps (void)
{
  free (xmap);
  free (ymap);
  xmap = malloc (sizeof (int) * fb_w);
  ymap = malloc (sizeof (int) * fb_h);
  double sc = (double) fb_w / width;
  if ((double) fb_h / height < sc)
    sc = (double) fb_h / height;
  if (sc >= 1)
    sc = (int) sc;			/* whole multiples: crisp pixels */
  sw = (int) (width * sc + 0.5);
  sh = (int) (height * sc + 0.5);
  if (sw > fb_w) sw = fb_w;
  if (sh > fb_h) sh = fb_h;
  ox = (fb_w - sw) / 2;
  oy = (fb_h - sh) / 2;
  ps = sw / width;
  if (ps < 1)
    ps = 1;
  if (ps > 8)
    ps = 8;
  for (int x = 0; x < fb_w; x++)
    xmap[x] = x < ox || x >= ox + sw ? -1 : (int) ((long long) (x - ox) * width / sw);
  for (int y = 0; y < fb_h; y++)
    ymap[y] = y < oy || y >= oy + sh ? -1 : (int) ((long long) (y - oy) * height / sh);
  map_w = width;
  map_h = height;
  map_valid = 1;
}

/* Copy the output rectangle [x0,x1) x [y0,y1) from back to the display,
   with the key colour made clear in the video's area and the pointer
   (32x32 at px,py, ps times the size) drawn on. */
static void
out_rect (int x0, int y0, int x1, int y1, int px, int py)
{
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > fb_w) x1 = fb_w;
  if (y1 > fb_h) y1 = fb_h;
  for (int y = y0; y < y1; y++)
    {
      const uint32_t *row = back + (size_t) y * fb_w;
      unsigned char *d = fb + (size_t) y * fb_stride;
      int pr = y - py;
      int has_ptr = pr >= 0 && pr < ptr_h * ps && pr < 32 * ps;
      pr = has_ptr ? pr / ps : 0;
      if (fb_bpp == 32)
	{
	  uint32_t *d32 = (uint32_t *) d;
	  if (!ov_need)
	    memcpy (d32 + x0, row + x0, (size_t) (x1 - x0) * 4);
	  else
	    {
	      /* opaque, but for the key colour inside the video's area */
	      int ky = ov_shown >= 0 && ov_w > 0 && ymap[y] >= ov_y && ymap[y] < ov_y + ov_h;
	      for (int x = x0; x < x1; x++)
		{
		  uint32_t c = row[x];
		  int k = ky && c == OV_KEY && xmap[x] >= ov_x && xmap[x] < ov_x + ov_w;
		  d32[x] = k ? 0 : c | 0xFF000000u;
		}
	    }
	  if (!has_ptr)
	    continue;
	  for (int i = 0; i < 32 * ps; i++)
	    {
	      int x = px + i;
	      if (x < x0 || x >= x1)
		continue;
	      unsigned c = ptr_shape[pr][i / ps];
	      if (c)
		d32[x] = ptr_pal[c] | 0xFF000000u;
	    }
	}
      else
	{
	  uint16_t *d16 = (uint16_t *) d;
	  for (int x = x0; x < x1; x++)
	    {
	      uint32_t c = row[x];
	      if (has_ptr && x >= px && x < px + 32 * ps && ptr_shape[pr][(x - px) / ps])
		c = ptr_pal[ptr_shape[pr][(x - px) / ps]];
	      d16[x] = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F);
	    }
	}
    }
}

/* Tell the display which parts changed (drivers that need it) */
static void
flush_rects (const struct drm_clip_rect *r, int n)
{
  if (drm_fd >= 0 && drm_fb && n > 0)
    {
      struct drm_mode_fb_dirty_cmd d = { .fb_id = drm_fb, .num_clips = (uint32_t) n, .clips_ptr = (uintptr_t) r };
      static int reported;
      if (ioctl (drm_fd, DRM_IOCTL_MODE_DIRTYFB, &d) && !reported++)
	perror ("rol-fb: DIRTYFB");
    }
}

/* The first and last bytes that differ in a and b (n bytes, known to
   differ) */
static void
diff_span (const unsigned char *a, const unsigned char *b, size_t n, size_t *first, size_t *last)
{
  size_t f = 0, l = n;
  while (f + 8 <= n && !memcmp (a + f, b + f, 8))
    f += 8;
  while (f < n && a[f] == b[f])
    f++;
  while (l >= f + 8 && !memcmp (a + l - 8, b + l - 8, 8))
    l -= 8;
  while (l > f && a[l - 1] == b[l - 1])
    l--;
  *first = f;
  *last = l ? l - 1 : 0;
}

/* Redraw what changed: the screen (only the part of it that changed:
   RISC OS's screen memory is compared row by row with a copy, and only
   the rectangle that changed is converted, scaled and sent), the palette
   or mode (all of it), or the pointer (where it was and where it is).
   ROL_FB_FULL=1 in rol-fb's environment redraws everything every time. */
static void
redraw (int force)
{
  static int last_mx = -1, last_my = -1, last_px = 0, last_py = 0, drawn;
  static int full_always = -1;
  if (full_always < 0)
    {
      const char *e = getenv ("ROL_FB_FULL");
      full_always = e && *e == '1';
    }
  if (log2bpp < 3 || width <= 0 || height <= 0 || screen_fd < 0 || !fb)
    return;
  const unsigned char *base = pixels;
  if (have_start && have_active && active_addr >= start_addr)
    base += active_addr - start_addr;
  size_t len = (size_t) row_bytes * height;
  struct stat st;
  if (fstat (screen_fd, &st) || (size_t) st.st_size < len + (base - pixels))
    return;
  if (!map_valid || map_w != width || map_h != height)
    {
      make_maps ();
      force = 1;
    }
  int full = force || full_always || mode_changed || len != last_len || !last_src || !drawn;
  /* the source rectangle that changed, in RISC OS pixels (x1, y1 inclusive) */
  int sx0 = width, sy0 = height, sx1 = -1, sy1 = -1;
  int shift = log2bpp - 3;			/* bytes per pixel = 1 << shift */
  size_t rb = (size_t) width << shift;
  if (rb > (size_t) row_bytes)
    rb = row_bytes;
  if (full)
    {
      sx0 = 0; sy0 = 0; sx1 = width - 1; sy1 = height - 1;
    }
  else
    {
      for (int y = 0; y < height; y++)
	{
	  const unsigned char *a = base + (size_t) y * row_bytes;
	  const unsigned char *b = (const unsigned char *) last_src + (size_t) y * row_bytes;
	  if (!memcmp (a, b, rb))
	    continue;
	  size_t f, l;
	  diff_span (a, b, rb, &f, &l);
	  int fx = (int) (f >> shift), lx = (int) (l >> shift);
	  if (fx < sx0) sx0 = fx;
	  if (lx > sx1) sx1 = lx;
	  if (y < sy0) sy0 = y;
	  sy1 = y;
	}
    }
  int screen_changed = sy1 >= 0;
  if (!screen_changed && !ptr_changed && mx == last_mx && my == last_my)
    return;
  struct drm_clip_rect clips[3];
  int nclips = 0;
  int px = ox + (int) ((long long) mx * sw / width) - ptr_ax * ps;
  int py = oy + (int) ((long long) my * sh / height) - ptr_ay * ps;
  if (screen_changed)
    {
      if (len != last_len)
	{
	  free (last_src);
	  last_src = malloc (len);
	  last_len = len;
	  full = 1;
	  sx0 = 0; sy0 = 0; sx1 = width - 1; sy1 = height - 1;
	}
      if (last_src)
	{
	  size_t a = (size_t) sx0 << shift, n = ((size_t) (sx1 - sx0 + 1)) << shift;
	  for (int y = sy0; y <= sy1; y++)
	    memcpy ((unsigned char *) last_src + (size_t) y * row_bytes + a, base + (size_t) y * row_bytes + a, n);
	}
      mode_changed = 0;
      const unsigned char *src = last_src ? (const unsigned char *) last_src : base;
      /* the output rectangle those pixels scale to */
      int ox0 = fb_w, ox1 = -1, oy0 = fb_h, oy1 = -1;
      if (full)
	{
	  ox0 = 0; ox1 = fb_w - 1; oy0 = 0; oy1 = fb_h - 1;
	}
      else
	{
	  for (int x = 0; x < fb_w; x++)
	    if (xmap[x] >= sx0 && xmap[x] <= sx1)
	      {
		if (x < ox0) ox0 = x;
		ox1 = x;
	      }
	  for (int y = 0; y < fb_h; y++)
	    if (ymap[y] >= sy0 && ymap[y] <= sy1)
	      {
		if (y < oy0) oy0 = y;
		oy1 = y;
	      }
	}
      if (ox1 >= ox0 && oy1 >= oy0)
	{
	  for (int y = oy0; y <= oy1; y++)
	    {
	      uint32_t *row = back + (size_t) y * fb_w;
	      int sy = ymap[y];
	      if (sy < 0)
		{
		  memset (row + ox0, 0, (size_t) (ox1 - ox0 + 1) * 4);
		  continue;
		}
	      if (y > oy0 && ymap[y - 1] == sy)
		{
		  memcpy (row + ox0, row - fb_w + ox0, (size_t) (ox1 - ox0 + 1) * 4);
		  continue;
		}
	      for (int x = ox0; x <= ox1; x++)
		row[x] = xmap[x] < 0 ? 0 : src_pixel (src, xmap[x], sy);
	    }
	  out_rect (ox0, oy0, ox1 + 1, oy1 + 1, px, py);
	  clips[nclips++] = (struct drm_clip_rect) { (unsigned short) ox0, (unsigned short) oy0,
						     (unsigned short) (ox1 + 1), (unsigned short) (oy1 + 1) };
	}
    }
  /* the pointer: put back what was under it, and draw it where it is */
  if (!full && (ptr_changed || mx != last_mx || my != last_my || screen_changed))
    {
      int rx[2] = { last_px, px }, ry[2] = { last_py, py };
      for (int i = 0; i < 2; i++)
	{
	  int x0 = rx[i] < 0 ? 0 : rx[i], y0 = ry[i] < 0 ? 0 : ry[i];
	  int x1 = rx[i] + 32 * ps > fb_w ? fb_w : rx[i] + 32 * ps;
	  int y1 = ry[i] + 32 * ps > fb_h ? fb_h : ry[i] + 32 * ps;
	  if (x1 <= x0 || y1 <= y0)
	    continue;
	  out_rect (x0, y0, x1, y1, px, py);
	  clips[nclips++] = (struct drm_clip_rect) { (unsigned short) x0, (unsigned short) y0,
						     (unsigned short) x1, (unsigned short) y1 };
	}
    }
  last_mx = mx;
  last_my = my;
  last_px = px;
  last_py = py;
  ptr_changed = 0;
  drawn = 1;
  if (full)
    {
      out_rect (0, 0, fb_w, fb_h, px, py);
      clips[0] = (struct drm_clip_rect) { 0, 0, (unsigned short) fb_w, (unsigned short) fb_h };
      nclips = 1;
    }
  flush_rects (clips, nclips);
  static int frames;
  if (frames++ < 3)
    fprintf (stderr, "rol-fb: frame %d drawn (%dx%d at %d,%d)\n", frames, sw, sh, ox, oy);
}

/* --- messages from RISC OS -------------------------------------------- */
static void
read_messages (void)
{
  union { int32_t reason; uint32_t w[80]; unsigned char b[400]; } c;
  for (;;)
    {
      union { char buf[CMSG_SPACE (sizeof (int)) * 2]; struct cmsghdr align; } ctl;
      struct iovec iov = { &c, sizeof c };
      struct msghdr msg = { 0 };
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = ctl.buf;
      msg.msg_controllen = sizeof ctl.buf;
      ssize_t s = recvmsg (sock, &msg, MSG_DONTWAIT);
      if (s < 4)
	return;
      for (struct cmsghdr *m = CMSG_FIRSTHDR (&msg); m; m = CMSG_NXTHDR (&msg, m))
	if (m->cmsg_level == SOL_SOCKET && m->cmsg_type == SCM_RIGHTS)
	  {
	    if (screen_fd >= 0)
	      close (screen_fd);
	    memcpy (&screen_fd, CMSG_DATA (m), sizeof (int));
	    void *p = mmap (pixels, SCREEN_MAX, PROT_READ, MAP_FIXED | MAP_SHARED, screen_fd, 0);
	    if (p == MAP_FAILED)
	      perror ("rol-fb: mmap screen");
	  }
      switch (c.reason)
	{
	case C_MODE_CHANGE:
	  {
	    int extra = 0;
	    log2bpp = c.w[1 + 1];
	    width = c.w[1 + 5];
	    height = c.w[1 + 11];
	    ncolour = 0;
	    modeflags = 0;
	    /* the VIDC list's control list: index, value pairs, then -1 */
	    for (int k = 1 + 16; k + 1 < 80 && c.w[k] != 0xFFFFFFFFu; k += 2)
	      switch (c.w[k])
		{
		case 14: extra = c.w[k + 1]; break;	/* ExtraBytes */
		case 15: ncolour = c.w[k + 1]; break;	/* NColour */
		case 16: modeflags = c.w[k + 1]; break;	/* ModeFlags */
		}
	    row_bytes = ((width << log2bpp) >> 3) + extra;
	    if (mx >= width) mx = width - 1;
	    if (my >= height) my = height - 1;
	    mode_changed = 1;
	    fprintf (stderr, "rol-fb: mode %dx%d, %d bpp, ncolour %u, flags &%x, extra %d\n",
		     width, height, 1 << log2bpp, ncolour, modeflags, extra);
	  }
	  send_report (R_MODE_SYNC, 0, 0, 0);
	  break;
	case C_SET_PALETTE:		/* colour (0xBBGGRR00), index, type */
	  if (c.w[3] == 0 && c.w[2] < 256)
	    {
	      palette[c.w[2]] = c.w[1] >> 8;
	      mode_changed = 1;
	    }
	  else if (c.w[3] == 2 && c.w[2] < 4)
	    {
	      uint32_t v = c.w[1];
	      ptr_pal[c.w[2]] = (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
	      ptr_changed = 1;
	    }
	  break;
	case C_POINTER:			/* width, height, active x, y, visible, data */
	  {
	    const unsigned char *p = c.b + 4;
	    ptr_h = p[4] ? p[1] : 0;
	    if (ptr_h > 32)
	      ptr_h = 32;
	    ptr_ax = p[2];
	    ptr_ay = p[3];
	    const unsigned char *src = p + 5;
	    for (int y = 0; y < 32; y++)
	      for (int x = 0; x < 32; x++)
		ptr_shape[y][x] = y < ptr_h ? (src[y * 8 + x / 4] >> ((x & 3) * 2)) & 3 : 0;
	    ptr_changed = 1;
	    break;
	  }
	case C_VERSION:
	  ro_version = c.w[1];
	  for (int k = 0; k < 256; k++)
	    if (key_down[k])
	      send_report (R_KEYDOWN, k, 0, 0);
	  send_report (R_VERSION, FE_VERSION, 0, 0);
	  break;
	case C_ACTIVESCREEN:
	  active_addr = c.w[1];
	  have_active = 1;
	  break;
	case C_STARTSCREEN:
	  start_addr = c.w[1];
	  have_start = 1;
	  break;
	case C_POINTER_POS:		/* the pointer's top left, pixels */
	  if (rel_mouse && width > 0 && height > 0)
	    {
	      mx = clampi ((int) c.w[1] + ptr_ax, 0, width - 1);
	      my = clampi ((int) c.w[2] + ptr_ay, 0, height - 1);
	    }
	  break;
	case C_LEDS:			/* bit 0 Caps Lock, 1 Num Lock, 2 Scroll Lock */
	  ro_leds = (int) (c.w[1] & 7);
	  for (int i = 0; i < ninputs; i++)
	    set_leds (inputs[i].fd);
	  break;
	default:
	  break;
	}
    }
}

/* --- input ------------------------------------------------------------- */

/* Set a device's LEDs to RISC OS's (a device without LEDs ignores it) */
static void
set_leds (int fd)
{
  if (ro_leds < 0 || fd < 0)
    return;
  struct input_event ev[4];
  memset (ev, 0, sizeof ev);
  static const unsigned short led[3] = { LED_CAPSL, LED_NUML, LED_SCROLLL };
  for (int i = 0; i < 3; i++)
    {
      ev[i].type = EV_LED;
      ev[i].code = led[i];
      ev[i].value = (ro_leds >> i) & 1;
    }
  ev[3].type = EV_SYN;
  ev[3].code = SYN_REPORT;
  if (write (fd, ev, sizeof ev) < 0 && errno != EBADF && errno != EINVAL)
    {
      static int said;
      if (!said++)
	perror ("rol-fb: keyboard LEDs");
    }
}

static void
scan_inputs (void)
{
  DIR *d = opendir ("/dev/input");
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir (d)) && ninputs < MAX_INPUTS)
    {
      if (strncmp (e->d_name, "event", 5))
	continue;
      int known = 0;
      for (int i = 0; i < ninputs; i++)
	known |= !strcmp (inputs[i].name, e->d_name);
      if (known)
	continue;
      char path[300];
      snprintf (path, sizeof path, "/dev/input/%s", e->d_name);
      /* read and write: writing EV_LED events sets a keyboard's LEDs */
      int fd = open (path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0)
	fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0)
	continue;
      struct input_absinfo ax = { 0 }, ay = { 0 };
      ioctl (fd, EVIOCGABS (ABS_X), &ax);
      ioctl (fd, EVIOCGABS (ABS_Y), &ay);
      ioctl (fd, EVIOCGRAB, 1);		/* keys don't go to the console too */
      inputs[ninputs].fd = fd;
      snprintf (inputs[ninputs].name, sizeof inputs[ninputs].name, "%s", e->d_name);
      inputs[ninputs].absmax_x = ax.maximum;
      inputs[ninputs].absmax_y = ay.maximum;
      char nm[128] = "";
      ioctl (fd, EVIOCGNAME (sizeof nm), nm);
      fprintf (stderr, "rol-fb: input %s: %s\n", e->d_name, nm);
      set_leds (fd);			/* a keyboard plugged in: RISC OS's LEDs */
      ninputs++;
    }
  closedir (d);
}

static int
clampi (int v, int lo, int hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

/* Ctrl+Break (or Ctrl+Alt+Backspace, for keyboards without Break): stop
   RISC OS at once, as Ctrl+Break resets a real RISC OS machine, so a hung
   RISC OS can always be left. What each of its threads was doing goes to
   the log first. */
static pid_t riscos_pid;
static int ctrl_l, ctrl_r, alt_l, alt_r, shift_l, shift_r, hard_quit;

/* A running thread: where it is. Stopped (ptrace, no signal), its
   registers read, let go, a few times over: a hung RISC OS shows the loop
   it's in. The 32-bit registers (r0-r15, cpsr) as a 64-bit tracer gets
   them. */
static void
sample_thread (int tid)
{
  if (ptrace (PTRACE_SEIZE, tid, 0, 0) != 0)
    {
      fprintf (stderr, "rol-fb:     (can't look: %s)\n", strerror (errno));
      return;
    }
  for (int i = 0; i < 12; i++)
    {
      int st;
      if (ptrace (PTRACE_INTERRUPT, tid, 0, 0) != 0
	  || waitpid (tid, &st, __WALL) != tid)
	break;
      uint32_t r[18] = { 0 };
      struct iovec io = { r, sizeof r };
      if (ptrace (PTRACE_GETREGSET, tid, (void *) NT_PRSTATUS, &io) == 0)
	{
	  uint32_t code[2] = { 0, 0 };
	  struct iovec l = { code, sizeof code }, rm = { (void *) (uintptr_t) (r[15] & ~3u), sizeof code };
	  if (!(r[16] & 0x20))
	    process_vm_readv (tid, &l, 1, &rm, 1, 0);
	  fprintf (stderr, "rol-fb:     pc %08x lr %08x sp %08x psr %08x"
		   " r0 %08x r1 %08x r2 %08x r3 %08x [%08x %08x]\n",
		   r[15], r[14], r[13], r[16], r[0], r[1], r[2], r[3], code[0], code[1]);
	}
      ptrace (PTRACE_CONT, tid, 0, 0);
      struct timespec t = { 0, 40000000 };
      nanosleep (&t, 0);
    }
  ptrace (PTRACE_DETACH, tid, 0, 0);
}

static void
stop_riscos (void)
{
  if (hard_quit || riscos_pid <= 0)
    return;
  hard_quit = 1;
  fprintf (stderr, "rol-fb: Ctrl+Break: stopping RISC OS. Its threads were:\n");
  char path[64];
  snprintf (path, sizeof path, "/proc/%d/task", (int) riscos_pid);
  DIR *d = opendir (path);
  struct dirent *e;
  while (d && (e = readdir (d)))
    {
      if (e->d_name[0] == '.')
	continue;
      char f[400], stat[512] = "", sys[256] = "";
      snprintf (f, sizeof f, "%s/%s/stat", path, e->d_name);
      int fd = open (f, O_RDONLY | O_CLOEXEC);
      ssize_t n = fd >= 0 ? read (fd, stat, sizeof stat - 1) : -1;
      if (fd >= 0)
	close (fd);
      stat[n > 0 ? n : 0] = 0;
      /* "pid (comm) S ...": the name and the state */
      char *rp = strrchr (stat, ')');
      snprintf (f, sizeof f, "%s/%s/syscall", path, e->d_name);
      fd = open (f, O_RDONLY | O_CLOEXEC);
      n = fd >= 0 ? read (fd, sys, sizeof sys - 1) : -1;
      if (fd >= 0)
	close (fd);
      sys[n > 0 ? n : 0] = 0;
      sys[strcspn (sys, "\n")] = 0;
      fprintf (stderr, "rol-fb:   thread %s %.*s state %c; syscall/sp/pc: %s\n",
	       e->d_name, rp ? (int) (rp - strchr (stat, '(') + 1) : 0,
	       rp ? strchr (stat, '(') : "", rp && rp[1] ? rp[2] : '?', sys);
      if (rp && rp[1] == ' ' && rp[2] == 'R')
	sample_thread (atoi (e->d_name));
    }
  if (d)
    closedir (d);
  kill (riscos_pid, SIGKILL);
}

static void
button (int key, unsigned bit, int down)
{
  send_report (down ? R_KEYDOWN : R_KEYUP, key, 0, 0);
  if (down)
    buttons |= bit;
  else
    buttons &= ~bit;
}

static void
read_input (int i)
{
  struct input_event ev[64];
  ssize_t n;
  while ((n = read (inputs[i].fd, ev, sizeof ev)) > 0)
    {
      int moved = 0, rel_dx = 0, rel_dy = 0;
      int w = width > 0 ? width : fb_w, h = height > 0 ? height : fb_h;
      for (int k = 0; k < n / (ssize_t) sizeof ev[0]; k++)
	{
	  struct input_event *e = &ev[k];
	  if (e->type == EV_KEY)
	    {
	      int down = e->value != 0;
	      if (e->value == 2)
		continue;		/* autorepeat: RISC OS repeats itself */
	      switch (e->code)
		{
		case KEY_LEFTCTRL: ctrl_l = down; break;
		case KEY_RIGHTCTRL: ctrl_r = down; break;
		case KEY_LEFTALT: alt_l = down; break;
		case KEY_RIGHTALT: alt_r = down; break;
		case KEY_LEFTSHIFT: shift_l = down; break;
		case KEY_RIGHTSHIFT: shift_r = down; break;
		}
	      if (down && (ctrl_l || ctrl_r)
		  && (((e->code == KEY_PAUSE || e->code == KEY_BREAK) && !(alt_l || alt_r))
		      || (e->code == KEY_BACKSPACE && (alt_l || alt_r))))
		{
		  stop_riscos ();
		  continue;
		}
	      /* Alt+Break is RISC OS's own (the Wimp offers to stop the task
		 that isn't letting go): Break is passed on as key 15. For
		 keyboards without Break (a Mac's), Alt+Shift+Backspace is
		 Break too. */
	      if (e->code == KEY_BACKSPACE && (alt_l || alt_r) && (shift_l || shift_r)
		  && !(ctrl_l || ctrl_r))
		{
		  if (down)
		    {
		      /* RISC OS mustn't see Shift with it (Shift+Break
			 restarts RISC OS): let go of Shift round it */
		      int ls = linux2ro[KEY_LEFTSHIFT], rs = linux2ro[KEY_RIGHTSHIFT];
		      if (shift_l && ls >= 0)
			send_report (R_KEYUP, ls, 0, 0);
		      if (shift_r && rs >= 0)
			send_report (R_KEYUP, rs, 0, 0);
		      send_report (R_KEYDOWN, 15, 0, 0);
		      send_report (R_KEYUP, 15, 0, 0);
		      if (shift_l && ls >= 0)
			send_report (R_KEYDOWN, ls, 0, 0);
		      if (shift_r && rs >= 0)
			send_report (R_KEYDOWN, rs, 0, 0);
		    }
		  continue;
		}
	      switch (e->code)
		{
		case BTN_LEFT:
		  button (RO_SELECT, 1, down);
		  continue;
		case BTN_RIGHT:
		  button (pcmouse ? RO_MENU : RO_ADJUST, pcmouse ? 2 : 4, down);
		  continue;
		case BTN_MIDDLE:
		  button (pcmouse ? RO_ADJUST : RO_MENU, pcmouse ? 4 : 2, down);
		  continue;
		case BTN_TOUCH:
		  continue;
		}
	      if (e->code < 256 && linux2ro[e->code] >= 0)
		{
		  key_down[linux2ro[e->code]] = down;
		  send_report (down ? R_KEYDOWN : R_KEYUP, linux2ro[e->code], 0, 0);
		}
	    }
	  else if (e->type == EV_REL)
	    {
	      if ((e->code == REL_X || e->code == REL_Y) && ro_version >= 3)
		{
		  /* a mouse, and RISC OS takes relative moves: it applies
		     its mouse speed, and says where the pointer is */
		  if (e->code == REL_X)
		    rel_dx += e->value;
		  else
		    rel_dy += e->value;
		  rel_mouse = 1;
		}
	      else if (e->code == REL_X)
		mx = clampi (mx + e->value, 0, w - 1), moved = 1;
	      else if (e->code == REL_Y)
		my = clampi (my + e->value, 0, h - 1), moved = 1;
	      else if (e->code == REL_WHEEL || e->code == REL_HWHEEL)
		{
		  /* the scroll wheel: no SocketKVM report for it, so as
		     key numbers &1F0-&1F3 (up, down, left, right) that the
		     KVMWheel module turns into PointerV WheelChange */
		  int key = e->code == REL_WHEEL ? (e->value > 0 ? 0x1F0 : 0x1F1)
						 : (e->value > 0 ? 0x1F3 : 0x1F2);
		  for (int c = abs (e->value); c > 0 && c <= 8; c--)
		    {
		      send_report (R_KEYDOWN, key, 0, 0);
		      send_report (R_KEYUP, key, 0, 0);
		    }
		}
	    }
	  else if (e->type == EV_ABS)
	    {
	      if (e->code == ABS_X && inputs[i].absmax_x > 0)
		{
		  rel_mouse = 0;
		  int fx = (int) ((long long) e->value * fb_w / (inputs[i].absmax_x + 1));
		  mx = clampi (sw > 0 ? (int) ((long long) (fx - ox) * w / sw) : fx, 0, w - 1);
		  moved = 1;
		}
	      else if (e->code == ABS_Y && inputs[i].absmax_y > 0)
		{
		  rel_mouse = 0;
		  int fy = (int) ((long long) e->value * fb_h / (inputs[i].absmax_y + 1));
		  my = clampi (sh > 0 ? (int) ((long long) (fy - oy) * h / sh) : fy, 0, h - 1);
		  moved = 1;
		}
	    }
	}
      if (moved)
	send_report (R_MOUSE, mx, my, buttons);
      /* RISC OS's y is up, Linux's down */
      if (rel_dx || rel_dy)
	send_report (R_MOUSE_REL, (uint32_t) rel_dx, (uint32_t) -rel_dy, 0);
    }
  if (n < 0 && errno == ENODEV)
    {
      close (inputs[i].fd);
      inputs[i] = inputs[--ninputs];
    }
}

static long
now_ms (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}


/* --- protected RISC OS: ROL_CONFINE=UID:GID[:GROUP] (set by init) runs
   RISC OS as that ordinary user, in its own view of the files: /riscos
   (its discs and settings, read/write), /opt/riscos (the ROM, read only),
   /etc (read only), its own /proc, /tmp and /dev (only the devices it uses:
   the video decoder, the VideoCore, USB devices given to it),
   and the sound FIFO. It can't change Linux or see the rest of the disc. */
static int
c_bind (const char *src, const char *dst, int ro, int dir)
{
  struct stat st;
  if (stat (src, &st))
    return -1;
  if (dir || S_ISDIR (st.st_mode))
    mkdir (dst, 0755);
  else
    {
      int f = open (dst, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
      if (f >= 0)
	close (f);
    }
  if (mount (src, dst, NULL, MS_BIND | MS_REC, NULL))
    return -1;
  unsigned long fl = MS_BIND | MS_REMOUNT | MS_NOSUID;
  if (ro)
    fl |= MS_RDONLY;
  if (strncmp (dst + strlen ("/run/rol-root"), "/dev", 4) != 0)
    fl |= MS_NODEV;
  mount (NULL, dst, NULL, fl, NULL);
  return 0;
}

static int
confine (void)
{
  const char *c = getenv ("ROL_CONFINE");
  if (!c || !*c)
    return 0;
  unsigned uid = 1000, gid = 1000, grp = 0;
  if (sscanf (c, "%u:%u:%u", &uid, &gid, &grp) < 2)
    return -1;
  unsetenv ("ROL_CONFINE");
  const char *R = "/run/rol-root";
  char p[256];
  if (unshare (CLONE_NEWNS))
    return -1;
  /* slave, not private: the only shared mount, /riscos/media (init), then
     passes on the USB discs rol-usbd mounts there later */
  mount (NULL, "/", NULL, MS_REC | MS_SLAVE, NULL);
  mkdir (R, 0755);
  if (mount ("tmpfs", R, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755"))
    return -1;
  const char *dirs[] = { "riscos", "opt", "opt/riscos", "etc", "dev", "proc", "tmp", "run", NULL };
  for (int i = 0; dirs[i]; i++)
    {
      snprintf (p, sizeof p, "%s/%s", R, dirs[i]);
      mkdir (p, 0755);
    }
  snprintf (p, sizeof p, "%s/riscos", R);
  if (c_bind ("/riscos", p, 0, 1))
    return -1;
  snprintf (p, sizeof p, "%s/opt/riscos", R);
  if (c_bind ("/opt/riscos", p, 1, 1))
    return -1;
  snprintf (p, sizeof p, "%s/etc", R);
  c_bind ("/etc", p, 1, 1);
  snprintf (p, sizeof p, "%s/tmp", R);
  mount ("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
  snprintf (p, sizeof p, "%s/run", R);
  mount ("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755");
  snprintf (p, sizeof p, "%s/run/riscos-sound", R);
  c_bind ("/run/riscos-sound", p, 0, 0);
  snprintf (p, sizeof p, "%s/proc", R);
  if (mount ("proc", p, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, "hidepid=2"))
    mount ("proc", p, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
  /* /dev: only what RISC OS uses (the video decoder, the VideoCore, a Pi 3
     or 4's GPIO registers) */
  snprintf (p, sizeof p, "%s/dev", R);
  mount ("tmpfs", p, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=755");
  const char *devs[] = { "null", "zero", "full", "random", "urandom", "vchiq", "vcio", "gpiomem", NULL };
  for (int i = 0; devs[i]; i++)
    {
      char s2[64];
      snprintf (s2, sizeof s2, "/dev/%s", devs[i]);
      snprintf (p, sizeof p, "%s/dev/%s", R, devs[i]);
      c_bind (s2, p, 0, 0);
    }
  DIR *d = opendir ("/dev");
  struct dirent *e;
  while (d && (e = readdir (d)))
    if (!strncmp (e->d_name, "video", 5) || !strncmp (e->d_name, "media", 5))
      {
	char s2[300];
	snprintf (s2, sizeof s2, "/dev/%s", e->d_name);
	snprintf (p, sizeof p, "%s/dev/%.40s", R, e->d_name);
	c_bind (s2, p, 0, 0);
      }
  if (d)
    closedir (d);
  /* USB devices handed to RISC OS (the phones, cameras and tablets PDFS
     talks to): the USB helper gives RISC OS's user just those nodes, so
     the rest stay root's; the directory is live, so later ones appear */
  snprintf (p, sizeof p, "%s/dev/bus", R);
  mkdir (p, 0755);
  snprintf (p, sizeof p, "%s/dev/bus/usb", R);
  c_bind ("/dev/bus/usb", p, 0, 1);
  snprintf (p, sizeof p, "%s/dev/shm", R);
  mkdir (p, 01777);
  mount ("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
  /* into it */
  if (chdir (R) || syscall (SYS_pivot_root, ".", ".") || umount2 (".", MNT_DETACH) || chdir ("/riscos"))
    return -1;
  gid_t g[1] = { (gid_t) grp };
  if (setgroups (grp ? 1 : 0, g) || setgid (gid) || setuid (uid))
    return -1;
  if (setuid (0) == 0)
    return -1;			/* (still root: refuse) */
  prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  setenv ("HOME", "/riscos", 1);
  return 0;
}

/* --- the monitor's own modes for RISC OS (-n: a real monitor) ---
   A monitor definition file for Configure, Screen (ScreenModes): the
   monitor's name from its EDID and the modes Linux's display driver lists
   for it, with their timings, at
   HardDisc4.!Boot.Resources.Configure.Monitors.Linux.Screen. The image's
   start-up mode file loads it if it's there (else Monitors.Other.UTM, for
   a window that takes any size). Written at each start, and removed when
   there's no monitor to describe, so a disc moved to another machine
   doesn't keep the last one's. */
#define MDF_DIR "/riscos/HardDisc4/!Boot/Resources/Configure/Monitors/Linux"
#define MDF_PATH MDF_DIR "/Screen,fff"

/* The monitor's name (EDID descriptor &FC), or its maker's three letters
   and product code, from the connector's EDID property; 0 if none. */
static int
edid_name (uint32_t conn_id, char *out, size_t len)
{
  struct drm_mode_get_connector c = { .connector_id = conn_id };
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || !c.count_props)
    return 0;
  uint32_t ids[64];
  uint64_t vals[64];
  if (c.count_props > 64)
    c.count_props = 64;
  c.count_modes = 0;
  c.count_encoders = 0;
  c.props_ptr = (uintptr_t) ids;
  c.prop_values_ptr = (uintptr_t) vals;
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c))
    return 0;
  for (uint32_t i = 0; i < c.count_props; i++)
    {
      struct drm_mode_get_property p = { .prop_id = ids[i] };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPROPERTY, &p) || strcmp (p.name, "EDID") || !vals[i])
	continue;
      uint8_t e[256];
      struct drm_mode_get_blob b = { .blob_id = (uint32_t) vals[i] };
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPROPBLOB, &b) || b.length < 128)
	return 0;
      if (b.length > sizeof e)
	b.length = sizeof e;
      b.data = (uintptr_t) e;
      if (ioctl (drm_fd, DRM_IOCTL_MODE_GETPROPBLOB, &b))
	return 0;
      for (int d = 54; d <= 108; d += 18)
	if (!e[d] && !e[d + 1] && !e[d + 2] && e[d + 3] == 0xFC)
	  {
	    size_t n = 0;
	    for (int k = d + 5; k < d + 18 && e[k] != 0x0A && e[k] && n + 1 < len; k++)
	      out[n++] = e[k] >= ' ' && e[k] < 127 ? e[k] : ' ';
	    while (n && out[n - 1] == ' ')
	      n--;
	    out[n] = 0;
	    if (n)
	      return 1;
	  }
      unsigned m = e[8] << 8 | e[9];
      snprintf (out, len, "%c%c%c %04X", '@' + (m >> 10 & 31), '@' + (m >> 5 & 31),
		'@' + (m & 31), e[10] | e[11] << 8);
      return 1;
    }
  return 0;
}

/* the sizes and rates written in it (write_mdf), for the start-up mode */
static unsigned mdf_modes[64][3];
static int mdf_count;

static int
mdf_has (int w, int h)
{
  for (int i = 0; i < mdf_count; i++)
    if ((int) mdf_modes[i][0] == w && (int) mdf_modes[i][1] == h)
      return 1;
  return 0;
}

static void
write_mdf (void)
{
  struct drm_mode_get_connector c = { .connector_id = drm_conn };
  struct drm_mode_modeinfo modes[64];
  if (drm_fd < 0 || !drm_conn || ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || !c.count_modes)
    {
      unlink (MDF_PATH);
      return;
    }
  if (c.count_modes > 64)
    c.count_modes = 64;
  c.modes_ptr = (uintptr_t) modes;
  c.count_props = 0;
  c.count_encoders = 0;
  if (ioctl (drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c))
    return;
  char name[48];
  if (!edid_name (drm_conn, name, sizeof name))
    snprintf (name, sizeof name, "Monitor");
  char buf[24576];
  size_t n = snprintf (buf, sizeof buf,
    "# Monitor definition file written by rol-fb (RISC OS for Linux) for the\n"
    "# monitor connected at start-up: the modes Linux's display driver lists\n"
    "# for it. Written again at each start; edits here are lost.\n\n"
    "file_format:1\nmonitor_title:%s\nDPMS_state:0\n", name);
  int written = 0;
  unsigned (*done)[3] = mdf_modes;
  /* the preferred mode first, then the rest in Linux's order */
  for (int pass = 0; pass < 2; pass++)
    for (uint32_t i = 0; i < c.count_modes && n < sizeof buf - 512; i++)
      {
	struct drm_mode_modeinfo *m = &modes[i];
	if ((!(m->type & DRM_MODE_TYPE_PREFERRED)) != pass)
	  continue;
	if (m->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN | DRM_MODE_FLAG_DBLCLK))
	  continue;
	/* up to 64MB of screen memory (RISC OS 5.31-19: 4K in 16 million
	   colours; earlier ROMs have 16MB, so they take the biggest modes
	   only in fewer colours) */
	if ((size_t) m->hdisplay * m->vdisplay * 4 > 64u << 20 || !m->clock)
	  continue;
	/* once each size and frame rate */
	int dup = 0;
	for (int j = 0; j < written && !dup; j++)
	  dup = done[j][0] == m->hdisplay && done[j][1] == m->vdisplay && done[j][2] == m->vrefresh;
	if (dup)
	  continue;
	done[written][0] = m->hdisplay;
	done[written][1] = m->vdisplay;
	done[written][2] = m->vrefresh;
	n += snprintf (buf + n, sizeof buf - n,
	  "\n# %u x %u, %u Hz%s\nstartmode\nmode_name:%u x %u\nx_res:%u\ny_res:%u\n"
	  "pixel_rate:%u\nh_timings:%u,%u,0,%u,0,%u\nv_timings:%u,%u,0,%u,0,%u\n"
	  "sync_pol:%u\nendmode\n",
	  m->hdisplay, m->vdisplay, m->vrefresh,
	  m->type & DRM_MODE_TYPE_PREFERRED ? " (the monitor's own)" : "",
	  m->hdisplay, m->vdisplay, m->hdisplay, m->vdisplay, m->clock,
	  m->hsync_end - m->hsync_start, m->htotal - m->hsync_end, m->hdisplay,
	  m->hsync_start - m->hdisplay,
	  m->vsync_end - m->vsync_start, m->vtotal - m->vsync_end, m->vdisplay,
	  m->vsync_start - m->vdisplay,
	  (m->flags & DRM_MODE_FLAG_NHSYNC ? 1 : 0) | (m->flags & DRM_MODE_FLAG_NVSYNC ? 2 : 0));
	written++;
      }
  mdf_count = written;
  if (!written)
    {
      unlink (MDF_PATH);
      return;
    }
  /* only if it changed (the SD card is written as little as can be) */
  char old[sizeof buf];
  int fd = open (MDF_PATH, O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      ssize_t r = read (fd, old, sizeof old);
      close (fd);
      if (r == (ssize_t) n && !memcmp (old, buf, n))
	return;
    }
  mkdir (MDF_DIR, 0755);
  if (chown (MDF_DIR, 1000, 1000)) { }
  fd = open (MDF_DIR "/Screen,fff.new", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  int ok = write (fd, buf, n) == (ssize_t) n;
  if (fchown (fd, 1000, 1000)) { }
  close (fd);
  if (ok && rename (MDF_DIR "/Screen,fff.new", MDF_PATH) == 0)
    fprintf (stderr, "rol-fb: monitor definition for \"%s\": %d modes\n", name, written);
  else
    unlink (MDF_DIR "/Screen,fff.new");
}

/* The start-up mode file, Choices:Boot.PreDesk.Configure.Monitor, as
   Configure, Screen writes one: a LoadModeFile line and a plain WimpMode
   line, so that Configure shows the monitor, resolution, colours and
   frame rate (it doesn't follow an alias or variables: images up to
   1.0.0018 set the mode with ROL$ScreenWidth/Height in an alias, and
   Configure showed "Unknown"). Written at each start while it is rol-fb's
   (or that older one), never once Configure has saved its own. */
#define MONITOR_CHOICE "/riscos/HardDisc4/!Boot/Choices/Boot/PreDesk/Configure/Monitor,feb"

static void
write_monitor_choice (int w, int h, unsigned hz, int own_mdf)
{
  char old[4096];
  ssize_t r = -1;
  int fd = open (MONITOR_CHOICE, O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      r = read (fd, old, sizeof old - 1);
      close (fd);
      if (r < 0)
	return;
      old[r] = 0;
      if (!strstr (old, "| Written by rol-fb") && !strstr (old, "Alias$ROLScreenMode"))
	return;			/* Configure's: leave it */
    }
  char buf[1024], f[16] = "";
  if (hz)
    snprintf (f, sizeof f, " F%u", hz);
  int n = snprintf (buf, sizeof buf,
    "| The screen mode at start. rol-fb writes this at each start: the\n"
    "| monitor (Monitors.Linux.Screen is the one connected, from Linux;\n"
    "| Monitors.Other.UTM is a window that takes any size) and the screen's\n"
    "| size. Choosing a mode in Configure, Screen and clicking Set replaces\n"
    "| this file, and rol-fb leaves that one alone (delete it to have this\n"
    "| one back).\n"
    "| Written by rol-fb\n"
    "LoadModeFile BootResources:Configure.Monitors.%s\n"
    "WimpMode X%d Y%d C16M%s\n",
    own_mdf ? "Linux.Screen" : "Other.UTM", w, h, f);
  if (r == n && !memcmp (old, buf, n))
    return;
  fd = open (MONITOR_CHOICE ".new", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  int ok = write (fd, buf, n) == n;
  if (fchown (fd, 1000, 1000)) { }
  close (fd);
  if (ok && rename (MONITOR_CHOICE ".new", MONITOR_CHOICE) == 0)
    fprintf (stderr, "rol-fb: start-up mode %dx%d%s (%s)\n", w, h, f,
	     own_mdf ? "Monitors.Linux.Screen" : "Monitors.Other.UTM");
  else
    unlink (MONITOR_CHOICE ".new");
}

int
main (int argc, char **argv)
{
  int opt;
  while ((opt = getopt (argc, argv, "+pn")) != -1)
    switch (opt)
      {
      case 'p': pcmouse = 1; break;
      case 'n': realmonitor = 1; break;
      default:
	fprintf (stderr, "usage: rol-fb [-p] [-n] -- COMMAND [ARGS...]\n");
	return 2;
      }
  if (optind >= argc)
    {
      fprintf (stderr, "usage: rol-fb [-p] [-n] -- COMMAND [ARGS...]\n");
      return 2;
    }
  if (fb_open ())
    return 1;
  if (realmonitor)
    write_mdf ();
  /* the start-up mode: the screen's size, halved until it fits the port's
     16 MB screen in 16M colours (as ROL$ScreenWidth/Height, below) */
  {
    int w = host_w ? host_w : fb_w, h = host_h ? host_h : fb_h, halved = 0;
    while ((long) w * h * 4 > 16L * 1024 * 1024)
      w /= 2, h /= 2, halved = 1;
    struct stat st;
    int own = realmonitor && stat (MDF_PATH, &st) == 0 && mdf_has (w, h);
    /* the frame rate where it's the monitor's own mode; none for UTM's
       window (any size: as the earlier file, which gave none) */
    if (w >= 320 && h >= 200)
      write_monitor_choice (w, h, own && !halved ? host_hz : 0, own);
  }
  pixels = mmap (NULL, SCREEN_MAX, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pixels == MAP_FAILED)
    {
      perror ("rol-fb: mmap");
      return 1;
    }
  /* a plain arrow until RISC OS sends its pointer */
  ptr_pal[1] = 0xFFFFFF; ptr_pal[2] = 0x000000;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x <= y && x < 11; x++)
      ptr_shape[y][x] = (x == 0 || x == y || y == 15) ? 2 : 1;
  ptr_h = 16;

  sigset_t set;
  sigemptyset (&set);
  sigaddset (&set, SIGCHLD);
  sigaddset (&set, SIGTERM);
  sigaddset (&set, SIGINT);
  sigprocmask (SIG_BLOCK, &set, NULL);
  int sfd = signalfd (-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);

  int sv[2];
  if (socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv))
    {
      perror ("rol-fb: socketpair");
      return 1;
    }
  pid_t self = getpid ();
  pid_t pid = fork ();
  if (pid == 0)
    {
      prctl (PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
      int s = fcntl (sv[1], F_DUPFD, 31);
      char env[64];
      snprintf (env, sizeof env, "%d", s);
      setenv ("RISC_OS_SocketKVM_Socket", env, 1);
      /* the screen's size, for the desktop's first mode: ROL$ScreenWidth
         and ROL$ScreenHeight in RISC OS (the image's Monitor choices file
         uses them until a mode is chosen and saved in Configure). Halved
         until it fits the port's 16 MB screen in 16M colours (shown here
         scaled up, as other modes are). */
      {
	int w = host_w ? host_w : fb_w, h = host_h ? host_h : fb_h;
	while ((long) w * h * 4 > 16L * 1024 * 1024)
	  w /= 2, h /= 2;
	if (w >= 320 && h >= 200)
	  {
	    char v[16];
	    snprintf (v, sizeof v, "%d", w);
	    setenv ("RISC_OS_ROL_ScreenWidth", v, 1);
	    snprintf (v, sizeof v, "%d", h);
	    setenv ("RISC_OS_ROL_ScreenHeight", v, 1);
	  }
      }
      sigprocmask (SIG_UNBLOCK, &set, NULL);
      if (confine ())
	{
	  perror ("rol-fb: protected RISC OS: can't set it up");
	  _exit (125);
	}
      if (getppid () == self)
	execvp (argv[optind], argv + optind);
      perror ("rol-fb: exec");
      _exit (126);
    }
  close (sv[1]);
  sock = sv[0];
  riscos_pid = pid;
  /* never wait for RISC OS to read its input: if it hangs, rol-fb must
     still see Ctrl+Break (events are dropped while it isn't reading) */
  fcntl (sock, F_SETFL, fcntl (sock, F_GETFL) | O_NONBLOCK);
  console_graphics (1);
  ov_start ();
  /* (no resize report now: the desktop starts in the mode saved in
     Configure, or the screen's size (ROL$ScreenWidth/Height, above); only
     later changes of the host's window size ask RISC OS for a new mode) */
  scan_inputs ();
  mx = fb_w / 2;
  my = fb_h / 2;

  int status = 0;
  long next_frame = now_ms (), next_scan = now_ms () + 2000;
  for (;;)
    {
      struct pollfd p[4 + MAX_INPUTS];
      p[0] = (struct pollfd) { sfd, POLLIN, 0 };
      p[1] = (struct pollfd) { sock, POLLIN, 0 };
      for (int i = 0; i < ninputs; i++)
	p[2 + i] = (struct pollfd) { inputs[i].fd, POLLIN, 0 };
      int np = 2 + ninputs;
      p[np] = (struct pollfd) { ov_listen, POLLIN, 0 };
      p[np + 1] = (struct pollfd) { ov_client, POLLIN, 0 };
      long t = now_ms ();
      int wait = next_frame > t ? (int) (next_frame - t) : 0;
      if (poll (p, np + 2, wait) < 0 && errno != EINTR)
	break;
      if (ov_listen >= 0 && (p[np].revents & POLLIN))
	ov_accept ();
      if (ov_client >= 0 && (p[np + 1].revents & (POLLIN | POLLHUP | POLLERR)))
	ov_message ();
      if (p[1].revents & POLLIN)
	read_messages ();
      if (mode_changed && width > 0 && height > 0)
	drm_match_mode (width, height);
      for (int i = ninputs - 1; i >= 0; i--)
	if (p[2 + i].revents & (POLLIN | POLLERR | POLLHUP))
	  read_input (i);
      t = now_ms ();
      if (t >= next_frame)
	{
	  redraw (0);
	  next_frame = t + 40;		/* 25 frames a second */
	}
      if (t >= next_scan)
	{
	  scan_inputs ();
	  if (drm_check_resize ())
	    send_report (R_RESIZE, host_w, host_h, 0);
	  next_scan = t + 1000;
	}
      if (p[0].revents & POLLIN)
	{
	  struct signalfd_siginfo si;
	  int done = 0;
	  while (read (sfd, &si, sizeof si) == sizeof si)
	    {
	      if (si.ssi_signo == SIGCHLD)
		{
		  int ws;
		  if (waitpid (pid, &ws, WNOHANG) == pid)
		    {
		      status = WIFEXITED (ws) ? WEXITSTATUS (ws) : 128 + WTERMSIG (ws);
		      done = 1;
		    }
		}
	      else
		{
		  kill (pid, SIGTERM);
		}
	    }
	  if (done)
	    break;
	}
    }
  console_graphics (0);
  return status;
}
