/*
 * clock.h
 */

#ifndef KERNEL_CLOCK_H_
#define KERNEL_CLOCK_H_

#include <kernel/types.h>

#define CLOCK_FREQ	100 // fire an interrupt every 10ms (100 per second)

void clock_init(u32 freq);
u32  clock_gettick(void);
void clock_inctick(void);
void clock_sleep(u32 msec);

#endif /* KERNEL_CLOCK_H_ */
