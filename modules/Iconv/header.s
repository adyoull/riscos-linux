@ Iconv (lite) for RISC OS for Linux: module header.
@ See iconv.c for what it does and doesn't do.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.section .text.header, "ax"
	.global	module_start
module_start:
	.word	0				@ start
	.word	cm_init - module_start
	.word	cm_final - module_start
	.word	0				@ service call handler
	.word	title - module_start
	.word	help - module_start
	.word	0				@ no commands
	.word	0x57540				@ SWI chunk
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"Iconv"
help:	.asciz	"Iconv\t\t0.91 (28 Oct 2026) lite, RISC OS for Linux test bed"
swi_names:
	.asciz	"Iconv\0Open\0Iconv\0Close\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
