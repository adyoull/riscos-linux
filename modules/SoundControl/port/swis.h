/* swis.h: _swix as Acorn's C library has it (the same register mask
   encoding), and the SWI numbers SoundControl uses.
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef SC_SWIS_H
#define SC_SWIS_H
#include "kernel.h"

#define _FLAGS     0x10
#define _IN(i)     (1U << (i))
#define _INR(i,j)  (~0U << (i) ^ ~0U << ((j) + 1))
#define _OUT(i)    ((i) != _FLAGS ? (1U << (31 - (i))) : 1U << 21)
#define _OUTR(i,j) (~0U >> (i) ^ ~0U >> ((j) + 1))
#define _RETURN(i) ((i) != _FLAGS ? (unsigned) (i) << 16 : 0xFU << 16)

_kernel_oserror *_swix (int swi, unsigned mask, ...);

#define OS_Module               0x1E
#define OS_Hardware             0x7A
#define Sound_ReadSysInfo       0x40147
#define Sound_ControllerInfo    0x4014A
#define MessageTrans_OpenFile   0x41501
#define MessageTrans_Lookup     0x41502
#define MessageTrans_CloseFile  0x41504
#define MessageTrans_ErrorLookup 0x41506
#define ResourceFS_RegisterFiles   0x41B40
#define ResourceFS_DeregisterFiles 0x41B41
#endif
