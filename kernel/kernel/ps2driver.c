/*
 * ps2driver.c
 *
 * Generic PS/2 driver.
 */

#include <kernel/ps2driver.h>
#include <kernel/log.h>

#include <string.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initialize PS2 driver structure.
 *
 * This can only fail if the @name is longer than PS2_DRIVER_NAME_LEN (NULL
 * byte included) or because of invalid parameter (NULL pointers).
 *
 * Returns true on success, false otherwise.
 */

bool ps2driver_init(struct ps2driver *driver, char *name)
{
	size_t name_len = 0;

	if (driver == NULL || name == NULL) {
		error("invalid parameter");
		return false;
	}

	name_len = strlen(name);
	if (name_len > (PS2_DRIVER_NAME_LEN - 1)) {
		error("name too long");
		return false;
	}

	memset(driver, 0, sizeof(*driver));
	memcpy(driver->name, name, name_len);

	return true;
}

// ----------------------------------------------------------------------------

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
