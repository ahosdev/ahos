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

#undef LOG_MODULE
#define LOG_MODULE "memman"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

extern void asm_reset_segment_selectors(); // implemented in [arch/i386/boot.S]

// ----------------------------------------------------------------------------

// computed using "tools/gdt_descriptor.c"
static uint64_t gdt[] = {
	0x0000000000000000, // null segment
	0x00CF9A000000FFFF, // kernel code segment
	0x00CF92000000FFFF, // kernel data segment
	0x00CFFA000000FFFF, // user code segment
	0x00CFF2000000FFFF  // user data segment
};

struct gdtr_reg
{
	uint16_t limit;
	uint32_t base;
} __attribute__ ((packed));

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void setup_gdt(void)
{
	struct gdtr_reg gdtr;

	gdtr.limit = sizeof(gdt) - 1;
	gdtr.base = (uint32_t) gdt;

	asm volatile("lgdt %0"
			: /* no output */
			: "m"(gdtr)
			: "memory");

	asm_reset_segment_selectors();
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void memman_init(void)
{
	setup_gdt();
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
