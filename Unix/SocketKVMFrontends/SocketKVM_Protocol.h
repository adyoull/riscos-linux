/*
 * Copyright (c) 2015, Timothy Baldwin
 * Copyright (c) 2026, Andrew Youll
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

#ifndef __GCC__
#define __attribute__(X)
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CONST(x) ((x) << 24)
#else
#define CONST(x) (x)
#endif

#ifdef __cplusplus
class ule32 {
  uint32_t data;
public:
  static uint32_t swap(uint32_t v) {
    if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)  v = __builtin_bswap32(v);
    return v;
  }
  ule32& operator = (uint32_t v) {
    data = swap(v);
    return *this;
  }
  operator uint32_t () {
    return swap(data);
  }
};
#else
typedef uint32_t ule32;
#endif

struct version {
  int32_t reason;
  ule32 version;
} version;

union report {
  enum {
    ev_mouse     = CONST(0),
    ev_keydown   = CONST(1),
    ev_keyup     = CONST(2),
    ev_mode_sync = CONST(3),
    ev_resize    = CONST(4),
    ev_close     = CONST(5),
    ev_version   = CONST(6),
    ev_mouse_rel = CONST(7),   /* to SocketKVM version 3 or later: mouse.x, y = signed
                                  movement (y up, RISC OS's way), buttons unused */
  } reason;
  struct version version;
  struct {
    int32_t reason;
    ule32 x, y, buttons;
  } mouse;
  struct {
    int32_t reason;
    ule32 code;
  } key;
};

union command {
  enum {
    c_mode_change = CONST(0),
    c_set_palette = CONST(1),
    c_pointer     = CONST(2),
    c_suspend     = CONST(3),
    c_close_ctl   = CONST(4),
    c_version     = CONST(6),
    c_activescreen = CONST(7),
    c_startscreen = CONST(8),
    c_leds        = CONST(9),   /* to front ends that report version 2 or later */
    c_pointer_pos = CONST(10),  /* to front ends that report version 3 or later: where
                                   RISC OS has the pointer (its top left, in pixels from
                                   the screen's top left), each time it moves */
  } reason;
  struct version version;
  struct {
    int32_t reason;
    ule32 vidc[16];
  } mode;
  struct palette {
    int32_t reason;
    ule32 colour, index, type;
  } palette;
  struct pointer {
    int32_t reason;
    uint8_t width, height, active_x, active_y;
    uint8_t visable;
    uint8_t data[];
  } pointer;
  struct suspend {
    int32_t reason;
    ule32 delay;
  } suspend;
  struct close_ctl {
    int32_t reason;
    int32_t use_message;
  } close_ctl;
  struct activescreen {
    int32_t reason;
    ule32 address;
  } activescreen;
  struct startscreen {
    int32_t reason;
    ule32 address;
  } startscreen;
  struct leds {             /* the keyboard LEDs: bit 0 Caps Lock, 1 Num Lock, 2 Scroll Lock */
    int32_t reason;
    ule32 leds;
  } leds;
  struct pointer_pos {      /* the pointer's top left (active point taken off), pixels */
    int32_t reason;
    int32_t x, y;
  } pointer_pos;
  uint8_t pad[33 * 8];
};
