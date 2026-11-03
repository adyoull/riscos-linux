/* modhead.h: what CMHG would generate from rool/cmhg/modhead for C (the
   module header itself is header.s). */
/* Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#ifndef WS_MODHEAD_H
#define WS_MODHEAD_H
#include "kernel.h"
#define Module_Title "WindowScroll"
#define CMD_Desktop_WindowScroll 0
#define CMD_WimpScroll 1
_kernel_oserror *module_init (const char *cmd_tail, int podule_base, void *pw);
_kernel_oserror *module_final (int fatal, int podule, void *pw);
_kernel_oserror *module_commands (const char *arg_string, int argc, int cmd_no, void *pw);
void module_service (int service_number, _kernel_swi_regs *r, void *pw);
int eventv_handler (_kernel_swi_regs *r, void *pw);
extern void eventv_handler_entry (void);
#endif
