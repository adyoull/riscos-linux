@ VCHIQ: see vchiq.c.
@ A stand-in for RISC OS's VCHIQ module, for RISC OS for Linux running natively.
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
	.word	0x59200				@ SWI chunk (as RISC OS's VCHIQ)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"VCHIQ"
help:	.asciz	"VCHIQ\t\t0.01 (30 Oct 2026) Linux's /dev/vchiq (RISC OS for Linux)"
swi_names:
	.asciz	"VCHIQ\0Initialise\0Connect\0Disconnect\0BulkQueueTransmit\0MsgDequeue\0MsgQueue\0Unused6\0Unused7\0Unused8\0ServiceClose\0ServiceOpen\0Unused11\0Unused12\0ServiceUse\0ServiceRelease\0BulkQueueReceive\0"
	.align	2
commands:
	.asciz	"VCHIQInfo"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*VCHIQInfo shows the connections and services open through Linux's /dev/vchiq.\rSyntax: *VCHIQInfo"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
	CM_COMMAND 0
