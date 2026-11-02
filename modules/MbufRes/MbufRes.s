@ MbufRes: RISC OS Open's MbufManager (MManagerBlob, Messages, Apache 2.0)
@ reads its messages from Resources:$.Resources.MManager.Messages, which a
@ ROM's ResourceFS has. RISC OS on Linux's ROM before 5.31-7 hasn't the
@ module, so HardDisc4.TestBed.Modules has it and this, which puts its
@ Messages in ResourceFS (again when ResourceFS restarts) for it.
@ PreDesk loads both if the ROM has no MbufManager.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE); the Messages
@ are RISC OS Open's (Apache 2.0).

	.syntax	unified
	.arm
	.text

	.set	XResourceFS_RegisterFiles, 0x61B40
	.set	XResourceFS_DeregisterFiles, 0x61B41
	.set	Service_ResourceFSStarting, 0x60

module_start:
	.word	0				@ start
	.word	init - module_start
	.word	final - module_start
	.word	service - module_start
	.word	title - module_start
	.word	help - module_start
	.word	0				@ commands
	.word	0				@ SWI chunk
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"MbufRes"
help:	.asciz	"MbufRes\t\t0.01 (01 Nov 2026) MbufManager's Messages"
	.align	2
flags:	.word	1				@ 32-bit compatible

init:
	push	{lr}
	adr	r0, files
	swi	XResourceFS_RegisterFiles
	pop	{pc}

final:
	push	{lr}
	adr	r0, files
	swi	XResourceFS_DeregisterFiles
	cmp	r0, r0				@ (V clear: no error)
	pop	{pc}

@ Service_ResourceFSStarting: R2 = routine to call with R0 = the files,
@ R3 = its workspace
service:
	teq	r1, #Service_ResourceFSStarting
	movne	pc, lr
	push	{r0-r3, lr}
	adr	r0, files
	mov	lr, pc
	mov	pc, r2
	pop	{r0-r3, pc}

	.align	2
files:
	.include "res-mbuf.inc"
