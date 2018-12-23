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
	// Port 0x80 is normally used by POST (Power-On SelfTest) code (bios).
	asm volatile("outb %0, $0x80"
				: /* no output */
				: "a"((char)0)
		    		);
}

inline void outb(uint16_t port, uint8_t value)
{
	asm volatile("outb %0, %1" 
				: /* no output */
				: "a"(value), "dN"(port) /* input */
				);
}

inline uint8_t inb(uint16_t port)
{
	uint8_t res;
	asm volatile("inb %1, %0"
				: "=a"(res) /* output */
				: "dN"(port) /* input */
				);
	return res;
}

#endif /* I386_IO_H_ */
