/*
 * memman.c
 *
 * Memory Manager.
 *
 * The memory model is the classical "flat memory" model.
 */

#include <kernel/types.h>
#include <kernel/interrupt.h>

#include <stdio.h>

extern void _enter_pmode(); // implemented in [arch/i386/boot.S]

// computed using "tools/gdt_descriptor.c"
static u64 gdt[] = {
	0x0000000000000000, // null segment
	0x00CF9A000000FFFF, // kernel code segment
	0x00CF92000000FFFF, // kernel data segment
	0x00CFFA000000FFFF, // user code segment
	0x00CFF2000000FFFF  // user data segment
};

struct gdtr_reg
{
	u16 limit;
	u32 base;
} __attribute__ ((packed));

static void setup_gdt(void)
{
	struct gdtr_reg gdtr;

	gdtr.limit = sizeof(gdt) - 1;
	gdtr.base = (u32) gdt;

	asm volatile("lgdt %0"
			: /* no output */
			: "m"(gdtr)
			: "memory");
}

void memman_init(void)
{
	printf("memory initialization\n");

	// disable interrupt + NMI
	printf("disabling interrupts...");
	disable_irq();
	disable_nmi();
	printf("done\n");

	/*
	 * About the "A20 line", first please read this:
	 *
	 * 	https://wiki.osdev.org/A20_Line
	 *
	 * For now, we *ASSUME* that we are booted from GRUB which *SHOULD*
	 * enable the A20 address line for us. To support different kinds
	 * of bootloader (or stop using GRUB), this should be implemented.
	 */
	// TODO: enable the A20 line
	printf("WARN: unknown A20 line state\n");

	printf("setting up new GDT...");
	setup_gdt();
	printf("done\n");

	printf("entering protected mode...");
	_enter_pmode();
	printf("done\n");

	printf("memory initialization complete!\n");
}

