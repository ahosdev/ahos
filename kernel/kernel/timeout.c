/*
 * timeout.c
 *
 * Basic timeout facility which uses the clock (PIT). Interrupts need to be
 * enable in order to use them.
 */

#include <kernel/timeout.h>
#include <kernel/clock.h>

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

	timeo->length = length;
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

	timeo->start = clock_gettick();
}

// ----------------------------------------------------------------------------

/*
 * Returns true if the timeout has been consumed, false otherwise.
 */

bool timeout_expired(struct timeout *timeo)
{
	uint32_t ms_diff = 0;

	if (timeo == NULL) {
		printf("[timeout] invalid argument");
		abort();
	}

	ms_diff = (clock_gettick() - timeo->start) * (1000 / CLOCK_FREQ);

	return (ms_diff >= timeo->length);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

