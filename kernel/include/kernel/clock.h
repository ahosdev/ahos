/*
 * clock.h
 */

#ifndef KERNEL_CLOCK_H_
#define KERNEL_CLOCK_H_

#include <kernel/types.h>

#define CLOCK_FREQ	100 // fire an interrupt every 10ms (100 per second)

void clock_init(uint32_t freq);
uint32_t clock_gettick(void);
void clock_inctick(void);
void clock_sleep(uint32_t msec);

#endif /* KERNEL_CLOCK_H_ */
