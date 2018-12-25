/*
 * keyboard.c
 *
 * Keyboard driver implementation.
 */

#include <kernel/keyboard.h>
#include <kernel/ps2driver.h>
#include <kernel/ps2ctrl.h>

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum keyboard_command {
	KBD_CMD_SET_LED				= 0xED,
	KBD_CMD_ECHO				= 0xEE,
	KBD_CMD_SCAN_CODE_SET		= 0xF0,
	KBD_CMD_IDENTIFY			= 0xF2,
	KBD_CMD_SET_TYPEMATIC		= 0xF3,
	KBD_CMD_ENABLE_SCANNING		= 0xF4,
	KBD_CMD_DISABLE_SCANNING	= 0xF5,
	KBD_CMD_SET_DEFAULT_PARAMS	= 0xF6,
	KBD_CMD_RESEND_LAST_BYTE	= 0xFE,
	KBD_CMD_RESET_AND_SELF_TEST = 0xFF,
	// the following commands are for scan code set 3 only
	KBD_CMD_SCS3_ALL_TYPEMATIC_AUTOREPEAT	= 0xF7,
	KBD_CMD_SCS3_ALL_MAKE_RELEASE			= 0xF8,
	KBD_CMD_SCS3_ALL_MAKE					= 0xF9,
	KBD_CMD_SCS3_ALL_TYPEMATIC_AUTOREPEAT_MAKE_RELEASE = 0xFA,
	KBD_CMD_SCS3_KEY_TYPEMATIC_AUTOREPEAT	= 0xFB,
	KBD_CMD_SCS3_KEY_MAKE_RELEASE			= 0xFC,
	KBD_CMD_SCS3_KEY_MAKE					= 0xFD,
};

// ----------------------------------------------------------------------------

// all other key not listed here are scan codes which depends on the scan code set
enum keyboard_response {
	KBD_RES_ERROR0				= 0x00, // key detection error or internal buffer overrun
	KBD_RES_SELF_TEST_PASSED	= 0xAA,
	KBD_RES_ECHO				= 0xEE,
	KBD_RES_ACK					= 0xFA,
	KBD_RES_SELF_TEST_FAILED0	= 0xFC,
	KBD_RES_SELF_TEST_FAILED1	= 0xFD,
	KBD_RES_RESEND				= 0xFE,
	KBD_RES_ERROR1				= 0xFF, // key detection error or internal buffer overrun
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct ps2driver keyboard_driver;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Send the SET LED command to the keyboard. The @state parameter is a bitmask
 * using 'keyboard_led_state' constants.
 *
 * WARNING: This is untested code (no way to test LED status with QEMU)
 */

bool keyboard_set_led_state(uint8_t state)
{
	uint8_t result;

	printf("[kbd] setting led state (0x%x)\n", state);

	if (state & ~(KBD_LED_SCROLL_LOCK|KBD_LED_NUMBER_LOCK|KBD_LED_CAPS_LOCK)) {
		printf("[kbd] ERROR: invalid LED state\n");
		return false;
	}

	if (ps2ctrl_send(0, KBD_CMD_SET_LED) == false) {
		// retry or not ?
		printf("[kbd] ERROR: failed to send command\n");
		return false;
	}
	printf("[kbd] command 'set led' sent\n");

	if (ps2ctrl_send(0, state) == false) {
		// retry or not ?
		printf("[kbd] ERROR: failed to send led status\n");
		return false;
	}
	printf("[kbd] led status sent\n");

	// we expect an 'ACK" or 'RESEND' response
	if (ps2ctrl_recv(0, &result) == false) {
		// retry or not ?
		printf("[kbd] ERROR: failed to receive command\n");
		return false;
	}
	printf("[kbd] received response (0x%x)\n", result);

	if (result == KBD_RES_ACK) {
		printf("[kbd] succeed\n");
		// succeed
		return true;
	} else if (result == KBD_RES_RESEND) {
		// FIXME: resend it
		printf("[kbd] need resend\n");
		return false; // FIXME: implement retry
	}

	printf("[kbd] unexpected response code\n");
	return false; // FIXME: retry or give up ?
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initialize the keyboard driver and register to the PS/2 controller.
 *
 * Returns true on success, false otherwise.
 */

bool keyboard_init(void)
{
	struct ps2driver *driver = &keyboard_driver;

	printf("[kbd] initializing...\n");

	if (ps2driver_init(driver, "KEYBOARD") == false) {
		goto fail;
	}

	if (ps2ctrl_register_driver(driver) == false) {
		printf("[kbd] driver registration failed\n");
		goto fail;
	}

	printf("[kbd] initialization complete\n");

	return true;

fail:
	printf("[kbd] keyboard initialization failed\n");
	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
