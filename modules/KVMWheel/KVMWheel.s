@ KVMWheel: the scroll wheel for the Linux port's SocketKVM front ends.
@ SocketKVM's protocol has no report for a wheel, so a front end (the UTM
@ image's rol-fb) sends wheel movement as key numbers that no keyboard
@ has: &1F0 up (away), &1F1 down, &1F2 left, &1F3 right. This module
@ claims KeyV, turns a press of one of them into PointerV WheelChange
@ (one step), as a mouse driver with a wheel does, and keeps them all
@ from the keyboard driver.
@ *KVMWheel Reverse turns the direction round (both ways: "natural"
@ scrolling, as on a Mac); *KVMWheel Normal puts it back. Configure's
@ Scroll wheel plug-in sets it.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.text

	.set	KeyV, 0x13
	.set	PointerV, 0x26
	.set	XOS_Claim, 0x2001F
	.set	XOS_Release, 0x20020
	.set	XOS_CallAVector, 0x20034
	.set	XOS_Write0, 0x20002
	.set	XOS_NewLine, 0x20003
	.set	KeyV_KeyUp, 1
	.set	KeyV_KeyDown, 2
	.set	PointerReason_WheelChange, 9
	.set	WheelKeyBase, 0x1F0

module_start:
	.word	0				@ start
	.word	init - module_start
	.word	final - module_start
	.word	0				@ service call handler
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0				@ SWI chunk
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"KVMWheel"
help:	.asciz	"KVMWheel\t0.02 (30 Oct 2026) scroll wheel for SocketKVM"
	.align	2
commands:
	.asciz	"KVMWheel"
	.align	2
	.word	command - module_start
	.word	0x00010000			@ 0 or 1 parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*KVMWheel Reverse turns the scroll wheel's direction round (natural scrolling, as on a Mac); *KVMWheel Normal puts it back. With no parameter it says which it is.\rSyntax: *KVMWheel [Reverse|Normal]"
	.align	2
@ 1: the direction is turned round (the module is in the RMA, writable)
reverse:
	.word	0
flags:	.word	1				@ 32-bit compatible

init:	stmfd	sp!, {lr}
	mov	r0, #KeyV
	adr	r1, keyv
	mov	r2, #0
	swi	XOS_Claim
	ldmfd	sp!, {pc}

@ *KVMWheel [Reverse|Normal]: r0 the parameter, r1 how many
command:
	stmfd	sp!, {lr}
	cmp	r1, #0
	beq	2f
	ldrb	r1, [r0]
	orr	r1, r1, #0x20			@ (lower case)
	cmp	r1, #'r'
	moveq	r1, #1
	movne	r1, #0
	str	r1, reverse
	ldmfd	sp!, {pc}
2:	ldr	r1, reverse
	cmp	r1, #0
	adreq	r0, msg_normal
	adrne	r0, msg_reverse
	swi	XOS_Write0
	swi	XOS_NewLine
	ldmfd	sp!, {pc}
msg_normal:
	.asciz	"The scroll wheel scrolls the normal way"
msg_reverse:
	.asciz	"The scroll wheel is reversed (natural scrolling)"
	.align	2

final:	stmfd	sp!, {lr}
	mov	r0, #KeyV
	adr	r1, keyv
	mov	r2, #0
	swi	XOS_Release
	mov	r0, #0				@ (gone already: fine)
	cmp	r0, #0				@ V clear
	ldmfd	sp!, {pc}

@ KeyV: r0 reason, r1 key number. Ours are claimed (by returning through
@ the address on the stack); the rest passed on (mov pc, lr).
keyv:	cmp	r0, #KeyV_KeyUp
	cmpne	r0, #KeyV_KeyDown
	movne	pc, lr
	sub	ip, r1, #WheelKeyBase
	cmp	ip, #3
	movhi	pc, lr
	cmp	r0, #KeyV_KeyUp
	ldreq	pc, [sp], #4			@ release: nothing to do
	stmfd	sp!, {r0-r3, r9, lr}
	mov	r1, #0				@ Y: negative = up/away
	mov	r3, #0				@ X
	cmp	ip, #0
	mvneq	r1, #0
	cmp	ip, #1
	moveq	r1, #1
	cmp	ip, #2
	mvneq	r3, #0
	cmp	ip, #3
	moveq	r3, #1
	ldr	r0, reverse
	cmp	r0, #0
	rsbne	r1, r1, #0
	rsbne	r3, r3, #0
	mov	r0, #PointerReason_WheelChange
	mov	r2, #0				@ extra buttons: none
	mov	r9, #PointerV
	swi	XOS_CallAVector
	ldmfd	sp!, {r0-r3, r9, lr}
	ldr	pc, [sp], #4			@ claimed
