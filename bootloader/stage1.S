/*
stage1.S

Stage 1 bootloader from a 1.44mb floppy disk formated with DOS 4.0 BPB.

Reading:
- https://www.codeproject.com/Articles/664165/Writing-a-boot-loader-in-Assembly-and-C-Part
- https://wiki.osdev.org/Memory_Map_(x86)
- http://www.cs.cmu.edu/~410-s07/p4/p4-boot.pdf
- http://www.brokenthorn.com/Resources/OSDev6.html
- http://www.independent-software.com/operating-system-development.html
- https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#FAT
- https://stackoverflow.com/questions/6819957/gnu-as-movl-eax-eax/6820015#6820015 (GAS addressing)
*/



/*
The real mode on 8086 allows to access (2**20=1Mb) memory with segmentation.
This is called "Low Memory". Once booted by the BIOS, we have the following
memory layout:

0x0000 - 0x03ff: (1kb)	Real Mode IVT
0x0400 - 0x04ff: (256b)	BIOS data area (BDA)
0x0500 - 0x7bff: (30kb) RAM guaranteed free for use

	0x1000 - 0x2fff: (max=8kb) Stage-2 code
	0x3000 - 0x31ff: (512b) disk buffer
	0x3200 - 0x3fff: free for use
	0x4000 - 0x4fff: (4kb) global variables
		0x4000-0x4001: Rootdir LBA (16-bits)
		0x4002-0x4003: nb sectors of rootdir
		0x4004-0x4005: data region LBA
	0x5000 - 0x61ff: (4608b) FAT table (assuming 9 sectors)
	0x6200 - 0x7bff: free for use
	 ...   - 0x7bff: real-mode stack (grows downward)

0x7c00  - 0x07dff: (512b) This is where our bootsector is loaded!
0x7e00  - 0x7ffff: (480.5kb) RAM guaranteed free for use
0x80000 - 0x9fbff: (~120Kb) RAM free for use (if it exists)
0x9fc00 - 0x9ffff: (1kb) RAM unusable (EBDA!) this can vary !
0xa0000 - 0xbffff: (128kb) video RAM (VGA display memory)
0xc0000 - 0xc7fff: (32kb) ROM, Video BIOS
0xc8000 - 0xeffff: (160kb) ROM and unusable space (Mapped hardware)
0xf0000 - 0xfffff: (64kb) ROM, Motherboard BIOS
*/



.code16
.text
	.global _start
_start:
	# Okay, let's start. The BIOS just loaded us at 0x7c00 and will start
	# executing the next instruction.

	jmp stage1	# Jump over the BPB + func + data definition



# The following macro is required to directly use values in assembly without
# dereferencing a label. For some reasons, we can't use it with ".ascii",
# nor multiple bytes expressions (e.g. 'A', B','C') since there is no variadic
# expression in GNU as. I don't know if there is a better way to do this with
# GNU as syntax without switching to Intel syntax noprefix mode (I like AT&T).

# XXX: can we actually do not use this and use label as memory location ?

.macro decl_var name type value
	.set \name, \value
	\type \value
.endm



/*
BIOS Parameter Block (BPB).

Describes the physical layout of a data storage volume. In our case
this is a 1.44mb floppy disk (3 1/2).

This should start at offset 0xb of sector 0.

See: https://en.wikipedia.org/wiki/BIOS_parameter_block
*/

	 BPB_OEM_NAME:			.ascii 	"Ah!OS  " # padded so BPB starts at 0xb
decl_var BPB_BYTES_PER_SECTOR 		.hword 	512	# sector size for floppy disk is 512
decl_var BPB_SECTORS_PER_CLUSTER	.byte  	1
decl_var BPB_RESERVED_SECTORS		.hword 	1	# One extra sector for bootloader
decl_var BPB_NB_FATS			.byte 	2 	# FAT-12 has two FATs
decl_var BPB_ROOT_ENTRIES		.hword	224	# Maximum entries in root directory
decl_var BPB_TOTAL_SECTORS		.hword	2880	# Max. sectors in a floppy disk
decl_var BPB_MEDIA			.byte	0xf0	# XXX: Is this correct ?!
decl_var BPB_SECTORS_PER_FAT		.hword 	9
decl_var BPB_SECTORS_PER_TRACK		.hword 	18
decl_var BPB_HEADS_PER_CYLINDER		.hword 	2	# two-sided
decl_var BPB_HIDDEN_SECTORS		.long 	0
decl_var BPB_TOTAL_SECTORS_BIG		.long 	0
decl_var BPB_DRIVE_NUMBER		.byte	0 	# starts of Extended Boot Record (EBPB)
decl_var BPB_UNUSED			.byte	0
decl_var BPB_EXT_BOOT_SIGNATURE		.byte 	0x29	 # MS 4.1
decl_var BPB_SERIAL_NUMBER		.long	0xa0a1a2a3
	 BPB_VOLUME_LABEL:		.byte 	'M','O','S',' ','F','L','O','P','P','Y',' '
	 BPB_FILE_SYSTEM:		.byte 	'F','A','T','1','2',' ',' ',' '



############
### DATA ###
############

LOADING_MSG:		.asciz "Starting stage-1...\r\n"
ENDING_MSG:		.asciz "Stage-1 complete!\r\n"
STG2_FOUND:		.asciz "'stage2.bin' found!\r\n"

# FAT-12 Internal name for "stage2.bin' using the 8.3 convention
STG2_FILENAME:		.ascii "STAGE2  BIN"
#STG2_FILENAME:		.ascii "MOS FLOPPY "



#################
### FUNCTIONS ###
#################

.macro print str
	leaw \str, %si
	call print_in
.endm

print_in:
	lodsb 			# load byte address DS:SI into AL, inc SI
	orb %al, %al 		# test if it is NULL
	jz print_out	# exit
	movb $0x0e, %ah		# bios service code to print
	int $0x10		# video teletype output
	jmp print_in
print_out:
	ret



# read_one_sector: convert the LBA to CHS, store the result in es:bx
# 	ES:BX: destination
#	AX: logical base address
#
# The LBA to CHS conversion formulas are:
# Sector   = (LBA mod SectorsPerTracks) + 1	# sector start at 1
# Cylinder = (LBA / SectorsPerTracks) / NumHeads
# Head	   = (LBA / SectorsPerTracks) mod NumHeads
#
# The "DIV" instruction is:
#   Unsigned divide DX:AX by r/m16, with result stored in AX = Quotient, DX = Remainder
#
# TODO: error handling + retry

read_one_sector:
	push %cx			# save sector counter
	push %ax 			# save LBA, it will be overwritten by "div" opcode
	push %bx 			# save destination location

	xor %dx, %dx
	mov $BPB_SECTORS_PER_TRACK, %bx
	div %bx				# dx= LBA mod SectorsPerTracks
	inc %dl				# sectors start at 1
	xchg %dl, %cl			# cl=start reading from sector (keep it)

	# ax: holds (LBA / SectorsPerTracks)
	xor %dx, %dx
	mov $BPB_HEADS_PER_CYLINDER, %bx
	div %bx
	xchg %al, %ch			# ch=cylinder (keep it)

	xchg %dl, %dh			# dh=head (keep it)
	movb $BPB_DRIVE_NUMBER, %dl	# dh=drive number (keep it)

	mov $0x0201, %ax		# function=2, read 1 sector

	pop %bx				# restore destination

	int $0x13			# disk-access interrupt
	jc bootfailure			# no retry for now

	pop %ax				# restore ax (LBA)
	pop %cx				# restore cx (mostly sector counter)
	ret
read_one_sector_end:



###############
### STAGE 1 ###
###############

stage1:
	# Step-0: Reset segments (do not assume anything).
	cli
	mov $0x7bfe, %ax	# Stack grows downward from 0x7bfe (hope it does not stackclash)
	mov %ax, %sp
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	sti

	print LOADING_MSG

	# Step-1: Canonicalizing CS:IP (were we loaded with 0:7c00 or 7c0:0 ?)
	ljmp $0, $canonical_eip
canonical_eip:

	# Step-2: Reset Floppy Disk
reset_floppy_disk:
	mov $0x0, %ah 	# reset floppy disk
	mov $0x0, %dl	# drive 0 is floppy drive
	int $0x13	# disk-access interrupt
	jc bootfailure

# Step-3: Find "STAGE2.BIN" in Root Directory (i.e. 'STAGE2  BIN')
rootdir_size:
	mov $0x20, %ax			# each rootdir entry is 32 bytes
	imul $BPB_ROOT_ENTRIES, %ax	# ax: total numbers of bytes in the rootdir
	#cmp $7168, %ax			# DEBUG: test
	#jz stage2_file_found		# DEBUG: ok
	#jmp bootfailure		# DEBUG: fail
	xor %dx, %dx
	mov $BPB_BYTES_PER_SECTOR, %bx	# can't use imm. values with 'div', we are loading an addr.
	div %bx				# ax: total number of sectors in rootdir
	#cmp $14, %ax
	#jz stage2_file_found
	#jmp bootfailure
	xchg %ax, %cx			# move it to %cx
	mov %cx, 0x4002			# save it

rootdir_start_sector:
	mov $BPB_NB_FATS, %ax
	#cmp $2, %ax
	#jz stage2_file_found
	#jmp bootfailure
	imul $BPB_SECTORS_PER_FAT, %ax
	#cmp $18, %ax
	#jz stage2_file_found
	#jmp bootfailure
	add $BPB_RESERVED_SECTORS, %ax	# %ax holds the rootdir start sector
	#cmp $19, %ax
	#jz stage2_file_found
	#jmp bootfailure
	mov %ax, 0x4000			# save it at 0x4000

	# read one sector and store it at 0300:0000 (one sector at a time)
	mov $0x300, %bx
	mov %bx, %es

rootdir_scan:
	# read at most 'cx' sector (e.g. 14 sector in rootdir). We assume there is at least one
	push %cx		# preserve max num of sectors in rootdir

	xor %bx, %bx
	call read_one_sector	# read the next sector and store it at 0x3000
	push % ax		# preserve current sector

	# TODO: don't hardcore the number of files per sector
	mov $0x10, %cx		# There is at most 16 files per sector (512 / 32) (sector_size / rootdir_entry_size)
	# initialize %di to the first rootdir entry in this sector
	xor %bx, %bx
	mov %bx, %di		# read from (ES:DI), ES=0x300, DI=0

rootdir_scan_loop:		# scan every rootdir entry in one sector
	push %cx		# preserve file counter per sector
	mov $11, %cx		# each files has 11 bytes (DOS 8.3 convention)
	lea STG2_FILENAME, %si	# read from (DS:SI), DS=0
	push %di		# preserve %di (start of a rootdir entry)
	cld			# read forward
	repe cmpsb		# Compares byte at address DS:SI with byte at address ES:DI up to %cx if matching
	pop %di			# reset %di to the start of a rootdir entry
	jz stage2_file_found
	add $0x20, %di		# try next rootdir entry (each are 32 bytes)
	pop %cx			# restore file per sector counter
	loop rootdir_scan_loop	# loop while %cx > 0 and decrement it
	pop %ax			# reload current sector
	inc %ax			# try next sector
	pop %cx			# reload nb sector in rootdir not read
	loop rootdir_scan	# some sector left ?
	jmp bootfailure

/*
Root Directory entry fields descriptions:

    Bytes 0-7 : DOS File name (Padded with spaces)
    Bytes 8-10 : DOS File extension (Padded with spaces)
    Bytes 11 : File attributes. This is a bit pattern:
        Bit 0 : Read Only
        Bit 1 : Hidden
        Bit 2 : System
        Bit 3 : Volume Label
        Bit 4 : This is a subdirectory
        Bit 5 : Archive
        Bit 6 : Device (Internal use)
        Bit 6 : Unused
    Bytes 12 : Unused
    Bytes 13 : Create time in ms
    Bytes 14-15 : Created time, using the following format:
        Bit 0-4 : Seconds (0-29)
        Bit 5-10 : Minutes (0-59)
        Bit 11-15 : Hours (0-23)
    Bytes 16-17 : Created year in the following format:
        Bit 0-4 : Year (0=1980; 127=2107
        Bit 5-8 : Month (1=January; 12=December)
        Bit 9-15 : Hours (0-23)
    Bytes 18-19 : Last access date (Uses same format as above)
    Bytes 20-21 : EA Index (Used in OS/2 and NT, dont worry about it)
    Bytes 22-23 : Last Modified time (See byte 14-15 for format)
    Bytes 24-25 : Last modified date (See bytes 16-17 for format)
    Bytes 26-27 : First Cluster
    Bytes 28-32 : File Size

For bootloader, we are actually only interrested by the 8.1name and the two
last fields (first cluster, file size). We might even discard the file size
and just follow the single linked list until an end cluster mark.
*/

stage2_file_found:
	print STG2_FOUND

find_data_lba:
	# compute the data region LBA (reduced by 2 because of reserved clusters)
	mov 0x4000, %ax		# deref memory at 0x4000 (rootdir lba)
	add 0x4002, %ax		# deref memory at 0x4002 (nb sectors in rootdir)
	dec %ax			# minus 2, optim: double "dec" instead of sub (2 bytes instead of 3)
	dec %ax	
	mov %ax, 0x4004		# save data region LBA at 0x4004
	#cmp $31, %ax
	#jz debug_ok

find_first_cluster:
	# retrieve the first cluster value of the 'stage2.bin' file
	# DI: offset in the rootdir
	# ES:0x0000 is a sector holding at most 16 entries, describes a rootdir part
	mov %es, %ax		# rootdir entry addr
	shl $0x4, %ax		# multiply by 16, BX=rootdir addr
	add %ax, %di		# offset in this rootdir part
	add $0x1a, %di		# DI: cluster index address of the matched rootdir entry
	mov (%di), %di		# DI: cluster index
	#cmp $3, %di
	#jz debug_ok

copy_fat:
	# copy the whole FAT table into memory
	mov $BPB_SECTORS_PER_FAT, %cx
	mov $0x500, %bx			# copy the fat at 0x5000
	mov %bx, %es
	xor %bx, %bx			# reset destination offset
	mov $BPB_RESERVED_SECTORS, %ax	# skip reserved sectors

copy_fat_loop:
	push %cx			# save sector counter
	push %ax			# save fat LBA
	push %bx			# save destination offset
	call read_one_sector
	pop %bx				# restore destination offset
	pop %ax				# restore fat lba
	pop %cx				# restore sector counter
	inc %ax				# next fat sector
	add $BPB_BYTES_PER_SECTOR, %bx	# increase destination offset
	loop copy_fat_loop

copy_stage2:
	# copy stage2 file into memory
	mov $0x100, %bx		# copy stage 2 at 0x1000
	mov %bx, %es
	xor %bx, %bx
	sub $BPB_BYTES_PER_SECTOR, %bx	# sub because bx is incremented before read_one_sector
	push %bx
	#cmp $34, %ax
	#jz debug_ok

copy_stage2_loop:
	# copy one sector
	# DI: next cluster to load
	mov 0x4004, %ax		# load data region LBA
	add %di, %ax		# add cluster index
	pop %bx			# restore previous destination offset
	add $BPB_BYTES_PER_SECTOR, %bx	# increase destination offset
	push %bx		# save destination offset
	call read_one_sector	# AX: the next LBA to read

read_next_cluster:
	// DI: current cluster in the FAT (the one we just copied)
	mov %di, %cx		# copy the current cluster
	mov %di, %dx		# copy the current cluster
	shr %dx			# divide by two
	add %dx, %cx		# sum again to get 3/2

	mov $0x5000, %bx	# FAT base address
	add %cx, %bx		# offset in the fat
	mov (%bx), %dx		# read two bytes from the fat

	test $0x1, %di		# test least significant byte
	jnz odd_cluster

even_cluster:
	and $0xfff, %dx		# mask out high bits
	jmp next_cluster
odd_cluster:
	shr $0x4, %dx		# discard low bits
next_cluster:
	mov %dx, %di		# update the current cluster index
	cmp $0xff0, %dx		# test for end of file
	jb copy_stage2_loop

	# and finally jump to stage 2 !
	print ENDING_MSG
	jmp 0x1000

BOOTFAILURE_MSG: 	.asciz "Boot Failure. Press any key to reboot...\r\n"
bootfailure:
	print BOOTFAILURE_MSG
	xor %ax, %ax	# function 0
	int $0x16	# bios function to wait for key
	ljmp $0xffff, $0 # jump to FFFF:0000 (reboot)

#DEBUG_OK_MSG: 		.asciz "DEBUG OK\r\n"
#debug_ok:
#	print DEBUG_OK_MSG
#	jmp bootfailure

#################################
### BOOTABLE DEVICE SIGNATURE ###
#################################

	# bootable device signature (end of the first 512 sector)
	. = _start + 510
	.word 0xaa55
