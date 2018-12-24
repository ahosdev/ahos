/*
 * timeout.c
 *
 * Basic timeout facility which uses the clock (PIT). Interrupts need to be
 * enable in order to use them, otherwise the clock ticks doesn't get
 * incremented.
 */

#include <kernel/timeout.h>
#include <kernel/clock.h>

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes a timeout with a duration of @length (milliseconds).
 */

void timeout_init(struct timeout *timeo, uint32_t length)
{
	if (timeo == NULL) {
		printf("[timeout] invalid argument");
		abort();
	}

	if (length == 0) {
		printf("[timeout] WARNING: defining a zero length timeout\n");
	}

	timeo->length = (length * CLOCK_FREQ) / 1000;
	timeo->target = (uint32_t) -1;
}

// ----------------------------------------------------------------------------

/*
 * Starts the timeout timer now.
 */

void timeout_start(struct timeout *timeo)
{
	if (timeo == NULL) {
		printf("[timeout] invalid argument");
		abort();
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
		printf("[timeout] invalid argument");
		abort();
	}

	return (clock_gettick() >= timeo->target);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================