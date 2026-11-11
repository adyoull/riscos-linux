@ LinuxShell: the *Linux command, which runs a Linux command (through the
@ image's rol-cmd daemon) and prints its output. See linuxshell.c.
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
	.word	0				@ no SWIs
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"LinuxShell"
help:	.asciz	"LinuxShell\t0.04 (11 Nov 2026) Linux commands from RISC OS"
	.align	2
commands:
	.asciz	"Linux"
	.align	2
	.word	cm_command_0 - module_start
	.word	0x00FF0000			@ 0 to 255 parameters
	.word	0
	.word	cmd_help - module_start
	.asciz	"LinuxPassword"
	.align	2
	.word	cm_command_1 - module_start
	.word	0				@ no parameters
	.word	0
	.word	pw_help - module_start
	.asciz	"LinuxUser"
	.align	2
	.word	cm_command_2 - module_start
	.word	0x00FF0000			@ 0 to 255 parameters
	.word	0
	.word	user_help - module_start
	.word	0
user_help:
	.asciz	"*LinuxUser runs a Linux command as RISC OS's own Linux user (riscos, not root: no password) on a Linux terminal: what you type goes to it, so it can ask questions. With no command it is a shell (type exit to leave). *IXRun does this in protected RISC OS. Escape is Ctrl-C; twice quickly stops the command.\rSyntax: *LinuxUser [<command>]\r  e.g. *LinuxUser ls -l /riscos/HardDisc4"
	.align	2
pw_help:
	.asciz	"*LinuxPassword sets Linux's root password, which *Linux and !Updates ask for (it asks for the current one first).\rSyntax: *LinuxPassword"
	.align	2
cmd_help:
	.asciz	"*Linux runs a Linux command (as root, through /bin/sh) on a Linux terminal: what you type goes to it, so it can ask questions. With no command it is a root shell (type exit to leave). It asks for the root password (again after 5 minutes unused). Escape is Ctrl-C; twice quickly stops the command. In a task window the desktop carries on meanwhile.\rSyntax: *Linux [<command>]\r  e.g. *Linux apt-get update"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ int escape_state(void): 1 if Escape has been pressed (not acknowledged)
	.global	escape_state
escape_state:
	stmfd	sp!, {lr}
	swi	0x2002C				@ XOS_ReadEscapeState
	movcc	r0, #0
	movcs	r0, #1
	ldmfd	sp!, {pc}

	.include "cmodule.inc"
	CM_COMMAND 0
	CM_COMMAND 1
	CM_COMMAND 2
