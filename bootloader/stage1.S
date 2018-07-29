/*
stage1.S

Stage 1 bootloader.

Reads:
- https://www.codeproject.com/Articles/664165/Writing-a-boot-loader-in-Assembly-and-C-Part
*/

.code16
.text
	.global _start

_start:
	jmp boot

	WELCOME: .asciz "Hello Bootloader!\r\n" # zero terminated string

	.macro print_str str
		leaw \str, %si
		call print_str_in
	.endm

	print_str_in:
		lodsb 			# load byte address DS:SI into AL, inc SI
		orb %al, %al 		# test if it is NULL
		jz print_str_out	# exit
		movb $0x0e, %ah		# bios service code to print
		int $0x10
		jmp print_str_in
	print_str_out:
		ret
	
boot:
	cli
	print_str WELCOME
	hlt

	# bootable device signature (end of the first 512 sector)
	. = _start + 510
	.word 0xaa55
