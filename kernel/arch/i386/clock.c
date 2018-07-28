/*
 * clock.c
 *
 * Programs the Programmable Interval Timer (PIT) 8254 controller.
 * 
 * Recommended readings:
 * - https://wiki.osdev.org/Programmable_Interval_Timer
 * - http://www.scs.stanford.edu/10wi-cs140/pintos/specs/8254.pdf
 */

#include <kernel/types.h>
#include <kernel/clock.h>
#include "io.h"


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

volatile u32 clock_tick; // keep it volatile, otherwise GCC does strange optimizations

/*
 * Initialize the COUNT clock value for channel 0 (irq 0).
 *
 * The caller must disable IRQ before calling this.
 */

void clock_init(u16 freq)
{
	u16 clock_divider;

	if (freq > INTERNAL_FREQ_HZ)
		freq = INTERNAL_FREQ_HZ;
	else if (freq < 1)
		freq = 1;

	// select channel 0
	outb(CLOCK_CTRL, BINARY_MODE|OP_MODE2|ACCESS_MODE_LOHI|SELECT_CHAN0);

	// compute and set clock divider
	clock_divider = (u16)(INTERNAL_FREQ_HZ / freq);
	outb(CLOCK_CHANNEL0, (u8)clock_divider);
	outb(CLOCK_CHANNEL0, (u8)(clock_divider >> 8));

	clock_tick = 0;
}

// called by clock isr handler
void clock_inctick(void)
{
	clock_tick++;
}

u32 clock_gettick(void)
{
	return clock_tick;
}

/*
 * Tries to (active) sleep @msec milliseconds.
 *
 * This is not a precise timer because:
 * 1) It can't sleep less than the interrupt frequency (10ms right now)
 * 2) It can sleep more than expected (because of the interrupt frequency)
 */

void clock_sleep(u32 msec)
{
	u32 target_tick;

	target_tick = clock_gettick() + (msec / (1000 / CLOCK_FREQ));

	// active sleep
	while (clock_gettick() < target_tick)
		;
}
