#
# isr_wrapper.S
#
# Taken from https://wiki.osdev.org/Interrupt_Service_Routines
#

.global isr_wrapper
.align 4

isr_wrapper:
	pushal
	cld /* C code following the sysV ABI requires DF to be clear on function entry */
	call unhandled_interrupt 
	popal
	iret
