/*
 * keyboard.c
 *
 * Keyboard driver implementation.
 *
 * Documentation:
 * - https://wiki.osdev.org/PS/2_Keyboard
 * - https://www.avrfreaks.net/sites/default/files/PS2%20Keyboard.pdf
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller#Step_10:_Reset_Devices
 */

#include <kernel/keyboard.h>
#include <kernel/ps2driver.h>
#include <kernel/ps2ctrl.h>
#include <kernel/log.h>

#include <stdlib.h>

#undef LOG_MODULE
#define LOG_MODULE "keyboard"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// the default timeout value (in milliseconds)
#define KBD_TIMEOUT 200

// ----------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------

static struct ps2driver keyboard_driver; // forward declaration

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Performs the keyboard starting sequence:
 *
 * - clear the receive buffer
 * - disable scanning
 * - send a RESET AND SELF-TEST command
 * - get current scan code
 * - enable scanning
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_start(uint8_t irq_line)
{
	struct ps2driver *driver = &keyboard_driver;

	info("starting keyboard driver <%s>...", driver->name);

	driver->irq_line = irq_line;
	dbg("driver uses IRQ line %u", irq_line);

	// TODO: starting sequence

	success("keyboard driver started");

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Receives data from the IRQ handler.
 *
 * WARNING: Because of the interrupt context, this has to return as soon as
 * possible.
 */

static void keyboard_recv(uint8_t data)
{
	struct ps2driver *driver = &keyboard_driver;

	dbg("[IRQ] received data = 0x%x", data);

	if (ps2driver_recv(driver, data) == false) {
		error("failed to enqueue data (0x%x), data is lost!", data);
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct ps2driver keyboard_driver = {
	.name	= "KEYBOARD_MF2",
	.type	= PS2_DEVICE_KEYBOARD_MF2,
	.start	= &keyboard_start,
	.recv	= &keyboard_recv,
	// .send() callback is set by the PS/2 controller during drivers start
};

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
	info("keyboard driver initialization...");

	if (ps2ctrl_register_driver(&keyboard_driver) == false) {
		error("driver registration failed");
		goto fail;
	}

	success("keyboard driver initialization complete");

	return true;

fail:
	error("keyboard driver initialization failed");
	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
