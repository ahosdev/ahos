# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a header as in the Multiboot Standard.
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Reserve a stack for the initial thread.
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# The kernel entry point.
.section .text
.global _start
.type _start, @function
_start:
	movl $stack_top, %esp

	# Call the global constructors.
	call _init

	# Transfer control to the main kernel.
	call kernel_main

	# Hang if kernel_main unexpectedly returns.
	cli
1:	hlt
	jmp 1b
.size _start, . - _start

.global asm_reset_segment_selectors
.type asm_reset_segment_selectors, @function
asm_reset_segment_selectors:
	# reset cs segment selector
	ljmp $0x8, $reset_cs	# 0x8 is 'cs' because each gdt entry is 64-bits
				# long, and 'cs' is the 2nd entry (8*8=64)
reset_cs:
	# reset other segment selectors
	mov $0x10, %eax		# same goes here, 0x10 is the "data" segment
				# because this is the 3rd entry in gdt
	mov %eax, %ds
	mov %eax, %fs
	mov %eax, %gs
	mov %eax, %ss
	ret
.size asm_reset_segment_selectors, . - asm_reset_segment_selectors
