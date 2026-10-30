/* modhdr.h: what CMHG would make from rool/cmhg/modhdr (header.s is the
   header itself). Copyright (c) 2026 Andrew Youll. MIT licence. */
#include "kernel.h"
#define CMD_MixVolume 0
#define SoundCtrl_00            0x50000
#define SoundCtrl_ExamineMixer  0x50000
#define SoundCtrl_SetMix        0x50001
#define SoundCtrl_GetMix        0x50002
#define Module_MessagesFile "Resources:$.Resources.SoundCtrl.Messages"
_kernel_oserror *module_Init (const char *cmd_tail, int podule_base, void *pw);
_kernel_oserror *module_Final (int fatal, int podule, void *pw);
void module_Service (int service_number, _kernel_swi_regs *r, void *pw);
_kernel_oserror *module_Commands (const char *arg_string, int argc, int cmd_no, void *pw);
_kernel_oserror *module_SWI (int swi_offset, _kernel_swi_regs *r, void *pw);
