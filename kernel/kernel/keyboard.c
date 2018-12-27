/*
 * keyboard.c
 *
 * PS/2 Keyboard driver implementation.
 *
 * Documentation:
 * - https://wiki.osdev.org/PS/2_Keyboard
 * - https://www.avrfreaks.net/sites/default/files/PS2%20Keyboard.pdf
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller
 *
 * QEMU source code that handles PS2 keyboard (and mouse):
 * - https://github.com/qemu/qemu/blob/master/hw/input/ps2.c
 *
 * WARNING: Beware of the limitation of your emulation software (if any)
 * and/or buggy PS2 controller/keyboard firmware. For instance QEMU 3.1.0
 * (December 2018) does not handle the following command properly:
 * - KBD_CMD_SET_TYPEMATIC -> NO-OP
 * - KBD_CMD_SET_LED -> no passthrough (testable with BOCHS/VMWARE)
 * - KBD_CMD_RESEND -> inexistent (BOCHS seems to panic on this one)
 * - KBD_CMD_DISABLE -> QEMU also reset the keyboard default parameter
 * - doesnt support any "scancode set 3" only commands
 *
 * TODO:
 * - all keyboards commands shouldn't be callable before driver has started.
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
#define KBD_TIMEOUT 200 // XXX: this should be longer for "reset" command

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

// ----------------------------------------------------------------------------

enum keyboard_led {
	KBD_LED_OFF			= 0, // turns all leds off
	KBD_LED_SCROLL		= 1 << 0,
	KBD_LED_NUMBER		= 1 << 1,
	KBD_LED_CAPSLOCK	= 1 << 2,
};

// ----------------------------------------------------------------------------

enum keyboard_state {
	KBD_STATE_RESET, // flush the recv queue and get back to a clean state
	KBD_STATE_WAIT_SCAN, // wait for a scan code
	KBD_STATE_TRANSLATE, // receive a complete sequence, translate it
};

// ----------------------------------------------------------------------------

// NOTE: this is "keycode", not a direct ASCII translation. For instance, you
// won't find "underscore" here since it is a combination of HYPHEN+SHIFT keys.
enum keycode {
	KEY_UNK,
	// alpha
	KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
	KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
	KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
	// num
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	// others non-special key
	KEY_BKQUOTE, KEY_HYPEN, KEY_EQUAL, KEY_BKSLASH, KEY_LBRACKET, KEY_RBRACKET,
	KEY_SEMICOLON, KEY_SQUOTE, KEY_COMMA, KEY_DOT, KEY_SLASH,
	KEY_BKSP, KEY_SPACE, KEY_TAB, KEY_CAPS, KEY_LSHIFT, KEY_LCTRL, KEY_LALT
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct ps2driver keyboard_driver; // forward declaration
static uint8_t keyboard_led_state = KBD_LED_OFF;
static enum keyboard_scs keyboard_scanset = KBD_SCS_UNKNOWN;
static enum keyboard_state kbd_state = KBD_STATE_RESET;

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
 * The leds are toggled on/off based on the @led_state bitmask.
 *
 * Returns true on success, false otherwise.
 */

static bool keyboard_set_led(uint8_t led_state)
{
	info("starting SET LED STATE sequence...");

	if (led_state > (KBD_LED_SCROLL|KBD_LED_NUMBER|KBD_LED_CAPSLOCK)) {
		error("invalid argument");
		return false;
	}

	if(keyboard_send(KBD_CMD_SET_LED) == false) {
		error("failed to send SET LED command");
		return false;
	}
	dbg("sending SET LED command succeed");

	if (keyboard_send(led_state) == false) {
		error("failed to send new led state");
		return false;
	}
	dbg("sending new led state succeed");

	keyboard_led_state = led_state; // save the current state

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

__attribute__((unused))
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
	struct ps2driver *driver = &keyboard_driver;
	uint8_t scs_status = 0;

	info("starting GET SCAN CODE SET sequence...");

	if (keyboard_send(KBD_CMD_SCAN_CODE_SET) == false) {
		error("failed to send SCAN CODE SET command");
		return false;
	}
	dbg("sending SCAN CODE SET command succeed");

	// send zero since we want to know the current scan code set
	if (keyboard_send(0) == false) {
		error("failed to ask the scan code set");
		return false;
	}

	if (ps2driver_read(driver, &scs_status, KBD_TIMEOUT) == false) {
		error("did not receive current scan code set");
		return false;
	}

	if (scs_status != KBD_SCS_1 &&
		scs_status != KBD_SCS_2 &&
		scs_status != KBD_SCS_3)
	{
		error("unknown scan code set");
		return false;
	}

	*scs = (enum keyboard_scs) scs_status;

	success("GET SCAN CODE SET sequence complete (set = %u)", scs_status);
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

__attribute__((unused))
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

__attribute__((unused))
static bool keyboard_set_typematic(enum keyboard_typematic_repeat repeat,
								   enum keyboard_typematic_delay delay)
{
	uint8_t typematic = 0;

	info("starting SET TYPEMATIC sequence...");

	// XXX: it's pretty hard to test typematic "visually". Let's check later
	// once we have a TUI/GUI. This is not handle by QEMU nor BOCH anyway...
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

__attribute__((unused))
static bool keyboard_disable_scanning(void)
{
	info("starting DISABLE SCANNING sequence...");

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
 * NOTE: This also re-enable scanning.
 *
 * Returns true on success, false otherwise.
 */

__attribute__((unused))
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
 * WARNING: QEMU does not implement this and BOCHS panic on this one. Anyway,
 * this is very hardware specific and does not matter on "emulator only"
 * environment.
 *
 * Return true on success, false otherwise.
 */

__attribute__((unused))
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
 * NOTE: This will re-enable scanning.
 *
 * Returns true on success, false otherwise.
 */

__attribute__((unused))
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

uint8_t last_scancode = 0;

static void keyboard_wait_scan(void)
{
	struct ps2driver *driver = &keyboard_driver;
	uint8_t scancode = 0;

	if (ps2driver_read(driver, &scancode, 0) == false) {
		// no scan code available
		dbg("no scan code");
		return;
	}

	if (scancode == 0xe0 || scancode == 0xe1) {
		// need another scan code (unhandled yet)
		error("scancode not handled (yet)");
	} else if (scancode == 0xf0) {
		// starting break sequence
		if (ps2driver_read(driver, &scancode, 0) == false) {
			error("failed to read next scan code");
		}
		// we got it, now throw it
	} else {
		// translate the scan code into a key code
		dbg("received scan code 0x%x", scancode);
		last_scancode = scancode;
		kbd_state = KBD_STATE_TRANSLATE;
	}
}

// ----------------------------------------------------------------------------

// scan code set 2
enum keycode scan_to_key[] = {
	// 0x00
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x08
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_TAB, KEY_BKQUOTE, KEY_UNK,
	// 0x10
	KEY_UNK, KEY_LALT, KEY_LSHIFT, KEY_UNK, KEY_LCTRL, KEY_Q, KEY_1, KEY_UNK,
	// 0x18
	KEY_UNK, KEY_UNK, KEY_Z, KEY_S, KEY_A, KEY_W, KEY_2, KEY_UNK,
	// 0x20
	KEY_UNK, KEY_C, KEY_X, KEY_D, KEY_E, KEY_4, KEY_3, KEY_UNK,
	// 0x28
	KEY_UNK, KEY_SPACE, KEY_V, KEY_F, KEY_T, KEY_R, KEY_5, KEY_UNK,
	// 0x30
	KEY_UNK, KEY_N, KEY_B, KEY_H, KEY_G, KEY_Y, KEY_6, KEY_UNK,
	// 0x38
	KEY_UNK, KEY_UNK, KEY_M, KEY_J, KEY_U, KEY_7, KEY_8, KEY_UNK,
	// 0x40
	KEY_UNK, KEY_COMMA, KEY_K, KEY_I, KEY_O, KEY_0, KEY_9, KEY_UNK,
	// 0x48
	KEY_UNK, KEY_DOT, KEY_SLASH, KEY_L, KEY_SEMICOLON, KEY_P, KEY_HYPEN, KEY_UNK,
	// 0x50
	KEY_UNK, KEY_UNK, KEY_SQUOTE, KEY_UNK, KEY_LBRACKET, KEY_EQUAL, KEY_UNK, KEY_UNK,
	// 0x58
	KEY_CAPS, KEY_UNK, KEY_UNK, KEY_RBRACKET, KEY_UNK, KEY_BKSLASH, KEY_UNK, KEY_UNK,
	// 0x60
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_BKSP, KEY_UNK,
	// 0x68
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x70
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x78
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x80
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x88
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x90
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x98
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xa0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xa8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xb0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xb8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xc0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xc8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xd0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xd8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xe0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xe8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xf0
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0xf8
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
};

static void strcpy(char *dst, char *src) // FIXME: temporary
{
	while (*src) {
		*dst++ = *src++;
	}
}

#include <string.h>

static void keycode2str(enum keycode kc, char *buf, size_t buf_size)
{
	memset(buf, 0, buf_size);

	if (kc >= KEY_A && kc <= KEY_Z) {
		buf[0] = 'A' + (kc - KEY_A);
	} else if (kc >= KEY_0 && kc <= KEY_9) {
		buf[0] = '0' + (kc - KEY_0);
	} else {
		switch (kc) {
		case KEY_BKQUOTE:	buf[0] = '`'; break;
		case KEY_HYPEN:		buf[0] = '-'; break;
		case KEY_EQUAL:		buf[0] = '='; break;
		case KEY_BKSLASH:	buf[0] = '\\'; break;
		case KEY_LBRACKET:	buf[0] = '['; break;
		case KEY_RBRACKET:	buf[0] = ']'; break;
		case KEY_SEMICOLON: buf[0] = ';'; break;
		case KEY_SQUOTE:	buf[0] = '\''; break;
		case KEY_COMMA:		buf[0] = ','; break;
		case KEY_DOT:		buf[0] = '.'; break;
		case KEY_SLASH:		buf[0] = '/'; break;
		case KEY_SPACE:		buf[0] = ' '; break;
		case KEY_BKSP:		strcpy(buf, "<BKSP>"); break;
		case KEY_TAB:		strcpy(buf, "<TAB>"); break;
		case KEY_CAPS:		strcpy(buf, "<CAPS>"); break;
		case KEY_LSHIFT:	strcpy(buf, "<LSHIFT>"); break;
		case KEY_LCTRL:		strcpy(buf, "<LCTRL>"); break;
		case KEY_LALT:		strcpy(buf, "<LALT>"); break;
		default:			strcpy(buf, "<UNKNOWN>"); break;
		}
	}
}

static void keyboard_translate(void)
{
	uint8_t scancode = last_scancode;
	enum keycode kc = scan_to_key[scancode];

	dbg("scancode = 0x%x", scancode);

	char buf[16];
	keycode2str(kc, buf, sizeof(buf));
	info("key detected '%s'", buf);

	kbd_state = KBD_STATE_WAIT_SCAN; // process a new sequence
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the keyboard driver.
 *
 * The driver assumes that:
 * - the device is enabled
 * - it has been reset and passed the power-on self-test
 * - scanning has been disabled by the PS/2 controller
 * - the PS/2 controller enable its interrupts (configuration byte)
 * - its IRQ line is cleared
 * - the receive queue might NOT by empty (holds garbage)
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

	ps2driver_flush_recv_queue(driver);

	// turns all led off (should be the case after reset but let's be paranoid)
	if (keyboard_set_led(KBD_LED_OFF) == false) {
		warn("failed to turn leds off");
		// we can continue here even if it failed
	}

	if (keyboard_get_scan_code_set(&keyboard_scanset) == false) {
		error("failed to retrieve current scan code set");
		return false;
	} else if (keyboard_scanset != KBD_SCS_2) {
		dbg("the keyboard is currently in another mode than scan code set 2");
		if (keyboard_set_scan_code_set(KBD_SCS_2) == false) {
			error("failed to change scan code set to 2");
			return false;
		}
	}

	if (keyboard_enable_scanning() == false) {
		error("failed to re-enable scanning");
		return true;
	}

	kbd_state = KBD_STATE_RESET;

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
	// .send() is set by the PS/2 controller during drivers start
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

// ----------------------------------------------------------------------------

/*
 * Keyboard task entry point.
 *
 * This is where the keyboard state machine processing starts. It should returns
 * often in order to give the scheduler opportunity to run another task.
 */

void keyboard_task(void)
{
	struct ps2driver *driver = &keyboard_driver;

	for (size_t loop = 0; loop < 2; ++loop) {
		switch (kbd_state) {
			case KBD_STATE_RESET:
				ps2driver_flush_recv_queue(driver);
				kbd_state = KBD_STATE_WAIT_SCAN;
				break;
			case KBD_STATE_WAIT_SCAN:
				keyboard_wait_scan();
				break;
			case KBD_STATE_TRANSLATE:
				keyboard_translate();
				break;
			default:
				warn("unknown keyboard state, switching to RESET state");
				kbd_state = KBD_STATE_RESET;
				break;
		}
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
