/*
	stage3.S

	Stage-3 runs in 32-bit protected mode (with interrupts disabled).

	It does the following:

	1) Parse the ELF file located at 0x100000 (i.e. the kernel)
	2) Clear BSS
	3) Jump into the kernel
*/

.code32
	jmp start_stage3



###############
### STAGE 3 ###
###############

start_stage3:
	# TODO

	cli
	hlt
