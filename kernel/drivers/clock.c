/*
 * clock.c
 *
 * Programs the Programmable Interval Timer (PIT) 8254 controller.
 *
 * Recommended readings:
 * - https://wiki.osdev.org/Programmable_Interval_Timer
 * - http://www.scs.stanford.edu/10wi-cs140/pintos/specs/8254.pdf
 */

#include <drivers/clock.h>

#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/log.h>

#include <arch/atomic.h>
#include <arch/io.h>

#define LOG_MODULE "clock"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// I/O port mapping
#define CLOCK_CHANNEL0 	0x40 // r/w (connected to IRQ0)
#define CLOCK_CHANNEL1	0x41 // r/w (unused)
#define CLOCK_CHANNEL2 	0x42 // r/w (speaker, unused)
#define CLOCK_CTRL 	0x43 // write only

#define BINARY_MODE 	(0 << 0)
#define BCD_MODE	(1 << 0)

#define OP_MODE0	(0b000 << 1) // interrupt on terminal count
#define OP_MODE1	(0b001 << 1) // hardware re-triggerable one-shot
#define OP_MODE2	(0b010 << 1) // rate generator
#define OP_MODE3	(0b011 << 1) // square wave generator
#define OP_MODE4	(0b100 << 1) // software triggered strobe
#define OP_MODE5	(0b101 << 1) // hardware triggered strobe
#define OP_MODE2BIS	(0b110 << 1) // rate generator (same as mode 2)
#define OP_MODE3BIS	(0b111 << 1) // square wave generator (same as mode 3)

#define ACCESS_MODE_LATCH	(0b00 << 4) // Latch count value command
#define ACCESS_MODE_LO		(0b01 << 4) // lobyte only
#define ACCESS_MODE_HI		(0b10 << 4) // hibyte only
#define ACCESS_MODE_LOHI	(0b11 << 4) // lobyte/hibyte

#define SELECT_CHAN0		(0b00 << 6) // channel 0
#define SELECT_CHAN1		(0b01 << 6) // channel 1
#define SELECT_CHAN2		(0b10 << 6) // channel 2
#define SELECT_READ_BACK	(0b11 << 6) // Read-back command (8254 only)

#define INTERNAL_FREQ_HZ	1193182	// in hz

// ----------------------------------------------------------------------------

atomic_t clock_tick;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initialize the COUNT clock value for channel 0 (irq 0).
 *
 * The caller must disable IRQ before calling this.
 */

void clock_init(uint32_t freq)
{
	uint16_t clock_divider;

	if (freq > INTERNAL_FREQ_HZ)
		freq = INTERNAL_FREQ_HZ;
	else if (freq < 1)
		freq = 1;

	// select channel 0
	outb(CLOCK_CTRL, BINARY_MODE|OP_MODE2|ACCESS_MODE_LOHI|SELECT_CHAN0);

	// compute and set clock divider
	clock_divider = (uint16_t)(INTERNAL_FREQ_HZ / freq);
	outb(CLOCK_CHANNEL0, (uint8_t)clock_divider);
	outb(CLOCK_CHANNEL0, (uint8_t)(clock_divider >> 8));

	atomic_write(&clock_tick, 0);

	irq_clear_mask(IRQ0_CLOCK);
}

// ----------------------------------------------------------------------------

int32_t clock_gettick(void)
{
	return atomic_read(&clock_tick);
}

// ----------------------------------------------------------------------------

/*
 * Tries to (active) sleep @msec milliseconds.
 *
 * This is not a precise timer because:
 * 1) It can't sleep less than the interrupt frequency (10ms right now)
 * 2) It can sleep more than expected (because of the interrupt frequency)
 */

void clock_sleep(int32_t msec)
{
	int32_t target_tick;

	if (msec < (1000 / CLOCK_FREQ)) {
		warn("trying to sleep less than clock frequency");
	}

	target_tick = clock_gettick() + (msec / (1000 / CLOCK_FREQ));
	if (target_tick < 0) {
		panic("tick INT overflow detected!!!");
	}

	// active sleep
	while (clock_gettick() < target_tick)
		;
}

// ----------------------------------------------------------------------------

/*
 * Clock interrupt request handler.
 */

void clock_irq_handler(void)
{
	atomic_inc(&clock_tick);

	if (atomic_read(&clock_tick) < 0) {
		panic("clock tick overflow detected!!!");
	}

	irq_send_eoi(IRQ0_CLOCK);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
