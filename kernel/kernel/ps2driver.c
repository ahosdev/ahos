/*
 * ps2driver.c
 *
 * Generic PS/2 driver.
 */

#include <kernel/ps2driver.h>
#include <kernel/log.h>

#include <string.h>

#undef LOG_MODULE
#define LOG_MODULE "ps2drv"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Add @data into the driver's receive queue.
 *
 * This MUST only be called in an interrupt context, hence there is no need to
 * protect resources.
 *
 * Returns true on success, or false if the receive queue is full.
 */

bool ps2driver_recv(struct ps2driver *driver, uint8_t data)
{
	size_t offset = 0;

	if (driver == NULL) {
		error("invalid parameter");
		return false;
	}

	if (driver->recv_queue_size == PS2_DRIVER_MAX_RECV) {
		error("receive queue is full");
		return false;
	}

	offset  = driver->recv_queue_next + driver->recv_queue_size;
	offset %= PS2_DRIVER_MAX_RECV;

	driver->recv_queue[offset] = data;
	driver->recv_queue_next++;
	driver->recv_queue_size++;

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
