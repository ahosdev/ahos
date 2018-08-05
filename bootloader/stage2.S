/*
	stage2.S

	Stage-2 does:

	1) enter Unreal mode
	2) enable gate A20
	3) copy kernel at 0x100000 (2nd MiB)
	4) detect memory
	5) enter Protected mode (setup basic GDT)
	6) run the kernel

	Stage-2 is remapped at 0x1000
*/

.code16
.text

	.global _start

_start:
	jmp stage2



############
### DATA ###
############

.include "bootloader/floppy_fat12_desc.S"

# Read-only
STG2_START_MSG:		.asciz "===[ Bootloader: Stage-2 starting ]===\r\n"
STG2_END_MSG:		.asciz "===[ Bootloader: Stage-2 complete ]===\r\n"
UNREAL_MODE_MSG: 	.asciz "Entering Unreal mode\r\n"
ENABLE_A20_MSG: 	.asciz "Enabling A20 gate\r\n"
DETECT_MEM_MSG:		.asciz "Detecting memory\r\n"
LOAD_KERNEL_MSG:	.asciz "Loading kernel\r\n"
ENTER_PMODE_MSG:	.asciz "Entering Protected mode\r\n"
BOOT_KERNEL_MSG:	.asciz "Booting kernel now!\r\n"
INIT_FLOPPY_MSG:	.asciz "Initializing floppy disk information\r\n"
KERNEL_FOUND_MSG:	.asciz "Kernel 'ahos.bin' found!\r\n"
KERNEL_NOT_FOUND_MSG:	.asciz "Kernel 'ahos.bin' not found!\r\n"
KERNEL_FILENAME:	.ascii "AHOS    BIN" # 8.3 name
FAT_SEG:		.hword 0x500	# segment value -> 0x5000 absolute address
DISK_BUFFER_SEG:	.hword 0x300	# segment value -> 0x3000 absolute address
KERNEL_ADDR:		.int 0x100000	# kernel location once loaded

# Unitialized data
ROOTDIR_NB_SECTORS:	.hword 0
ROOTDIR_LBA:		.hword 0
DATA_REGION_LBA:	.hword 0
KERNEL_LBA:		.hword 0


##############
### MACROS ###
##############

.macro print str
	pusha
	leaw \str, %si
	call print_str
	popa
.endm



#################
### FUNCTIONS ###
#################

print_str:
	lodsb 			# load byte address DS:SI into AL, inc SI
	orb %al, %al 		# test if it is NULL
	jz print_str_out	# exit
	movb $0x0e, %ah		# bios service code to print
	int $0x10		# video teletype output
	jmp print_str		# print next character
print_str_out:
	ret


BOOTFAILURE_MSG: 	.asciz "Boot Failure. Press any key to reboot...\r\n"
bootfailure:
	print BOOTFAILURE_MSG
	xor %ax, %ax	# function 0
	int $0x16	# bios function to wait for key
	ljmp $0xffff, $0 # jump to FFFF:0000 (reboot)


DEBUGOK_MSG:		.asciz "DEBUG OK\r\n"
debugok:
	print DEBUGOK_MSG
	call bootfailure


# The "Unreal" mode is possible because the 80286 (and newer) cpu keeps an
# internal segment descriptor cache even in real mode. However, it is possible
# to tweak it by entering the protected mode, setup a gdt that breaks the 64KiB
# limit and go back to real mode. Doing so allows to address memory with 32-bit
# offset while retaining the 16-bit instructions and segment:offset addressing
# mode. The gate A20 still needs to be enabled in order to access high memory.

# This does not touch 'cs', so the code still needs to be in the first 64KiB.

enable_unreal_mode:
	cli			# disable hardware interrupts
	push %ds 		# save real mode DS
	push %es		# save real mode ES

	lgdt unreal_gdtr	# loads the unreal GDT

	mov %cr0, %eax
	or $1, %al		# set pmode bit
	mov %eax, %cr0		# switch to pmode

	mov $0x08, %bx		# select descriptor 1
	mov %bx, %ds		# load new selector
	mov %bx, %es		# load new selector

	and $0xFE, %al		# clear pmode bit
	mov %eax, %cr0		# back to real mode

	pop %es			# get back old segment
	pop %ds			# get back old segment
	sti			# enable hardware interrupts

	ret

unreal_gdtr:
	.hword	unreal_gdt_end - unreal_gdt - 1		# size of gdt minus 1
	.long	unreal_gdt				# address of gdt

unreal_gdt:
	# null segment
	.long 	0
	.long 	0
	# flat ring-0 "data" desc
	.long	0x0000FFFF
	.long 	0x00CF9200
unreal_gdt_end:





# In the old days, some programmers exploit the memory "wrap around" tricks
# (0xffff + 1 = 0x0000). To not break those programs, Intel setup a logic gate
# to control the 21st address line (A20) which is disabled during boot. In
# order to access full 32-bit memory, this gate needs to be enabled.

# Checks the A20 gate status.
#
# Returns:
#	1 in AX if it is enabled, 0 if disabled.

check_a20_state:
	cli			# disable interrupts
	push %ds		# save DS
	push %bx		# save BX

	push %ds		# save DS
	mov $0xFFFF, %bx	# try to get bootsector signature
	mov %bx, %ds		# one 1MiB higher than 0x00007dfe
	mov $0x7e0e, %bx
	mov (%bx), %ax		# get value one 1MiB higher
	cmp $0xaa55, %ax	# check it is equal to 0xaa55
	pop %ds			# restore DS (needed for printing)
	jnz a20_enabled		# if not, a20 is enabled

	xor %bx, %bx
	mov %bx, %ds
	mov $0x7dfe, %bx
	mov (%bx), %ax		# get bootsector signature
	cmp $0xaa55, %ax	# check that it is '0xaa55'
	jnz bootfailure

	# TODO: modify data and recheck (they could be equal by chance)
	jmp bootfailure		# remove it once proper checking is implemented

A20_ENABLED_MSG: 	.asciz "A20 gate is enabled\r\n"
A20_DISABLED_MSG:	.asciz "A20 gate is disabled\r\n"
a20_enabled:
	print A20_ENABLED_MSG
	mov $0x1, %ax
	jmp check_a20_state_out
a20_disabled:
	print A20_DISABLED_MSG
	xor %ax, %ax

check_a20_state_out:
	pop %bx			# restore BX
	pop %ds			# restore DS
	sti			# enable interrupts
	ret

# Enables the A20 gate (if necessary). QEMU (SeaBIOS) seems to enable it during
# boot. Since, disabling it (or recompile SeaBIOS/Qemu) just to test the check
# function is a pain, A20 gate enabling is not implemented for now.

enable_gate_a20:
	call check_a20_state	# test if A20 is not already enabled
	or %al, %al
	jz enable_a20		# if not, enable it
	ret			# otherwise return

enable_a20:
	# TODO: no need to implement for now (QEMU)
	jmp bootfailure

enable_gate_a20_out:
	ret



# Detecting usable memory can (only) be done by using BIOS interrupts. That is,
# this needs to be done in real-mode before the kernel starts.

detect_memory:
	# TODO
	ret



# Initializes floppy disk information. It computes:
# - ROOTDIR_NB_SECTORS
# - ROOTIDR_LBA
# - DATA_REGION_LBA
#
# Arguments:
# 	None
#
# Returns:
#	None
#

init_floppy_info:
	pusha

compute_rootdir_nb_sectors:
	mov $0x20, %ax			# each rootdir entry is 32 bytes
	imul $BPB_ROOT_ENTRIES, %ax	# ax: total numbers of bytes in the rootdir
	xor %dx, %dx
	mov $BPB_BYTES_PER_SECTOR, %bx	# can't use imm. values with 'div', we are loading an addr.
	div %bx				# ax: total number of sectors in rootdir
	xor %dx, %dx
	mov %ax, ROOTDIR_NB_SECTORS	# save it

compute_rootdir_lba:
	mov $BPB_NB_FATS, %ax
	imul $BPB_SECTORS_PER_FAT, %ax
	add $BPB_RESERVED_SECTORS, %ax	# %ax holds the rootdir start sector
	mov %ax, ROOTDIR_LBA		# save it

compute_dataregion_lba:
	# compute the data region LBA (reduced by 2 because of reserved clusters)
	add ROOTDIR_NB_SECTORS, %ax	# AX holds the rootdir lba
	dec %ax
	dec %ax
	mov %ax, DATA_REGION_LBA	# save it

	popa
	ret



# Reads a sector from floppy disk. On error, it retries up to three times
# before giving up.
#
# Arguments:
# 	AX: Logical Block Address sector to read
#	ES:BX: destination buffer
#
# Returns:
#	AX: 1 on success, 0 otherwise.

read_sector:
	push %bx
	push %cx
	push %dx

	mov $0x3, %cx	# set maximum number of tries
	push %cx	# save try counter

read_sector_retry:
	pop %cx
	or %cx, %cx
	jz read_sector_error
	dec %cx
	push %cx

reset_floppy_disk:
	push %ax	# save LBA
	mov $0x0, %ah 	# reset floppy disk
	mov $0x0, %dl	# drive 0 is floppy drive
	int $0x13	# disk-access interrupt
	pop %ax		# restore LBA
	jc read_sector_retry

convert_lba_to_chs:
	push %ax	# save LBA
	push %bx	# save destination offset (ES:BX)
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
	pop %bx				# restore destination offset
	int $0x13			# disk-access interrupt
	pop %ax				# restore LBA
	jc read_sector_retry		# reading failure
	mov $1, %ax			# reading success
	pop %cx				# pop the retry counter

read_sector_out:
	pop %dx
	pop %cx
	pop %bx
	ret
read_sector_error:
	xor %ax, %ax
	jmp read_sector_out



# Find the first cluster of a file from the root directory.
#
# Arguments:
#	AX: address of the 8.3 filename (padded)
#
# Returns:
#	AX: -1 on error, otherwise the first cluster number

find_first_cluster:
	push %cx
	push %bx
	push %es
	push %si
	push %di

	mov (DISK_BUFFER_SEG), %es	# store current sector at DISK_BUFFER_SEG:0
	mov (ROOTDIR_NB_SECTORS), %cx	# read at most ROOTDIR_NB_SECTORS

find_first_cluster_next_sector:
	push %cx			# save sector counter
	push %ax			# save filename address

	# read the next rootdir sector (from 0 to ROOTDIR_NB_SECTORS - 1)
	mov %cx, %ax			# number of sectors left
	neg %ax				# read sectors forward
	add (ROOTDIR_NB_SECTORS), %ax	# next rootdir sector
	add (ROOTDIR_LBA), %ax		# next rootdir sector LBA
	xor %bx, %bx			# destination starts at offset 0
	call read_sector
	cmp %ax, 0			# did the floppy disk read fail ?
	jz bootfailure			# if so, we can not continue...

	mov $0x10, %cx			# there is at most 16 files per sector	(512 / 32)
	xor %bx, %bx			# reset source offset
	mov %bx, %di			# read from (ES:DI) 0x300:0

find_first_cluster_sector_scan_loop:	# scan every rootdir entry in one sector
	pop %si				# retrieve filename address...
	push %si			# ...and save it
	push %cx			# save rootdir entry counter
	push %di			# save start of current rootdir entry

	cld				# read forward
	repe cmpsb			# compares bytes from DS:SI with ES:DI up to CX bytes if matching
	pop %di				# restore to start of current rootdir entry
	jz find_first_cluster_found	# the file name matched

	add $0x20, %di			# try next rootdir entry (each are 32 bytes)
	pop %cx				# restore rootdir entry sector counter
	loop find_first_cluster_sector_scan_loop # loop while there is more file in the current sector

	pop %ax				# restore filename address
	pop %cx				# restore sector counter
	loop find_first_cluster_next_sector # loop while there is more sector to scan

find_first_cluster_not_found: # we did not find the file and there is no more sector to scan
	xor %ax, %ax
	not %ax
	jmp find_first_cluster_out

find_first_cluster_found:		# we find a matching filename, DI=rootdir entry address
	pop %ax				# pop rootdir entry sector counter
	pop %ax				# pop filename address
	pop %ax				# pop total sector counter
	add $0x1a, %di			# cluster index address in the rootdir entry
	push %ds			# save DS
	mov (DISK_BUFFER_SEG), %ds	# make DS point to same segment than ES (i.e. current sector)
	mov (%di), %di			# read the cluster value
	xchg %di, %ax			# store the result in AX
	pop %ds				# restore DS

find_first_cluster_out:
	pop %di
	pop %si
	pop %es
	pop %bx
	pop %cx
	ret



# Find the next cluster from the FAT.
#
# Arguments:
#	AX: the current cluster
#
# Returns:
#	AX: the next cluster or -1 on error.

find_next_cluster:
	# TODO
	ret


# Loads a file from floppy disk into "high memory". Note that the "unreal"
# mode must be enable before calling it. The FAT is expected to be loaded
# in memory at FAT_SEG:0000.
#
# Arguments:
#	AX: address of the 8.3 filename (padded)
#	EDI: destination buffer (32-bit register!)
#
# Returns:
#	AX: 1 on success, 0 otherwise.
#

.set DST_OFFSET,	0
.set CURRENT_CLUSTER,	4

load_file:
	push %es
	push %ds
	push %si
	push %edi
	push %sp
	push %bp
	push %dx
	push %cx
	sub $0x8, %sp			# make room for two dwords
	mov %sp, %bp

	mov %edi, DST_OFFSET(%bp)	# set destination offset stack value

	call find_first_cluster		# find the first cluster in the file
	cmp $0xffff, %ax		# did we found it ?
	jz load_file_error		# nope, returns error
	mov %ax, CURRENT_CLUSTER(%bp)	# set current cluster stack value

load_file_loop:
	# copy current sector into disk buffer
	mov CURRENT_CLUSTER(%bp), %ax	# load current cluster
	add (DATA_REGION_LBA), %ax	# current file sector LBA
	mov (DISK_BUFFER_SEG), %dx
	mov %dx, %es			# next cluster will land in disk buffer
	xor %bx, %bx			# reset destination offset
	call read_sector		# read the current file sector into the disk buffer
	cmp $0x0, %ax			# did it failed ?
	jz load_file_error

	# copy the disk buffer into high memory
	push %ds			# save DS
	mov (DISK_BUFFER_SEG), %dx
	mov %dx, %ds			# DS=disk buffer
	xor %si, %si			# read from the start of disk buffer
	mov %si, %es			# ES=0
	mov DST_OFFSET(%bp), %edi	# write to 0:DST_OFFSET (32-bits!)
	mov $BPB_BYTES_PER_SECTOR, %cx	# copy sector size byte (i.e. 512)
	rep movsb			# copy CX bytes from file into high memory
	pop %ds				# restore DS

	# get the next cluster number
	mov CURRENT_CLUSTER(%bp), %ax
	call find_next_cluster
	cmp $0xffff, %ax
	jz load_file_complete

	mov %ax, CURRENT_CLUSTER(%bp)	# save the next cluster
	mov DST_OFFSET(%bp), %dx	# retrieve the current destination offset
	add $BPB_BYTES_PER_SECTOR, %dx	# increase the destination offset
	mov %dx, DST_OFFSET(%bp)
	jmp load_file_loop		# load the next cluster

load_file_complete:
	mov $0x1, %ax
	jmp load_file_out
load_file_error:
	xor %ax, %ax
load_file_out:
	pop %cx
	pop %dx
	pop %bp
	pop %sp
	pop %edi
	pop %si
	pop %ds
	pop %es
	ret

# Seeks 'ahos.bin' file in floppy disk and load it at 0x100000 (2nd MiB).

load_kernel:
	# TODO
	ret



# Enters Protected Mode and setup a basic flat memory GDT. The kernel will needs
# to re-setup it later on.

enter_pmode:
	# TODO
	ret



###############
### STAGE-2 ###
###############

stage2:
	print STG2_START_MSG

	print UNREAL_MODE_MSG
	call enable_unreal_mode

	print ENABLE_A20_MSG
	call enable_gate_a20

	print INIT_FLOPPY_MSG
	call init_floppy_info

	print LOAD_KERNEL_MSG
	call load_kernel

	print DETECT_MEM_MSG
	call detect_memory

	print ENTER_PMODE_MSG
	call enter_pmode

	print STG2_END_MSG

	print BOOT_KERNEL_MSG
	# TODO: jump into the kernel

	jmp bootfailure
