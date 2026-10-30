/* msgtrans.h: RISC_OSLib's MessageTrans file calls, for the test bed's
   port of WindowScroll. Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef WS_MSGTRANS_H
#define WS_MSGTRANS_H
#include "kernel.h"
typedef struct { int data[4]; } msgtrans_control_block;
_kernel_oserror *msgtrans_open_file (msgtrans_control_block *cb, const char *name, char *buffer);
_kernel_oserror *msgtrans_close_file (msgtrans_control_block *cb);
#endif
