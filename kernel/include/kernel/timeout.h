/*
 * timeout.h
 *
 * Basic timeout facility which uses the clock (PIT). Interrupts need to be
 * enable in order to use them, otherwise the clock ticks doesn't get
 * incremented.
 */

#ifndef KERNEL_TIMEOUT_H_
#define KERNEL_TIMEOUT_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

struct timeout {
	uint32_t length; // in tick
	uint32_t target; // in tick
};

// ----------------------------------------------------------------------------

void timeout_init(struct timeout *timeo, uint32_t length);
void timeout_start(struct timeout *timeo);
bool timeout_expired(struct timeout *timeo);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_TIMEOUT_H_ */
