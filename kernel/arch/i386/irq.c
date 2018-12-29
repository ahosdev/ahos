/*
 * irq.c
 *
 * Programming the 8259A (PIC) chipset.
 */

#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/log.h>

#include <stdio.h>
#include <stdlib.h>

#include "io.h"

#undef LOG_MODULE
#define LOG_MODULE "irq"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// Master PIC I/O ports
#define MPIC_CMD 	0x0020	// means A0=0
#define MPIC_DATA 	0x0021	// means A0=1

// Slave PIC I/O ports
#define SPIC_CMD 	0x00A0	// means A0=0
#define SPIC_DATA 	0x00A1	// means A0=1

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void irq_set_mask(uint8_t irq)
{
	uint16_t port;

	if (irq < 8)
	{
		port = MPIC_DATA;
	}
	else
	{
		port = SPIC_DATA;
		irq -= 8;
	}

	outb(port, inb(port) | (1 << irq));
	io_wait();
}

// ----------------------------------------------------------------------------

void irq_clear_mask(uint8_t irq)
{
	uint16_t port;

	if (irq < 8)
	{
		port = MPIC_DATA;
	}
	else
	{
		port = SPIC_DATA;
		irq -= 8;
	}

	outb(port, inb(port) & ~(1 << irq));
	io_wait();
}

// ----------------------------------------------------------------------------

void irq_send_eoi(uint8_t irq)
{
	if (irq > IRQ_MAX_VALUE) {
		warn("IRQ value out-of-range");
		return;
	}

	if (irq > 8)
		outb(SPIC_CMD, 0x20); // unspecified EOI
	outb(MPIC_CMD, 0x20);
}

// ----------------------------------------------------------------------------

/*
 * Remap 8259A PIC interrupts to user-defined interrupt vector.
 */

void irq_init(uint8_t master_offset, uint8_t slave_offset)
{
	uint8_t irq;

	if (master_offset < 32 || slave_offset < 32)
	{
		// interrupts reserved by intel
		abort();
	}

	// ICW1: start the initialization sequence (both master and slave)
	// 0x11=init+need icw4+cascade mode+interval 8+edge triggered+no MCS80
	outb(MPIC_CMD, 0x11);
	io_wait(); // uses io_wait() here because the PIC might be slower than cpu
	outb(SPIC_CMD, 0x11);
	io_wait();

	// ICW2: specify the interrupt vector address
	outb(MPIC_DATA, master_offset);
	io_wait();
	outb(SPIC_DATA, slave_offset);
	io_wait();

	// ICW3: we are in cascade mode (IRQ2 is connected to slave INT line)
	outb(MPIC_DATA, (1 << 2)); // IR2 has a slave
	io_wait();
	outb(SPIC_DATA, 0x2); // slave is connected to master's pin 2 (IRQ2)
	io_wait();

	// ICW4: 0b000 0 00 0 1
	outb(MPIC_DATA, 0x1); // enable 80x86 mode
	outb(SPIC_DATA, 0x1); // enable 80x86 mode

	// mask all interrupts
	for (irq = 0; irq < 16; ++irq)
		irq_set_mask(irq);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
