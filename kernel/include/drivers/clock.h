/*
 * clock.h
 */

#ifndef DRIVERS_CLOCK_H_
#define DRIVERS_CLOCK_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define CLOCK_FREQ	100 // fire an interrupt every 10ms (100 per second)

// ----------------------------------------------------------------------------

void clock_init(uint32_t freq);
int32_t clock_gettick(void);
void clock_sleep(int32_t msec);
void clock_irq_handler(void);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* DRIVERS_CLOCK_H_ */