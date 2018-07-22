/*
 * idt.c
 *
 * Interrupt Descriptor Table setup.
 *
 * Recommended readings:
 * - Intel IA-32 "System Volume 3A"
 * - https://wiki.osdev.org/IDT
 * - https://wiki.osdev.org/Interrupts
 */

#include <kernel/types.h>
#include <stdio.h>

extern void isr_wrapper(void); // implemented in isr_wrapper.S

struct idtr_reg
{
	u16 limit;
	u32 base;
}__attribute__((packed));

struct idt_entry
{
	u16 offset_lo;
	u16 segment_selector;
	u16 flags;
	u16 offset_hi;
}__attribute__((packed));

/*
 * Trap and Interrupt gates are similar, and their descriptors are structurally
 * the same, they differ only in the "type" field. The difference is that for
 * interrupt gates, interrupts are automatically disabled upon entry and
 * reenabled upon IRET which restores the saved EFLAGS.
 */

#define int_gate(isr_ptr) \
	(struct idt_entry) {\
		.offset_lo = ((u32)isr_ptr & 0xffff), \
		.segment_selector = 0x08, /* ring-0 code segment */ \
		.flags = 0b1000111000000000, \
		.offset_hi = ((u32)isr_ptr >> 16) & 0xffff \
	}

#define trap_gate(isr_ptr) \
	(struct idt_entry) {\
		.offset_lo = ((u32)isr_ptr & 0xffff), \
		.segment_selector = 0x08, /* ring-0 code segment */ \
		.flags = 0b1000111100000000, \
		.offset_hi = ((u32)isr_ptr >> 16) & 0xffff \
	}

#if 0
#define task_gate(isr_ptr) \
	(struct idt_entry){\
		.offset_lo = 0x0, \
		.segment_selector = 0x0, /* FIXME: TSS */ \
		.flags = 0b1000010100000000, \
		.offset_hi = 0x0 \
	}
#endif


/*
 * In theory, only the 'P' flag (PRESENT) should be set to 0, otherfields
 * should be unused. Set them all to zero to be sure.
 */

#define empty_gate() \
	(struct idt_entry) { \
		.offset_lo = 0x0, \
		.segment_selector = 0x0, \
		.flags = 0x0, \
		.offset_hi = 0x0 \
	}

void unhandled_exception(void)
{
	// TODO
}

void unhandled_interrupt(void)
{
	// TODO
	printf("ERROR: unhandled interrupt!\n");
}

struct idt_entry idt[256];

void setup_idt(void)
{
	size_t i ;
	struct idtr_reg idtr;

	for (i = 0; i < 256; ++i)
		idt[i] = empty_gate();

	// The first 32 entries are fixed by intel ia-32 architecture
	//idt[0] = empty_gate();
	//idt[0] = trap_gate(unhandled_exception); // divide error
	idt[0] = int_gate(isr_wrapper); // divide error

	idt[1] = trap_gate(unhandled_exception); // reserved
	idt[2] = int_gate(unhandled_interrupt); // nmi interrupt
	idt[3] = trap_gate(unhandled_exception); // breakpoint
	idt[4] = trap_gate(unhandled_exception); // overflow
	idt[5] = trap_gate(unhandled_exception); // bound range exceeded
	idt[6] = trap_gate(unhandled_exception); // invalid opcode
	idt[7] = trap_gate(unhandled_exception); // device not available (no math coprocessor)
	idt[8] = trap_gate(unhandled_exception); // double fault
	idt[9] = trap_gate(unhandled_exception); // coprocessor segment overrun (reserved)
	idt[10] = trap_gate(unhandled_exception); // invalid tss
	idt[11] = trap_gate(unhandled_exception); // segment not present
	idt[12] = trap_gate(unhandled_exception); // stack-segment fault
	idt[13] = trap_gate(unhandled_exception); // general protection
	idt[14] = trap_gate(unhandled_exception); // page fault
	idt[15] = trap_gate(unhandled_exception); // reserved
	idt[16] = trap_gate(unhandled_exception); // x87 fpu floating-point error (math fault)
	idt[17] = trap_gate(unhandled_exception); // alignment check
	idt[18] = trap_gate(unhandled_exception); // machine check
	idt[19] = trap_gate(unhandled_exception); // simd float-point exception
	idt[20] = trap_gate(unhandled_exception); // reserved
	idt[21] = trap_gate(unhandled_exception); // reserved
	idt[22] = trap_gate(unhandled_exception); // reserved
	idt[23] = trap_gate(unhandled_exception); // reserved
	idt[24] = trap_gate(unhandled_exception); // reserved
	idt[25] = trap_gate(unhandled_exception); // reserved
	idt[26] = trap_gate(unhandled_exception); // reserved
	idt[27] = trap_gate(unhandled_exception); // reserved
	idt[28] = trap_gate(unhandled_exception); // reserved
	idt[29] = trap_gate(unhandled_exception); // reserved
	idt[30] = trap_gate(unhandled_exception); // reserved
	idt[31] = trap_gate(unhandled_exception); // reserved

	// Next entries are user-defined
	//idt[32] = int_gate(unhandled_interrupt); // our first custom interrupt!
	idt[32] = int_gate(isr_wrapper); // our first custom interrupt!

	// Load the new idt
	idtr.limit = sizeof(idt) - 1;
	idtr.base = (u32) idt;

	asm volatile("lidt %0"
			: /* no output */
			: "m"(idtr)
			: "memory");
}
