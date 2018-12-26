/*
 * ps2driver.c
 *
 * Generic PS/2 driver.
 */

#include <kernel/ps2driver.h>
#include <kernel/log.h>
#include <kernel/interrupt.h>

#include <string.h>

#undef LOG_MODULE
#define LOG_MODULE "ps2drv"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Lock driver's data from IRQ handler.
 *
 * WARNING: this method only works on single processor, single task system.
 */

static inline void ps2driver_lock(struct ps2driver *driver)
{
	if (driver == NULL) {
		error("invalid argument");
		return;
	}

	irq_set_mask(driver->irq_line);
}

// ----------------------------------------------------------------------------

/*
 * Unlock driver's data from IRQ handler.
 *
 * WARNING: this method only works on single processor, single task system.
 */

static inline void ps2driver_unlock(struct ps2driver *driver)
{
	if (driver == NULL) {
		error("invalid argument");
		return;
	}

	irq_clear_mask(driver->irq_line);
}

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
	if (driver == NULL) {
		error("invalid parameter");
		return false;
	}

	if (driver->recv_queue_size == PS2_DRIVER_MAX_RECV) {
		warn("<%s> receive queue is full", driver->name);
		return false;
	}

	driver->recv_queue[driver->recv_queue_last++] = data;
	if (driver->recv_queue_last == PS2_DRIVER_MAX_RECV) {
		driver->recv_queue_last = 0;
	}

	driver->recv_queue_size++;

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Flush the driver receive queue.
 */

void ps2driver_flush_recv_queue(struct ps2driver *driver)
{
	if (driver == NULL) {
		error("invalid argument");
	} else {
		// protect writing from race by IRQ handlers
		ps2driver_lock(driver);
		driver->recv_queue_head = 0;
		driver->recv_queue_last = 0;
		driver->recv_queue_size = 0;
		ps2driver_unlock(driver);
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
