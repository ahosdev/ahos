/*
stage1.S

Stage 1 bootloader.

Reads:
- https://www.codeproject.com/Articles/664165/Writing-a-boot-loader-in-Assembly-and-C-Part
- https://wiki.osdev.org/Memory_Map_(x86)

Sequence:
- the BIOS load our first 512 bytes at 0x7c00
- load sector 2 to N from 0x1000 to (0x1000 + N*512)
- jump to 0x1000
*/

/*
Memory Map once booted by the BIOS (only access to 64kb memory)

0x0000 - 0x03ff: (1kb)	Real Mode IVT
0x0400 - 0x04ff: (256b)	BIOS data area (BDA)
0x0500 - 0x7bff: (30kb) RAM guaranteed free for use
0x7c00 - 0x7dff: (512b) This BootSector!
0x7e00 - 0x7fff: (512b) RAM guaranteed free for use
next locations = ???
*/

.code16
.text
	.global _start

_start:
	jmp stage1

	STG1_START: .asciz "===[ Bootloader: Stage-1 starting ]===\r\n" # zero terminated string
	STG1_END: .asciz "===[ Bootloader: Stage-1 complete ]===\r\n"
	STG2_START: .asciz "===[ Bootloader: Stage-2 starting ]===\r\n" # zero terminated string
	STG2_END: .asciz "===[ Bootloader: Stage-2 complete ]===\r\n"
	RESET_FLOPPY: .asciz "Resetting Floppy Disk\r\n"
	LOADING_SECTORS: .asciz "Loading Sectors\r\n"
	GETMEM: .asciz "Retrieving memory size\r\n"

	.macro print_str str
		leaw \str, %si
		call print_str_in
	.endm

	print_str_in:
		lodsb 			# load byte address DS:SI into AL, inc SI
		orb %al, %al 		# test if it is NULL
		jz print_str_out	# exit
		movb $0x0e, %ah		# bios service code to print
		int $0x10		# video teletype output
		jmp print_str_in
	print_str_out:
		ret
	
stage1:
	print_str STG1_START

	print_str RESET_FLOPPY
reset_floppy_disk:
	mov $0x0, %ah 	# reset floppy disk
	mov $0x0, %dl	# drive 0 is floppy drive
	int $0x13	# disk-access interrupt
	jc reset_floppy_disk

	print_str LOADING_SECTORS
load_sectors:
	# load sectors from 1 to N and map it at 0x1000:0000
	mov $0x1000, %ax
	mov %ax, %es 	# write at 0x1000:<BX>
	xor %bx, %bx	# start writing at offset zero
	mov $0x1, %al	# number of sectors to read
	mov $0x0, %ch	# cylinder (0 to 1023 on hdd, 0 to 17 on floppy)
	mov $0x2, %cl	# start reading from sector 2 (values: 1 to 63)
	mov $0x0, %dl	# drive 0 is floppy drive
	mov $0x0, %dh	# head 0 is front side
	mov $0x2, %ah	# ah=2 (function-2; read sector into memory)
	int $0x13	# disk-access interrupt
	jc load_sectors

	print_str STG1_END
	ljmp $0x1000, $0

	# don't go further, we should already jumped into stage 2
	cli
	hlt

	# bootable device signature (end of the first 512 sector)
	. = _start + 510
	.word 0xaa55

stage2:	# stage 2 is remapped at 0x1000:0
	jmp start_stage2

	.macro print_str2 str
		leaw \str, %si
		call print_str_in2
	.endm

	print_str_in2:
		lodsb 			# load byte address DS:SI into AL, inc SI
		orb %al, %al 		# test if it is NULL
		jz print_str_out2	# exit
		movb $0x0e, %ah		# bios service code to print
		int $0x10		# video teletype output
		jmp print_str_in2
	print_str_out2:
		ret

start_stage2:
	print_str2 STG2_START

	# TODO

	print_str2 STG2_END

	cli
	hlt
	. = stage2 + 511
	.byte 0xff # mark the end of stage 2
