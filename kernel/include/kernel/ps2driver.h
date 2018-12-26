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

enum ps2_device_type {
	PS2_DEVICE_KEYBOARD_AT_WITH_TRANSLATION,
	PS2_DEVICE_KEYBOARD_MF2,
	PS2_DEVICE_KEYBOARD_MF2_WITH_TRANSLATION,
	PS2_DEVICE_MOUSE_STD,
	PS2_DEVICE_MOUSE_WITH_SCROLL_WHEEL,
	PS2_DEVICE_MOUSE_5BUTTON,
	PS2_DEVICE_UNKNOWN,
};

// ----------------------------------------------------------------------------

#define PS2_DRIVER_NAME_LEN	64	// include the ending NULL byte
#define PS2_DRIVER_MAX_RECV	255	// in bytes

// ----------------------------------------------------------------------------

struct ps2driver {
	char name[PS2_DRIVER_NAME_LEN]; // driver name
	enum ps2_device_type type;
	uint8_t recv_queue[PS2_DRIVER_MAX_RECV]; // circular
	size_t recv_queue_size; // nb elements in the queue
	size_t recv_queue_next; // index of the first element in the queue
	bool (*start)(void); // called by PS2 controller
	void (*recv)(uint8_t data); // called from IRQ handler
};

// ----------------------------------------------------------------------------

bool ps2driver_recv(struct ps2driver *driver, uint8_t data);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_PS2DRIVER_H_ */
