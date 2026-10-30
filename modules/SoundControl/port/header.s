@ SoundControl (RISC OS Open's, 1.06) built for the test bed: the module
@ header that CMHG would make from rool/cmhg/modhdr. cmodule.inc has the
@ veneers. (The Pi ROM's copy can't be loaded on the port: a ROM C module
@ calls the ROM's C library directly.)
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
	.word	cm_service - module_start
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0x50000				@ SWI chunk (SoundCtrl)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	messages - module_start		@ international help file
	.word	flags - module_start

title:	.asciz	"SoundControl"
help:	.asciz	"Sound Control\t1.06 (10 Sep 2016) test bed build"
	.align	2				@ (the kernel wants this word-aligned)
messages:
	.asciz	"Resources:$.Resources.SoundCtrl.Messages"
swi_names:
	.asciz	"SoundCtrl\0ExamineMixer\0SetMix\0GetMix\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ *MixVolume (international: the help and syntax are Messages tokens)
commands:
	.asciz	"MixVolume"
	.align	2
	.word	cm_command_0 - module_start
	.word	0x10050004			@ 4 to 5 parameters, international
	.word	tok_s - module_start
	.word	tok_h - module_start
	.word	0
tok_s:	.asciz	"SSCTMXV"
tok_h:	.asciz	"HSCTMXV"
	.align	2

	.include "cmodule.inc"
	CM_COMMAND 0

@ void *resmess_ResourcesFiles(void): the ResourceFS files (Messages)
	.global	resmess_ResourcesFiles
resmess_ResourcesFiles:
	adr	r0, resources
	mov	pc, lr
	.align	2
resources:
	.include "resources.inc"
