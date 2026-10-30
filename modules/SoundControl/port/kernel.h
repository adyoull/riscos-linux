/* kernel.h for the port of ROOL's WindowScroll to the test bed's C module
   framework: just the types it uses. Copyright (c) 2026 Andrew Youll. MIT. */
#ifndef WS_KERNEL_H
#define WS_KERNEL_H
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
typedef struct { int r[10]; } _kernel_swi_regs;
#endif
