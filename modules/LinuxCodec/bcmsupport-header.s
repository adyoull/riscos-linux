@ BCMSupport: see bcmsupport.c.
@ A stand-in for RISC OS's BCMSupport module, for RISC OS for Linux running natively.
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
	.word	0
	.word	0x591C0				@ SWI chunk (as RISC OS's BCMSupport)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"BCMSupport"
help:	.asciz	"BCMSupport\t0.01 (30 Oct 2026) Linux's /dev/vcio (RISC OS for Linux)"
swi_names:
	.asciz	"BCMSupport\0Unused0\0Unused1\0AllocPropertyBuffer\0FreePropertyBuffer\0SendPropertyBuffer\0SendTempPropertyBuffer\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
