/*
 * io.h
 *
 * Helpers for in/out instructions.
 */

#ifndef I386_IO_H_
#define I386_IO_H_

#include <kernel/types.h>

inline void outb(u16 port, u8 value)
{
	asm volatile("outb %0, %1" 
				: /* no output */
				: "a"(value), "dN"(port) /* input */
				);
}

inline u8 inb(u16 port)
{
	u8 res;
	asm volatile("inb %1, %0"
				: "=a"(res) /* output */
				: "dN"(port) /* input */
				);
	return res;
}

#endif /* I386_IO_H_ */
