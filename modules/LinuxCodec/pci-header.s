@ PCI: see pci.c.
@ A stand-in for RISC OS's PCI module, for RISC OS for Linux running natively.
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
	.word	0x50380				@ SWI chunk (as RISC OS's PCI)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"PCI"
help:	.asciz	"PCI\t\t0.01 (30 Oct 2026) RAMAlloc only (RISC OS for Linux)"
swi_names:
	.asciz	"PCI\0ReadID\0ReadHeader\0ReturnNumber\0EnumerateFunctions\0IORead\0IOWrite\0MemoryRead\0MemoryWrite\0ConfigurationRead\0ConfigurationWrite\0HardwareAddress\0ReadInfo\0SpecialCycle\0FindByLocation\0FindByID\0FindByClass\0RAMAlloc\0RAMFree\0LogicalAddress\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
