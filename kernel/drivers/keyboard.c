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
 * - doesnt support any "scancode set 3 only" commands
 *
 * TODO:
 * - all keyboards commands shouldn't be callable before driver has started.
 * - handle scan code set 1 and 3
 */

#include <drivers/keyboard.h>
#include <drivers/ps2driver.h>
#include <drivers/ps2ctrl.h>
#include <kernel/log.h>

#include <stdlib.h>
#include <string.h>

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
	KBD_STATE_READ_MORE, // received a first scan code but we need more
	KBD_STATE_TRANSLATE, // receive a complete sequence, translate it
};

// ----------------------------------------------------------------------------

// NOTE: this is "keycode", not a direct ASCII translation. For instance, you
// won't find "underscore" here since it is a combination of HYPHEN+SHIFT keys.
enum keycode {
	KEY_UNK,
	// --- 1 byte scan code ---
	// alpha
	KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
	KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
	KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
	// num
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	// others key
	KEY_BKQUOTE, KEY_HYPEN, KEY_EQUAL, KEY_BKSLASH, KEY_LBRACKET, KEY_RBRACKET,
	KEY_SEMICOLON, KEY_SQUOTE, KEY_COMMA, KEY_DOT, KEY_SLASH,
	KEY_BKSP, KEY_SPACE, KEY_TAB, KEY_CAPS, KEY_LSHIFT, KEY_LCTRL, KEY_LALT,
	KEY_ENTER, KEY_ESC, KEY_SCROLL, KEY_NUM, KEY_LT, KEY_RSHIFT,
	// function keys
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9,
	KEY_F10, KEY_F11, KEY_F12,
	// keypad
	KEY_KP_STAR, KEY_KP_HYPHEN, KEY_KP_MINUS, KEY_KP_PLUS, KEY_KP_DOT,
	KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
	KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,

	// --- 2 bytes keycodes ---
	KEY_LGUI, KEY_RCTRL, KEY_RGUI, KEY_RALT, KEY_APPS, KEY_INSERT, KEY_HOME,
	KEY_PGUP, KEY_DEL, KEY_END, KEY_PGDOWN, KEY_UP, KEY_LEFT, KEY_DOWN,
	KEY_RIGHT, KEY_KP_DIV, KEY_KP_EN,

	// --- extra long keycodes ---
	KEY_PRNT_SCRN, KEY_PAUSE,
};

// ----------------------------------------------------------------------------

// scan code set 2 (one byte only)
enum keycode scan_to_key[] = {
	// 0x00
	KEY_UNK, KEY_F9, KEY_UNK, KEY_F5, KEY_F3, KEY_F1, KEY_F2, KEY_F12,
	// 0x08
	KEY_UNK, KEY_F10, KEY_F8, KEY_F6, KEY_F4, KEY_TAB, KEY_BKQUOTE, KEY_UNK,
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
	KEY_CAPS, KEY_RSHIFT, KEY_ENTER, KEY_RBRACKET, KEY_UNK, KEY_BKSLASH, KEY_UNK, KEY_UNK,
	// 0x60
	KEY_UNK, KEY_LT, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK, KEY_BKSP, KEY_UNK,
	// 0x68
	KEY_UNK, KEY_KP_1, KEY_UNK, KEY_KP_4, KEY_KP_7, KEY_UNK, KEY_UNK, KEY_UNK,
	// 0x70
	KEY_KP_0, KEY_KP_DOT, KEY_KP_2, KEY_KP_5, KEY_KP_6, KEY_KP_8, KEY_ESC, KEY_NUM,
	// 0x78
	KEY_F11, KEY_KP_PLUS, KEY_KP_3, KEY_KP_HYPHEN, KEY_KP_STAR, KEY_KP_9, KEY_SCROLL, KEY_UNK,
	// 0x80
	KEY_UNK, KEY_UNK, KEY_UNK, KEY_F7, KEY_UNK, KEY_UNK, KEY_UNK, KEY_UNK,
};

// ----------------------------------------------------------------------------

enum keycode_type {
	KBD_KEYTYPE_MAKE,
	KBD_KEYTYPE_BREAK,
};

// ----------------------------------------------------------------------------

struct scancode_seq {
	unsigned char scancodes[8]; // maximum sequence on SCS-2 is 8 (pause)!
	size_t len;
	size_t need;
};

// ----------------------------------------------------------------------------

struct keycode_res {
	enum keycode kc;
	enum keycode_type type;
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct ps2driver keyboard_driver; // forward declaration
static uint8_t keyboard_led_state = KBD_LED_OFF;
static enum keyboard_scs keyboard_scanset = KBD_SCS_UNKNOWN;
static enum keyboard_state kbd_state = KBD_STATE_RESET;
static struct scancode_seq kbd_seq;

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
		case KEY_LT:		buf[0] = '<'; break;
		case KEY_BKSP:		strcpy(buf, "<BKSP>"); break;
		case KEY_TAB:		strcpy(buf, "<TAB>"); break;
		case KEY_CAPS:		strcpy(buf, "<CAPS>"); break;
		case KEY_LSHIFT:	strcpy(buf, "<LSHIFT>"); break;
		case KEY_RSHIFT:	strcpy(buf, "<RSHIFT>"); break;
		case KEY_LCTRL:		strcpy(buf, "<LCTRL>"); break;
		case KEY_LALT:		strcpy(buf, "<LALT>"); break;
		case KEY_ENTER:		strcpy(buf, "<ENTER>"); break;
		case KEY_ESC:		strcpy(buf, "<ESC>"); break;
		case KEY_F1:		strcpy(buf, "<F1>"); break;
		case KEY_F2:		strcpy(buf, "<F2>"); break;
		case KEY_F3:		strcpy(buf, "<F3>"); break;
		case KEY_F4:		strcpy(buf, "<F4>"); break;
		case KEY_F5:		strcpy(buf, "<F5>"); break;
		case KEY_F6:		strcpy(buf, "<F6>"); break;
		case KEY_F7:		strcpy(buf, "<F7>"); break;
		case KEY_F8:		strcpy(buf, "<F8>"); break;
		case KEY_F9:		strcpy(buf, "<F9>"); break;
		case KEY_F10:		strcpy(buf, "<F10>"); break;
		case KEY_F11:		strcpy(buf, "<F11>"); break;
		case KEY_F12:		strcpy(buf, "<F12>"); break;
		case KEY_SCROLL:	strcpy(buf, "<SCROLL>"); break;
		case KEY_NUM:		strcpy(buf, "<NUM>"); break;
		case KEY_KP_STAR:	strcpy(buf, "<KP_STAR>"); break;
		case KEY_KP_HYPHEN:	strcpy(buf, "<KP_HYPHEN>"); break;
		case KEY_KP_MINUS:	strcpy(buf, "<KP_MINUS>"); break;
		case KEY_KP_PLUS:	strcpy(buf, "<KP_PLUS>"); break;
		case KEY_KP_DOT:	strcpy(buf, "<KP_DOT>"); break;
		case KEY_KP_0:		strcpy(buf, "<KP_0>"); break;
		case KEY_KP_1:		strcpy(buf, "<KP_1>"); break;
		case KEY_KP_2:		strcpy(buf, "<KP_2>"); break;
		case KEY_KP_3:		strcpy(buf, "<KP_3>"); break;
		case KEY_KP_4:		strcpy(buf, "<KP_4>"); break;
		case KEY_KP_5:		strcpy(buf, "<KP_5>"); break;
		case KEY_KP_6:		strcpy(buf, "<KP_6>"); break;
		case KEY_KP_7:		strcpy(buf, "<KP_7>"); break;
		case KEY_KP_8:		strcpy(buf, "<KP_8>"); break;
		case KEY_KP_9:		strcpy(buf, "<KP_9>"); break;

		// 2 bytes keycodes
		case KEY_LGUI:		strcpy(buf, "<LGUI>"); break;
		case KEY_RCTRL:		strcpy(buf, "<RCTRL>"); break;
		case KEY_RGUI:		strcpy(buf, "<RGUI>"); break;
		case KEY_RALT:		strcpy(buf, "<RALT>"); break;
		case KEY_APPS:		strcpy(buf, "<APPS>"); break;
		case KEY_INSERT:	strcpy(buf, "<INSERT>"); break;
		case KEY_HOME:		strcpy(buf, "<HOME>"); break;
		case KEY_PGUP:		strcpy(buf, "<PGUP>"); break;
		case KEY_DEL:		strcpy(buf, "<DEL>"); break;
		case KEY_END:		strcpy(buf, "<END>"); break;
		case KEY_PGDOWN:	strcpy(buf, "<PGDOWN>"); break;
		case KEY_UP:		strcpy(buf, "<UP>"); break;
		case KEY_LEFT:		strcpy(buf, "<LEFT>"); break;
		case KEY_DOWN:		strcpy(buf, "<DOWN>"); break;
		case KEY_RIGHT:		strcpy(buf, "<RIGHT>"); break;
		case KEY_KP_DIV:	strcpy(buf, "<KP_DIV>"); break;
		case KEY_KP_EN:		strcpy(buf, "<KP_EN>"); break;

		// extra long keycode
		case KEY_PRNT_SCRN:	strcpy(buf, "<PRNT_SCRN>"); break;
		case KEY_PAUSE:		strcpy(buf, "<PAUSE>"); break;

		default:			strcpy(buf, "<UNKNOWN>"); break;
		}
	}
}

// ----------------------------------------------------------------------------

static bool keyboard_validate_sequence(unsigned char *expect,
									   unsigned char *seq,
									   size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		if (expect[i] != seq[i]) {
			dbg("error: expected 0x%x, got 0x%x", expect[i], seq[i]);
			return false;
		}
	}

	return true;
}

// ----------------------------------------------------------------------------

static enum keycode scan_2bytes_to_key(unsigned char scan)
{
	switch (scan) {
		case 0x1f: return KEY_LGUI;
		case 0x14: return KEY_RCTRL;
		case 0x27: return KEY_RGUI;
		case 0x11: return KEY_RALT;
		case 0x2f: return KEY_APPS;
		case 0x70: return KEY_INSERT;
		case 0x6c: return KEY_HOME;
		case 0x7d: return KEY_PGUP;
		case 0x71: return KEY_DEL;
		case 0x69: return KEY_END;
		case 0x7a: return KEY_PGDOWN;
		case 0x75: return KEY_UP;
		case 0x6b: return KEY_LEFT;
		case 0x72: return KEY_DOWN;
		case 0x74: return KEY_RIGHT;
		case 0x4a: return KEY_KP_DIV;
		case 0x5a: return KEY_KP_EN;
	}

	warn("unknown 2 bytes scan code");
	return KEY_UNK;
}

// ----------------------------------------------------------------------------

static struct keycode_res keyboard_state_translate(void)
{
	struct keycode_res res;

	if (kbd_seq.len == 1) {
		// one byte make code
		res.kc = scan_to_key[kbd_seq.scancodes[0]];
		res.type = KBD_KEYTYPE_MAKE;
	} else if (kbd_seq.len == 2) {
		if (kbd_seq.scancodes[0] == 0xe0) {
			// two bytes make code
			res.kc = scan_2bytes_to_key(kbd_seq.scancodes[1]);
			res.type = KBD_KEYTYPE_MAKE;
		} else {
			// one byte break code
			res.kc = scan_to_key[kbd_seq.scancodes[1]];
			res.type = KBD_KEYTYPE_BREAK;
		}
	} else if (kbd_seq.len == 3) {
			// two bytes break code
		res.kc = scan_2bytes_to_key(kbd_seq.scancodes[2]);
		res.type = KBD_KEYTYPE_BREAK;
	} else if (kbd_seq.len == 4) {
		unsigned char expect[] = { 0xe0, 0x12, 0xe0, 0x7c };
		if (!keyboard_validate_sequence(expect, kbd_seq.scancodes, kbd_seq.len)) {
			warn("unexpected scancode in print screen make sequence");
		}
		res.kc = KEY_PRNT_SCRN;
		res.type = KBD_KEYTYPE_MAKE;
	} else if (kbd_seq.len == 6) {
		unsigned char expect[] =
			{ 0xe0, 0xf0, 0x7c, 0xe0, 0xf0, 0x12 };
		if (!keyboard_validate_sequence(expect, kbd_seq.scancodes, kbd_seq.len)) {
			warn("unexpected scancode in print screen break sequence");
		}
		res.kc = KEY_PRNT_SCRN;
		res.type = KBD_KEYTYPE_BREAK;
	} else if (kbd_seq.len == 8) {
		unsigned char expect[] =
			{ 0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77 };
		if (!keyboard_validate_sequence(expect, kbd_seq.scancodes, kbd_seq.len)) {
			warn("unexpected scancode in pause sequence");
		}
		res.kc = KEY_PAUSE;
		res.type = KBD_KEYTYPE_MAKE;
	} else {
		error("unknown sequence len %d", kbd_seq.len);
		res.kc = KEY_UNK;
		res.type = KBD_KEYTYPE_MAKE;
	}

	kbd_state = KBD_STATE_WAIT_SCAN;

	return res;
}

// ----------------------------------------------------------------------------

static void keyboard_state_read_more(void)
{
	uint8_t scancode = 0;

	if (ps2driver_read(&keyboard_driver, &scancode, 0) == false) {
		// no scan code available
		dbg("no scan code");
		return;
	}

	kbd_seq.scancodes[kbd_seq.len++] = scancode;
	kbd_seq.need--;

	if ((kbd_seq.len == 2) && (kbd_seq.scancodes[0] == 0xe0)) {
		if (kbd_seq.scancodes[1] == 0x12) {
			// print screen make code
			kbd_seq.need = 2;
		} else if (kbd_seq.scancodes[1] == 0xf0) {
			// two bytes break code
			kbd_seq.need = 1;
		} else {
			// two-bytes make code
		}
	} else if ((kbd_seq.len == 3) && (kbd_seq.scancodes[2] == 0x7c)) {
		// print screen break code
		kbd_seq.need = 3;
	}

	if (kbd_seq.need == 0) {
		kbd_state = KBD_STATE_TRANSLATE;
	}
}

// ----------------------------------------------------------------------------

static void keyboard_state_wait_scan(void)
{
	uint8_t scancode = 0;

	if (ps2driver_read(&keyboard_driver, &scancode, 0) == false) {
		// no scan code available
		dbg("no scan code");
		return;
	}

	kbd_seq.scancodes[0] = scancode;
	kbd_seq.len = 1;

	switch (scancode) {
		case 0xe0: /* fall through */
		case 0xf0:
			kbd_seq.need = 1;
			kbd_state = KBD_STATE_READ_MORE;
			break;
		case 0xe1:
			kbd_seq.need = 7;
			kbd_state = KBD_STATE_READ_MORE;
			break;
		default:
			kbd_state = KBD_STATE_TRANSLATE;
			break;
	};
}

// ----------------------------------------------------------------------------

static void keyboard_state_reset(void)
{
	ps2driver_flush_recv_queue(&keyboard_driver);

	kbd_seq.len = 0;
	kbd_seq.need = 0;

	kbd_state = KBD_STATE_WAIT_SCAN;
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
	dbg("starting SET LED STATE sequence...");

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

	dbg("SET LED STATE sequence complete");
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

	dbg("starting ECHO sequence...");

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

	dbg("ECHO sequence complete");
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

	dbg("starting GET SCAN CODE SET sequence...");

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

	dbg("GET SCAN CODE SET sequence complete (set = %u)", scs_status);
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
	dbg("starting SET SCAN CODE SET (set = %d) sequence...", scs);

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

	dbg("SET SCAN CODE SET sequence complete (set = %d)", scs);
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

	dbg("starting SET TYPEMATIC sequence...");

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

	dbg("SET TYPEMATIC sequence complete");

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
	dbg("starting ENABLE SCANNING sequence...");

	if (keyboard_send(KBD_CMD_ENABLE_SCANNING) == false) {
		error("failed to send ENABLE SCANNING command");
		return false;
	}

	dbg("ENABLE SCANNING sequence complete");

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
	dbg("starting DISABLE SCANNING sequence...");

	if (keyboard_send(KBD_CMD_DISABLE_SCANNING) == false) {
		error("failed to send DISABLE SCANNING command");
		return false;
	}

	dbg("DISABLE SCANNING sequence complete");

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
	dbg("starting SET DEFAULT PARAMETER sequence...");

	UNTESTED_CODE();

	if (keyboard_send(KBD_CMD_SET_DEFAULT_PARAMS) == false) {
		error("failed to send SET DEFAULT PARAMETER command");
		return false;
	}

	dbg("SET DEFAULT PARAMETER sequence complete");

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

	dbg("starting RESEND LAST BYTE sequence...");

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

	dbg("RESEND LAST BYTE sequence complete (0x%x)", result);

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

	dbg("starting RESET AND SELF-TEST sequence...");

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

	dbg("RESET AND SELF-TEST sequence complete");

	return true;
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
	struct keycode_res kc_res;
	char buf[16];

	switch (kbd_state) {
		case KBD_STATE_RESET:
			keyboard_state_reset();
			break;
		case KBD_STATE_WAIT_SCAN:
			keyboard_state_wait_scan();
			break;
		case KBD_STATE_READ_MORE:
			keyboard_state_read_more();
			break;
		case KBD_STATE_TRANSLATE:
			kc_res = keyboard_state_translate();
			// TODO: add the result into a queue
			keycode2str(kc_res.kc, buf, sizeof(buf));
			info("key <%s> %s",
				buf, kc_res.type == KBD_KEYTYPE_MAKE ? "pressed" : "released");
			break;
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
