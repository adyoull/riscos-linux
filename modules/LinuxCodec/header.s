@ LinuxCodec: Linux's video decoders (V4L2), for RISC OS programs.
@ See linuxcodec.c.
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
	.word	commands - module_start
	.word	0x5A440				@ SWI chunk (not yet registered)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"LinuxCodec"
help:	.asciz	"LinuxCodec\t0.03 (31 Oct 2026) Linux's video decoders for RISC OS"
swi_names:
	.asciz	"LinuxCodec\0Open\0Close\0Decode\0GetFrame\0ReleaseFrame\0Info\0OverlayOpen\0OverlayShow\0OverlayHide\0"
	.align	2
commands:
	.asciz	"LinuxCodec"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*LinuxCodec lists Linux's video codec devices (V4L2 memory-to-memory) and the decoders open.\rSyntax: *LinuxCodec"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
	CM_COMMAND 0
