/* swis.h: _swix and _swi as Acorn's C library has them (the same register
   mask encoding), and the SWI numbers WindowScroll uses.
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef WS_SWIS_H
#define WS_SWIS_H
#include "kernel.h"

#define _FLAGS     0x10
#define _IN(i)     (1U << (i))
#define _INR(i,j)  (~0U << (i) ^ ~0U << ((j) + 1))
#define _OUT(i)    ((i) != _FLAGS ? (1U << (31 - (i))) : 1U << 21)
#define _OUTR(i,j) (~0U >> (i) ^ ~0U >> ((j) + 1))
#define _RETURN(i) ((i) != _FLAGS ? (unsigned) (i) << 16 : 0xFU << 16)

_kernel_oserror *_swix (int swi, unsigned mask, ...);
int _swi (int swi, unsigned mask, ...);

#define OS_Write0               0x02
#define OS_NewLine              0x03
#define OS_Byte                 0x06
#define OS_GetEnv               0x10
#define OS_Exit                 0x11
#define OS_Module               0x1E
#define OS_Claim                0x1F
#define OS_Release              0x20
#define OS_ReadMonotonicTime    0x42
#define OS_ReadArgs             0x49
#define OS_Pointer              0x64
#define Wimp_Initialise         0x400C0
#define Wimp_OpenWindow         0x400C5
#define Wimp_Poll               0x400C7
#define Wimp_GetWindowState     0x400CB
#define Wimp_GetWindowInfo      0x400CC
#define Wimp_GetIconState       0x400CE
#define Wimp_GetPointerInfo     0x400CF
#define Wimp_GetCaretPosition   0x400D3
#define Wimp_CloseDown          0x400DD
#define Wimp_ReportError        0x400DF
#define Wimp_PollIdle           0x400E1
#define Wimp_SendMessage        0x400E7
#define Wimp_SlotSize           0x400EC
#define Wimp_Extend             0x400FB
#define MessageTrans_OpenFile   0x41501
#define MessageTrans_Lookup     0x41502
#define MessageTrans_CloseFile  0x41504
#define MessageTrans_ErrorLookup 0x41506
#define MessageTrans_GSLookup   0x41507
#define ResourceFS_RegisterFiles   0x41B40
#define ResourceFS_DeregisterFiles 0x41B41
#endif
