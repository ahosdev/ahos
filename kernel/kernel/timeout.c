/*
 * timeout.c
 *
 * Basic timeout facility which uses the clock (PIT). Interrupts need to be
 * enable in order to use them.
 */

#include <kernel/timeout.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes a timeout with a duration of @length (milliseconds).
 */

void timeout_init(struct timeout *timeo, uint32_t length)
{
	timeo = timeo;
	length = length;

	// TODO
}

// ----------------------------------------------------------------------------

/*
 * Starts the timeout timer now.
 */

void timeout_start(struct timeout *timeo)
{
	timeo = timeo;

	// TODO
}

// ----------------------------------------------------------------------------

/*
 * Returns true if the timeout has been consumed, false otherwise.
 */

bool timeout_expired(struct timeout *timeo)
{
	timeo = timeo;

	// TODO

	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

