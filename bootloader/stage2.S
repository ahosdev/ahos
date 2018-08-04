/*
	stage2.S

	Stage-2 is in charge to setup everything for the kernel (pmode, gdt,
	a20, ...) and boot it.

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


############
### DATA ###
############

STG2_START: 	.asciz "===[ Bootloader: Stage-2 starting ]===\r\n"
STG2_END: 	.asciz "===[ Bootloader: Stage-2 complete ]===\r\n"


###############
### STAGE-2 ###
###############

stage2:
	print_str STG2_START

	# TODO

	print_str STG2_END

	cli
	hlt

	. = _start + 512 - 8
	.ascii "END_STG2" #mark the end of stage 2

	#. = _start + 1024 - 4 # just for testing cluster chaining in stage-1
	#.ascii "YYYY"
