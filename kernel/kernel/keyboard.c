/*
 * keyboard.c
 *
 * Keyboard driver implementation.
 *
 * Documentation:
 * - https://wiki.osdev.org/PS/2_Keyboard
 * - https://www.avrfreaks.net/sites/default/files/PS2%20Keyboard.pdf
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller
 *
 * FIXME: a lots of commands doesn't seems to work while we still receive an
 * ACK. For instance setting the scan code set works but disabling scan code
 * don't. I don't know if it comes from QEMU or from the code. It should be
 * tested with another emulator (e.g. Bochs). Those commands are marked as
 * UNTESTED until I can figure out what is going on... or maybe it is because
 * we didn't configure the USB controller as advised from OSDev...
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

// scan code set
enum keyboard_scs {
	KBD_SCS_1 = 0x1,
	KBD_SCS_2 = 0x2,
	KBD_SCS_3 = 0x3,
	KBD_SCS_UNKNOWN,
};

// ----------------------------------------------------------------------------

enum keyboard_typematic_repeat {
	KBD_TMT_REPEAT_SLOW		= 0b11111, // 2 Hz
	KBK_TMT_REPEAT_NORMAL	= 0b01000, // 14 Hz (?)
	KBD_TMT_REPEAT_FAST		= 0b00000, // 30 Hz
};

// ----------------------------------------------------------------------------

enum keyboard_typematic_delay {
	// in milliseconds
	KBD_TMT_DELAY_250	= 0b00,
	KBD_TMT_DELAY_500	= 0b01,
	KBD_TMT_DELAY_750	= 0b10,
	KBD_TMT_DELAY_1000	= 0b11,
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct ps2driver keyboard_driver; // forward declaration

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Sends the @data byte and receive a response to/from the keyboard.
 *
 * On success, the response is stored in @response. Otherwise, @response is
 * untouched.
 *
 * Returns true on success, false otherwise (max try, timeout).
 */

static bool keyboard_send_and_recv(uint8_t data, uint8_t *response)
{
	struct ps2driver *driver = &keyboard_driver;
	uint8_t res; // stack variable to let @response untouched on error
	size_t max_try = 3;

	dbg("sending 0x%x byte", data);

	if (driver->send == NULL) {
		error("driver cannot send data");
		return false;
	}

	if (response == NULL) {
		error("invalid argument");
		return false;
	}

retry:
	if (max_try-- == 0) {
		error("max try reached");
		return false;
	}

	if (driver->send(data, KBD_TIMEOUT) == false) {
		warn("failed to send byte");
		goto retry;
	}
	dbg("sending byte succeed");

	if (ps2driver_read(driver, &res, KBD_TIMEOUT) == false) {
		warn("failed to receive response");
		goto retry;
	}
	dbg("received 0x%x response", res);

	*response = res;
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Helper to sends @data byte and receive response to the keyboard.
 *
 * The keyboard always responds with an ACK (0xFA) or RESEND (0xFE) after
 * receiving a byte.
 *
 * The only exceptions being the ECHO/RESEND commands which have a different
 * "positive answer". To keep the interface simple (one parameter) they don't
 * use this helper.
 *
 * Also, some commands (Identify keyboard, get scan code, self-test) needs to
 * receive additional data after a ACK. Those data must be catch and handled
 * from the command directly.
 *
 * Returns true on success (received ACK), false otherwise (max try reached,
 * unknown response).
 */

static bool keyboard_send(uint8_t data)
{
	uint8_t response = 0;
	size_t max_try = 3;

retry:
	if (max_try-- == 0) {
		error("max try reached");
		return false;
	}

	if (keyboard_send_and_recv(data, &response) == false) {
		error("failed to send/recv data to/from keyboard");
		return false;
	}

	if (response == KBD_RES_RESEND) {
		dbg("received RESEND");
		goto retry;
	} else if (response != KBD_RES_ACK) {
		error("unexpected response received (0x%x)", response);
		return false; // give up now, this could indicate a severe error
	}

	dbg("received ACK");
	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Sends a SET LED STATE command to the keyboard.
 *
 * Leds are set if parameters @scroll, @number or @caps are true, otherwise
 * they are turned off.
 *
 * NOTE: there is no way to query the current led state from keyboard. That is,
 * we need to store the current state in driver data if we want to implement
 * a "toggle this led" feature.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_set_led(bool scroll, bool number, bool caps)
{
	uint8_t led_mask = 0;

	info("starting SET LED STATE sequence...");

	// I cannot validate this works because QEMU doesn't pass led status
	UNTESTED_CODE();

	if(keyboard_send(KBD_CMD_SET_LED) == false) {
		error("failed to send SET LED command");
		return false;
	}
	dbg("sending SET LED command succeed");

	led_mask = (!!scroll << 0) | (!!number << 1) | (!!caps << 2);
	if (keyboard_send(led_mask) == false) {
		error("failed to send new led state");
		return false;
	}
	dbg("sending new led state succeed");

	success("SET LED STATE sequence complete");
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Sends an ECHO command to the keyboard (useful for diagnostic purposes or
 * device remove detection).
 *
 * NOTE: It doesn't use the keyboard_send() helper since the "positive answer"
 * is an ECHO (0xEE) instead of an ACK (0xFA)... and I don't want to add an
 * extra parameter to keyboard_send() (KISS).
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_echo(void)
{
	uint8_t response = 0;
	size_t max_try = 3;

	info("starting ECHO sequence...");

retry:
	if (max_try-- == 0) {
		error("ECHO sequence failed (max try)");
		return false;
	}

	if (keyboard_send_and_recv(KBD_CMD_ECHO, &response) == false) {
		error("failed to send/recv data to/from keyboard");
		return false;
	}

	if (response == KBD_RES_RESEND) {
		warn("received RESEND");
		goto retry;
	} else if (response != KBD_RES_ECHO) {
		error("unexpected response received (0x%x)", response);
		return false; // could indicate a severe error
	}

	success("ECHO sequence complete");
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Get the keyboard scan code set.
 *
 * On success, the result is stored @scs, otherwise @scs is left untouched.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_get_scan_code_set(enum keyboard_scs *scs)
{
	uint8_t scs_status = 0;
	uint8_t response = 0;
	size_t max_try = 3;

	info("starting GET SCAN CODE SET sequence...");

	if (keyboard_send(KBD_CMD_SCAN_CODE_SET) == false) {
		error("failed to send SCAN CODE SET command");
		return false;
	}
	dbg("sending SCAN CODE SET command succeed");

retry:
	if (max_try-- == 0) {
		error("max try reached");
		return false;
	}

	// send zero since we want to know the current scan code set
	scs_status = 0;
	if (keyboard_send_and_recv(scs_status, &response) == false) {
		error("failed to send/recv data to/from keyboard");
		return false;
	}

	if (response == KBD_RES_RESEND) {
		warn("received RESEND");
		goto retry;
	} else if ((response < 1) && (response > 3)) {
		error("unexpected response (0x%x)", response);
		return false;
	}

	*scs = (enum keyboard_scs) response;

	success("GET SCAN CODE SET sequence complete (set = %u)", response);
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Set the keyboard scan code set @scs.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_set_scan_code_set(enum keyboard_scs scs)
{
	info("starting SET SCAN CODE SET (set = %d) sequence...", scs);

	if (scs == KBD_SCS_UNKNOWN) {
		error("invalid argument");
		return false;
	}

	if (scs != KBD_SCS_2) {
		NOT_IMPLEMENTED(); // we only handle scan code set 2 for now (ever?)
		return false;
	}

	if (keyboard_send(KBD_CMD_SCAN_CODE_SET) == false) {
		error("failed to send SCAN CODE SET command");
		return false;
	}
	dbg("sending SCAN CODE SET command succeed");

	if (keyboard_send((uint8_t)scs) == false) {
		error("failed to send the new scan code set");
		return false;
	}

	success("SET SCAN CODE SET sequence complete (set = %d)", scs);
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Identify the keyboard type.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_identify(void)
{
	NOT_IMPLEMENTED(); // this would duplicate PS/2 controller code

	return false;
}

// ----------------------------------------------------------------------------

/*
 * Set typematic rate and delay.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_set_typematic(enum keyboard_typematic_repeat repeat,
								   enum keyboard_typematic_delay delay)
{
	uint8_t typematic = 0;

	info("starting SET TYPEMATIC sequence...");

	// XXX: it's pretty hard to test typematic "visually". Let's check later
	// once we have a TUI/GUI.
	UNTESTED_CODE();

	if (keyboard_send(KBD_CMD_SET_TYPEMATIC) == false) {
		error("failed to send SET TYPEMATIC command");
		return false;
	}
	dbg("sending SET TYPEMATIC comamnd succeed");

	typematic = (uint8_t) repeat | ((uint8_t)delay << 5);

	if (keyboard_send(typematic) == false) {
		error("failed to send new typematic");
		return false;
	}

	success("SET TYPEMATIC sequence complete");

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Enables scanning (keyboard will send scan code).
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_enable_scanning(void)
{
	info("starting ENABLE SCANNING sequence...");

	UNTESTED_CODE();

	if (keyboard_send(KBD_CMD_ENABLE_SCANNING) == false) {
		error("failed to send ENABLE SCANNING command");
		return false;
	}

	success("ENABLE SCANNING sequence complete");

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Disables scanning (keyboard won't send scan code).
 *
 * WARNING: This MAY reset default parameters on some firmware.
 *
 * Returns true on success, false otherwise.
 */

// FIXME: This doesn't work (still receive scanning)! We received an ACK though!

static bool keyboard_disable_scanning(void)
{
	info("starting DISABLE SCANNING sequence...");

	UNTESTED_CODE();

	if (keyboard_send(KBD_CMD_DISABLE_SCANNING) == false) {
		error("failed to send DISABLE SCANNING command");
		return false;
	}

	success("DISABLE SCANNING sequence complete");

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Set default parameters.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_set_default_parameter(void)
{
	info("starting SET DEFAULT PARAMETER sequence...");

	UNTESTED_CODE();

	if (keyboard_send(KBD_CMD_SET_DEFAULT_PARAMS) == false) {
		error("failed to send SET DEFAULT PARAMETER command");
		return false;
	}

	success("SET DEFAULT PARAMETER sequence complete");

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Resend last byte.
 *
 * On success the result is stored in @last_byte, otherwise it is left
 * untouched.
 *
 * Return true on success, false otherwise.
 */

static bool keyboard_resend_last_byte(uint8_t *last_byte)
{
	uint8_t result = 0;

	info("starting RESEND LAST BYTE sequence...");

	UNTESTED_CODE();

	if (last_byte == NULL) {
		error("invalid argument");
		return false;
	}

	if (keyboard_send_and_recv(KBD_CMD_RESEND_LAST_BYTE, &result) == false) {
		error("failed to send RESEND LAST BYTE command");
		return false;
	}

	*last_byte = result;

	success("RESEND LAST BYTE sequence complete (0x%x)", result);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Reset the keyboard and starts the self-test.
 *
 * NOTE: This may reset the keyboard parameters on some firmware.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_reset_and_self_test(void)
{
	uint8_t result = 0;

	info("starting RESET AND SELF-TEST sequence...");

	if (keyboard_send(KBD_CMD_RESET_AND_SELF_TEST) == false) {
		error("failed to send RESET AND SELF-TEST command");
		return false;
	}
	dbg("sending RESET AND SELF-TEST command succeed");

	if (ps2driver_read(&keyboard_driver, &result, KBD_TIMEOUT) == false) {
		error("failed to receive self-test result");
		return false;
	}

	if ((result == KBD_RES_SELF_TEST_FAILED0) ||
		(result == KBD_RES_SELF_TEST_FAILED1))
	{
		error("self-test failed");
		return false;
	} else if (result != KBD_RES_SELF_TEST_PASSED) {
		error("unexpected code (0x%x)", result);
		return false;
	}

	success("RESET AND SELF-TEST sequence complete");

	return true;
}

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
