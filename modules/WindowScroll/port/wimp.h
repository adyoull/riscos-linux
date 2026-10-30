/* wimp.h: the part of RISC_OSLib's wimp.h that WindowScroll uses (the same
   names and layouts as the Wimp's own blocks), for the test bed's port.
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef WS_WIMP_H
#define WS_WIMP_H
#include "kernel.h"

typedef _kernel_oserror os_error;
typedef int wimp_w;
typedef int wimp_i;
typedef int wimp_t;
typedef int wimp_msgaction;
typedef int wimp_etype;

typedef struct { int x0, y0, x1, y1; } wimp_box;

/* window flags */
#define wimp_WPANE        0x00000020
#define wimp_WSCROLL_R1   0x00000100   /* auto-repeat Scroll_Request */
#define wimp_SCROLL_R2    0x00000200   /* debounced Scroll_Request */
#define wimp_BACK_WINDOW  0x00000800
#define wimp_WVSCR        0x10000000
#define wimp_WHSCR        0x40000000
#define wimp_WNEW         0x80000000

/* icon flags */
#define wimp_ITEXT        0x00000001
#define wimp_INDIRECT     0x00000100

typedef union {
  char text[12];
  char sprite_name[12];
  struct { char *name; char *validstring; int bufflen; } indirecttext;
  struct { char *name; void *spritearea; int nameisname; } indirectsprite;
} wimp_icondata;

typedef struct { wimp_box box; int flags; wimp_icondata data; } wimp_icon;

typedef struct {
  wimp_box box;
  int scx, scy;
  wimp_w behind;
  int flags;
  char colours[8];
  wimp_box ex;
  int titleflags;
  int workflags;
  void *spritearea;
  short minwidth, minheight;
  wimp_icondata title;
  int nicons;
} wimp_wind;

typedef struct { wimp_w w; wimp_wind info; } wimp_winfo;

typedef struct { wimp_w w; wimp_box box; int x, y; wimp_w behind; } wimp_openstr;
typedef struct { wimp_openstr o; int flags; } wimp_wstate;
typedef struct { wimp_w w; wimp_i i; int x, y; int height; int index; } wimp_caretstr;
typedef struct { int x, y; int bbits; wimp_w w; wimp_i i; } wimp_mousestr;

typedef struct { int size; wimp_t task; int my_ref; int your_ref; wimp_msgaction action; } wimp_msghdr;
typedef struct { wimp_msghdr hdr; union { char chars[236]; int words[59]; } data; } wimp_msgstr;

typedef union {
  wimp_openstr o;
  wimp_msgstr msg;
  char bytes[256];
} wimp_eventdata;

typedef struct { wimp_etype e; wimp_eventdata data; } wimp_eventstr;

/* event codes and their Wimp_Poll mask bits */
#define wimp_ENULL         0
#define wimp_EREDRAW       1
#define wimp_EOPEN         2
#define wimp_ESCROLL       10
#define wimp_ESEND         17
#define wimp_ESENDWANTACK  18
#define wimp_EACK          19
#define wimp_EMNULL        (1 << 0)
#define wimp_EMREDRAW      (1 << 1)
#define wimp_EMPTRLEAVE    (1 << 4)
#define wimp_EMPTRENTER    (1 << 5)
#define wimp_EMBUT         (1 << 6)
#define wimp_EMKEY         (1 << 8)
#define wimp_EMLOSECARET   (1 << 11)
#define wimp_EMGAINCARET   (1 << 12)
#define wimp_EMACK         (1 << 19)

#define wimp_MCLOSEDOWN    0            /* Message_Quit */

typedef int wimp_errflags;
#define wimp_EOK           1

os_error *wimp_get_caret_pos (wimp_caretstr *c);
os_error *wimp_get_point_info (wimp_mousestr *m);
os_error *wimp_get_wind_state (wimp_w w, wimp_wstate *s);
os_error *wimp_get_wind_info (wimp_winfo *i);
os_error *wimp_get_icon_info (wimp_w w, wimp_i i, wimp_icon *icon);
os_error *wimp_sendwmessage (wimp_etype code, wimp_msgstr *m, wimp_w w, wimp_i i);
os_error *wimp_open_wind (wimp_openstr *o);
os_error *wimp_taskclose (wimp_t t);
os_error *wimp_reporterror (os_error *e, wimp_errflags flags, char *name);
#endif
