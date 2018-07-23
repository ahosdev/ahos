/*
 * io.h
 *
 * Helpers for in/out instructions.
 */

#ifndef I386_IO_H_
#define I386_IO_H_

#include <kernel/types.h>

inline void io_wait(void)
{
	// Linux thinks the port 0x80 (checkpoints) is free for use, do the same.
	asm volatile("outb %0, $0x80"
				: /* no output */
				: "a"(0)
		    		);
}

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
