/*
	boot.s

	Code is mostly stolen from:

		https://wiki.osdev.org/Bare_Bones
*/

.set ALIGN, 1<<0
.set MEMINFO, 1<<1 // provide memory
.set FLAGS, ALIGN | MEMINFO
.set MAGIC, 0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

// declare the multiboot header
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

// setup the stack
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

// kernel entry point
.section .text
.global _start
.type _start, @function
_start:
	// we are loaded in 32-bit protected mode (no paging)

	mov $stack_top, %esp
	
	// FIXME: fixe the GDT
	// FIXME: enable paging

	call kernel_main

	// infinite loop
	cli
1:	hlt
	jmp 1b

.size _start, . - _start
	
