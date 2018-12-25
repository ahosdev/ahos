/*
 * ps2driver.h
 *
 * Generic PS/2 driver.
 */

#ifndef KERNEL_PS2DRIVER_H_
#define KERNEL_PS2DRIVER_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define PS2_DRIVER_NAME_LEN	64	// include the ending NULL byte
#define PS2_DRIVER_MAX_RECV	255	// in bytes

// ----------------------------------------------------------------------------

struct ps2driver {
	char	name[PS2_DRIVER_NAME_LEN]; // driver name
	uint8_t recv_queue[PS2_DRIVER_MAX_RECV]; // circular
	size_t	recv_queue_size; // nb elements in the queue
	size_t	recv_queue_next; // index of the first element in the queue
};

// ----------------------------------------------------------------------------

bool ps2driver_init(struct ps2driver *driver, char *name);
bool ps2driver_recv(struct ps2driver *driver, uint8_t data);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_PS2DRIVER_H_ */
