/*
stage1.S

Stage 1 bootloader from a 1.44mb floppy disk formated with DOS 4.0 BPB.

Reads:
- https://www.codeproject.com/Articles/664165/Writing-a-boot-loader-in-Assembly-and-C-Part
- https://wiki.osdev.org/Memory_Map_(x86)

Sequence:
<<< stage-1 >>>
- the BIOS load our first 512 bytes at 0x7c00
- load (reserved) sector 2 to N from 0x1000 to (0x1000 + N*512)
- jump to 0x1000
<<< stage-2 >>>
*/

/*
Memory Map once booted by the BIOS (only access to 64kb memory)

0x0000 - 0x03ff: (1kb)	Real Mode IVT
0x0400 - 0x04ff: (256b)	BIOS data area (BDA)
0x0500 - 0x7bff: (30kb) RAM guaranteed free for use
0x7c00 - 0x7dff: (512b) This is where our bootsector is loaded!
0x7e00 - 0x7fff: (512b) RAM guaranteed free for use

next locations = ???
*/

.code16
.text
	.global _start

_start:
	jmp stage1	# First instruction after BIOS loaded us at 0x7c00.

/*
BIOS Parameter Block (BPB).

Describes the physical layout of a data storage volume. In our case
this is a 1.44mb floppy disk (3 1/2).

This should start at offset 0xb of sector 0.

See: https://en.wikipedia.org/wiki/BIOS_parameter_block
*/

.org 0xb, 0

BPB_BYTES_PER_SECTOR: 		.hword 	512	# sector size for floppy disk is 512
BPB_SECTORS_PER_CLUSTER:	.byte  	1
BPB_RESERVED_SECTORS:		.hword 	1	# One extra sector for bootloader
BPB_NB_FATS:			.byte 	2 	# FAT-12 has two FATs
BPB_ROOT_ENTRIES:		.hword	224	# Maximum entries in root directory
BPB_TOTAL_SECTORS:		.hword	2880	# Max. sectors in a floppy disk
# double sided, 9 sectors per FAT, 80 tracks, removable
BPB_MEDIA:			.byte	0xf0	# XXX: Is this correct ?!
BPB_SECTORS_PER_FAT:		.hword 	9
BPB_SECTORS_PER_TRACK:		.hword 	18
BPB_HEADS_PER_CYLINDER:		.hword 	2	# two-sided
BPB_HIDDEN_SECTORS:		.hword 	0
				.hword 	0
BPB_TOTAL_SECTORS_BIG:		.hword 	0
				.hword 	0
BPB_DRIVE_NUMBER:		.byte	0
BPB_UNUSED:			.byte	0
BPB_EXT_BOOT_SIGNATURE:		.byte 	0x29	 # MS 4.1
BPB_SERIAL_NUMBER:		.hword	0xa0a1
				.hword 	0xa2a3
BPB_VOLUME_LABEL:		.byte 'M','O','S',' ','F','L','O','P','P','Y',' '
BPB_FILE_SYSTEM:		.byte 'F','A','T','1','2',' ',' ',' '


# Right now, we have something like 300 bytes available to store arbitrary data

STG2_START: .asciz "===[ Bootloader: Stage-2 starting ]===\r\n" # zero terminated string
STG2_END: .asciz "===[ Bootloader: Stage-2 complete ]===\r\n\n"


#####################################
### STAGE 1 (code size: 48 bytes) ###
#####################################

######
#
# Stage-1 does:
#
# 1) Reset data and extra registers to zero
# 2) Reset the floppy disk
# 3) Copy the second sector into 0x1000 segment (0x10000 physical addr)
# 4) Jump to stage 2 at 0x1000:0
#
######

stage1:
	# Reset segments (do not assume they have been zero'ed).
	cli
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	sti

reset_floppy_disk:
	mov $0x0, %ah 	# reset floppy disk
	mov $0x0, %dl	# drive 0 is floppy drive
	int $0x13	# disk-access interrupt
	jc reset_floppy_disk

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

	ljmp $0x1000, $0

	# don't go further, we should already have jumped into stage 2
	cli
	hlt

	# bootable device signature (end of the first 512 sector)
	. = _start + 510
	.word 0xaa55


###############
### STAGE-2 ###
###############

/*
Stage-2 is remapped at 0x1000:0 from stage-1. This is the second sector on
floppy disk (aka "reserved sector").
*/

# Stage-2 is remapped at 0x1000:0

stage2:
	jmp start_stage2

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

start_stage2:
	print_str STG2_START

	# TODO

	print_str STG2_END

	cli
	hlt

	. = stage2 + 512 - 8
	.ascii "END_BOOT" #mark the end of stage 2
