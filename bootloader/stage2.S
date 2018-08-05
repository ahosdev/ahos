/*
	stage2.S

	Stage-2 does:

	1) enter Unreal mode
	2) enable gate A20
	3) detect memory
	4) copy kernel at 0x100000 (2nd MiB)
	5) enter Protected mode (setup basic GDT)
	6) run the kernel

	Stage-2 is remapped at 0x1000
*/

.code16
.text

	.global _start

_start:
	jmp stage2



##############
### MACROS ###
##############

.macro print str
	leaw \str, %si
	call print_str
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

enable_gate_a20:
	# TODO
	ret



# Detecting usable memory can (only) be done by using BIOS interrupts. That is,
# this needs to be done in real-mode before the kernel starts.

detect_memory:
	# TODO
	ret



# Seeks 'ahos.bin' file in floppy disk and load it at 0x100000.

load_kernel:
	# TODO
	ret



# Enters Protected Mode and setup a basic flat memory GDT. The kernel will needs
# to re-setup it later on.

enter_pmode:
	# TODO
	ret



############
### DATA ###
############

STG2_START_MSG:		.asciz "===[ Bootloader: Stage-2 starting ]===\r\n"
STG2_END_MSG: 		.asciz "===[ Bootloader: Stage-2 complete ]===\r\n"
UNREAL_MODE_MSG: 	.asciz "Entering Unreal mode\r\n"
ENABLE_A20_MSG: 	.asciz "Enabling gate A20\r\n"
DETECT_MEM_MSG:		.asciz "Detecting memory\r\n"
LOAD_KERNEL_MSG:	.asciz "Loading kernel\r\n"
ENTER_PMODE_MSG:	.asciz "Entering Protected mode\r\n"
BOOT_KERNEL_MSG:	.asciz "Booting kernel now!\r\n"



###############
### STAGE-2 ###
###############

stage2:
	print STG2_START_MSG

	print UNREAL_MODE_MSG
	call enable_unreal_mode

	print ENABLE_A20_MSG
	call enable_gate_a20

	print DETECT_MEM_MSG
	call detect_memory

	print LOAD_KERNEL_MSG
	call load_kernel

	print ENTER_PMODE_MSG
	call enter_pmode

	print STG2_END_MSG

	print BOOT_KERNEL_MSG
	# TODO: jump into the kernel

	jmp bootfailure
