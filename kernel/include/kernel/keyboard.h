/*
 * keyboard.h
 *
 * Keyboard driver implementation.
 */

#ifndef KERNEL_KEYBOARD_H_
#define KERNEL_KEYBOARD_H_

#include <kernel/types.h>
#include <kernel/ps2ctrl.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

extern struct ps2_device keyboard_device;

// ----------------------------------------------------------------------------

enum keyboard_led_state {
	KBD_LED_OFF			= 0,
	KBD_LED_SCROLL_LOCK = 1 << 0,
	KBD_LED_NUMBER_LOCK = 1 << 1,
	KBD_LED_CAPS_LOCK	= 1 << 2,
};

bool keyboard_set_led_state(uint8_t state);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_KEYBOARD_H_ */
