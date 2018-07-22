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
#include <stdlib.h> // uses abort

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
	printf("ERROR: unhandled exception!\n");
	abort();
	/* no return */
}

void unhandled_interrupt(void)
{
	printf("ERROR: unhandled interrupt!\n");
	abort();
	/* no return */
}

void isr_handler(int isr_num, int error_code)
{
	printf("Interrupted #%d detected (error=%d)\n", isr_num, error_code);

	switch (isr_num)
	{
		// TODO: implement interrupts handler
		default:
			unhandled_interrupt();
			break;
	}
}

struct idt_entry idt[256];

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr32(void);

void setup_idt(void)
{
	size_t i ;
	struct idtr_reg idtr;

	for (i = 0; i < 256; ++i)
		idt[i] = empty_gate();

	// The first 32 entries are fixed by intel ia-32 architecture
	idt[0]  = trap_gate(isr0); // divide error
	idt[1]  = trap_gate(isr1); // reserved
	idt[2]  = int_gate(isr2); // nmi interrupt
	idt[3]  = trap_gate(isr3); // breakpoint
	idt[4]  = trap_gate(isr4); // overflow
	idt[5]  = trap_gate(isr5); // bound range exceeded
	idt[6]  = trap_gate(isr6); // invalid/undefined opcode (UD2 !)
	idt[7]  = trap_gate(isr7); // device not available (no math coprocessor)
	idt[8]  = trap_gate(isr8); // double fault
	idt[9]  = trap_gate(isr9); // coprocessor segment overrun (reserved)
	idt[10] = trap_gate(isr10); // invalid tss
	idt[11] = trap_gate(isr11); // segment not present
	idt[12] = trap_gate(isr12); // stack-segment fault
	idt[13] = trap_gate(isr13); // general protection
	idt[14] = trap_gate(isr14); // page fault
	idt[15] = trap_gate(isr15); // reserved
	idt[16] = trap_gate(isr16); // x87 fpu floating-point error (math fault)
	idt[17] = trap_gate(isr17); // alignment check
	idt[18] = trap_gate(isr18); // machine check
	idt[19] = trap_gate(isr19); // simd float-point exception
	idt[20] = trap_gate(isr20); // reserved
	idt[21] = trap_gate(isr21); // reserved
	idt[22] = trap_gate(isr22); // reserved
	idt[23] = trap_gate(isr23); // reserved
	idt[24] = trap_gate(isr24); // reserved
	idt[25] = trap_gate(isr25); // reserved
	idt[26] = trap_gate(isr26); // reserved
	idt[27] = trap_gate(isr27); // reserved
	idt[28] = trap_gate(isr28); // reserved
	idt[29] = trap_gate(isr29); // reserved
	idt[30] = trap_gate(isr30); // reserved
	idt[31] = trap_gate(isr31); // reserved

	// Next entries are user-defined
	//idt[32] = int_gate(unhandled_interrupt); // our first custom interrupt!
	idt[32] = int_gate(isr32); // our first custom interrupt!

	// Load the new idt
	idtr.limit = sizeof(idt) - 1;
	idtr.base = (u32) idt;

	asm volatile("lidt %0"
			: /* no output */
			: "m"(idtr)
			: "memory");
}
