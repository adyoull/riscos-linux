@ Constants the translated elementary functions use (from RISC OS Open's
@ hdr/VFPSupport and s/Macros: FPSCR fields and IEEE 754 parameters).
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
	.equ	FPSCR_IOC, 1 << 0
	.equ	FPSCR_DZC, 1 << 1
	.equ	FPSCR_OFC, 1 << 2
	.equ	FPSCR_UFC, 1 << 3
	.equ	FPSCR_IXC, 1 << 4
	.equ	FPSCR_IDC, 1 << 7
	.equ	FPSCR_CUMULATIVE_FLAGS, FPSCR_IOC + FPSCR_DZC + FPSCR_OFC + FPSCR_UFC + FPSCR_IXC + FPSCR_IDC
	.equ	FPSCR_IOE, 1 << 8
	.equ	FPSCR_DZE, 1 << 9
	.equ	FPSCR_OFE, 1 << 10
	.equ	FPSCR_UFE, 1 << 11
	.equ	FPSCR_IXE, 1 << 12
	.equ	FPSCR_IDE, 1 << 15
	.equ	FPSCR_RMODE_SHIFT, 22
	.equ	FPSCR_RMODE_MASK, 3 << 22
	.equ	FPSCR_RMODE_NEAREST, 0 << 22
	.equ	FPSCR_RMODE_UP, 1 << 22
	.equ	FPSCR_RMODE_DOWN, 2 << 22
	.equ	FPSCR_RMODE_ZERO, 3 << 22
	.equ	FLT_MANT_DIG, 24
	.equ	FLT_EXP_BIAS, 127
	.equ	DBL_MANT_DIG, 53
	.equ	DBL_EXP_BIAS, 1023
