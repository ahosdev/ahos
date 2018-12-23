/*
 * idt.c
 *
 * Interrupt Descriptor Table setup.
 *
 * Recommended readings:
 * - https://wiki.osdev.org/IDT
 * - https://wiki.osdev.org/Interrupts
 * - https://wiki.osdev.org/Interrupt_Service_Routines
 * - http://www.brokenthorn.com/Resources/OSDev15.html
 *
 * TODO
 * - use trap_gate() to handle nested interrupts
 * - find a better way to write wrapper and export them
 */

#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/clock.h>

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
 * should be unused. Set them all to zero to be sure... Well, it's not that
 * clear what "undefined" gate should be used.
 *
 * Right now, undefined interrupt will generate a GPF.
 */

#define empty_gate() \
	(struct idt_entry) { \
		.offset_lo = 0x0, \
		.segment_selector = 0x00, \
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

static void divide_error_handler(void)
{
	printf("\"Divide Error\" exception detected!\n");
	// TODO
	unhandled_exception();
}

static void invalid_opcode_handler(void)
{
	printf("\"Invalid Opcode (Undefined Opcode)\" exception detected!\n");
	// TODO
	unhandled_exception();
}

static void double_fault_handler(void)
{
	printf("\"Double Fault\" exception detected!\n");
	// TODO
	unhandled_exception();
}

static void general_protection_fault_handler(void)
{
	printf("\"General Protection Fault\" exception detected!\n");
	// TODO
	unhandled_exception();
}

static void page_fault_handler(void)
{
	printf("\"Page Fault\" exception detected!\n");
	// TODO
	unhandled_exception();
}

static void user_defined_interrupt_handler(void)
{
	printf("\"User Defined\" interruption detected!\n");
	// FIXME: remove me (unnecessary)
	unhandled_interrupt();
}

static void clock_handler(void)
{
	clock_inctick();

	irq_send_eoi(IRQ0_CLOCK - IRQ0_INT);
}

static void keyboard_handler(void)
{
	printf("key pressed!\n");
}

void isr_handler(int isr_num, int error_code)
{
	error_code = error_code; // fixe compilation warning (unused)

	switch (isr_num)
	{
		case 0: divide_error_handler(); break;
		case 6: invalid_opcode_handler(); break;
		case 8: double_fault_handler(); break;
		case 13: general_protection_fault_handler(); break;
		case 14: page_fault_handler(); break;
		case 32: clock_handler(); break;
		case 33: keyboard_handler(); break;
		case 34: user_defined_interrupt_handler(); break;
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
// user defined interrupts

// irq0-7
extern void isr32(void);
extern void isr33(void);
extern void isr34(void);
extern void isr35(void);
extern void isr36(void);
extern void isr37(void);
extern void isr38(void);
extern void isr39(void);

// irq8-15
extern void isr40(void);
extern void isr41(void);
extern void isr42(void);
extern void isr43(void);
extern void isr44(void);
extern void isr45(void);
extern void isr46(void);
extern void isr47(void);

void setup_idt(void)
{
	size_t i ;
	struct idtr_reg idtr;

	for (i = 0; i < 256; ++i)
		idt[i] = empty_gate();

	// The first 32 entries are fixed by intel ia-32 architecture
	idt[0]  = int_gate(isr0); // divide error
	idt[1]  = int_gate(isr1); // reserved
	idt[2]  = int_gate(isr2); // nmi interrupt
	idt[3]  = int_gate(isr3); // breakpoint
	idt[4]  = int_gate(isr4); // overflow
	idt[5]  = int_gate(isr5); // bound range exceeded
	idt[6]  = int_gate(isr6); // invalid/undefined opcode (UD2 !)
	idt[7]  = int_gate(isr7); // device not available (no math coprocessor)
	idt[8]  = int_gate(isr8); // double fault
	idt[9]  = int_gate(isr9); // coprocessor segment overrun (reserved)
	idt[10] = int_gate(isr10); // invalid tss
	idt[11] = int_gate(isr11); // segment not present
	idt[12] = int_gate(isr12); // stack-segment fault
	idt[13] = int_gate(isr13); // general protection
	idt[14] = int_gate(isr14); // page fault
	idt[15] = int_gate(isr15); // reserved
	idt[16] = int_gate(isr16); // x87 fpu floating-point error (math fault)
	idt[17] = int_gate(isr17); // alignment check
	idt[18] = int_gate(isr18); // machine check
	idt[19] = int_gate(isr19); // simd float-point exception
	idt[20] = int_gate(isr20); // reserved
	idt[21] = int_gate(isr21); // reserved
	idt[22] = int_gate(isr22); // reserved
	idt[23] = int_gate(isr23); // reserved
	idt[24] = int_gate(isr24); // reserved
	idt[25] = int_gate(isr25); // reserved
	idt[26] = int_gate(isr26); // reserved
	idt[27] = int_gate(isr27); // reserved
	idt[28] = int_gate(isr28); // reserved
	idt[29] = int_gate(isr29); // reserved
	idt[30] = int_gate(isr30); // reserved
	idt[31] = int_gate(isr31); // reserved

	// Next entries are user-defined

	// IRQ0-7 (master pic)
	idt[32] = int_gate(isr32);
	idt[33] = int_gate(isr33);
	idt[34] = int_gate(isr34);
	idt[35] = int_gate(isr35);
	idt[36] = int_gate(isr36);
	idt[37] = int_gate(isr37);
	idt[38] = int_gate(isr38);
	idt[39] = int_gate(isr39);

	// IRQ8-15 (slave pic)
	idt[40] = int_gate(isr40);
	idt[41] = int_gate(isr41);
	idt[42] = int_gate(isr42);
	idt[43] = int_gate(isr43);
	idt[44] = int_gate(isr44);
	idt[45] = int_gate(isr45);
	idt[46] = int_gate(isr46);
	idt[47] = int_gate(isr47);

	// Load the new idt
	idtr.limit = sizeof(idt) - 1;
	idtr.base = (u32) idt;

	asm volatile("lidt %0"
			: /* no output */
			: "m"(idtr)
			: "memory");
}
