/*
 * interrupt.c
 *
 * i386 arch-dependent implementations about interruptions.
 */

#include <kernel/interrupt.h>

#include "io.h"


inline void enable_irq(void)
{
	asm volatile("sti" : :);
}

inline void disable_irq(void)
{
	asm volatile("cli" : : );
}

/*
 * To turn on NMI we need to write to set the highest bit of the first CMOS
 * port (that is 0x70).
 */

inline void enable_nmi(void)
{
	outb(0x70, inb(0x70) & 0x7f);
}

/*
 * To turn off NMI we need to write to clear the highest bit of the first CMOS
 * port (that is 0x70).
 */

inline void disable_nmi(void)
{
	outb(0x70, inb(0x70)|(1<<7));
}