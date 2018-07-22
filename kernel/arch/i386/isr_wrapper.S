#
# isr_wrapper.S
#
# Inspired by:
# - https://wiki.osdev.org/Interrupt_Service_Routines
# - http://www.jamesmolloy.co.uk/tutorial_html/4.-The%20GDT%20and%20IDT.html
#

.macro isr_noerr isr_num
	.global isr\isr_num
	.align 4
	isr\isr_num:
	cli # no nested interrupts
	push $0 # dummy error code
	push $\isr_num
	jmp isr_common_stub
.endm

.macro isr_err isr_num
	.global isr\isr_num
	.align 4
	isr\isr_num:
	cli # no nested interrupts
	push \isr_num
	jmp isr_common_stub
.endm

# While entering this code the stack layout is:
# | EIP		| <--- top (low address)
# | CS		|
# | EFLAGS	| <--- ESP before transfer to handler
# | ISR_NUM    	| <--- pushed by wrappers
# | ERROR_CODE 	| <--- push by cpu *OR* wrapper
# | ...unk... 	|

isr_common_stub:
	pushal

	# Retrieve isr number and error code from stack and repush them.
	# TODO: remove this when we will catch all the stack in isr_handler()
	# 	arguments.
	mov 0x20(%esp), %eax # isr num
	mov 0x24(%esp), %ebx # error code
	push %ebx
	push %eax

	cld /* C code following the sysV ABI requires DF to be clear on function entry */
	call isr_handler

	add $8, %esp
	popal
	add $8, %esp # clean the pushed error code push isr number

	# re-enable interrupts and continue execution prior interrupt
	sti
	iret

# First 32 interrupts are fixed by intel
isr_noerr 0
isr_noerr 1
isr_noerr 2
isr_noerr 3
isr_noerr 4
isr_noerr 5
isr_noerr 6
isr_noerr 7
isr_err 8
isr_noerr 9
isr_err 10
isr_err 11
isr_err 12
isr_err 13
isr_err 14
isr_noerr 15
isr_noerr 16
isr_err 17
isr_noerr 18
isr_noerr 19
isr_noerr 20
isr_noerr 21
isr_noerr 22
isr_noerr 23
isr_noerr 24
isr_noerr 25
isr_noerr 26
isr_noerr 27
isr_noerr 28
isr_noerr 29
isr_noerr 30
isr_noerr 31

# User-defined interrupts starts here
isr_noerr 32
