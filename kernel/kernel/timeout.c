/*
 * timeout.c
 *
 * Basic timeout facility which uses the clock (PIT). Interrupts need to be
 * enable in order to use them, otherwise the clock ticks doesn't get
 * incremented.
 */

#include <kernel/timeout.h>
#include <kernel/log.h>

#include <drivers/clock.h>

#include <stdio.h>
#include <stdlib.h>

#define LOG_MODULE "timeout"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes a timeout with a duration of @length (milliseconds).
 */

void timeout_init(struct timeout *timeo, int32_t length)
{
	if (timeo == NULL) {
		panic("invalid argument");
	}

	if (length < 0) {
		warn("defining a negative length timeout");
	}

	timeo->length = (length * CLOCK_FREQ) / 1000;
	timeo->target = -1;
}

// ----------------------------------------------------------------------------

/*
 * Starts the timeout timer now.
 */

void timeout_start(struct timeout *timeo)
{
	if (timeo == NULL) {
		panic("invalid argument");
	}

	timeo->target = clock_gettick() + timeo->length;
}

// ----------------------------------------------------------------------------

/*
 * Returns true if the timeout has been consumed, false otherwise.
 */

bool timeout_expired(struct timeout *timeo)
{
	if (timeo == NULL) {
		panic("invalid argument");
	}

	return (clock_gettick() >= timeo->target);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
