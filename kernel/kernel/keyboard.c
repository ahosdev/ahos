/*
 * keyboard.c
 *
 * Keyboard driver implementation.
 */

#include <kernel/keyboard.h>

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

static bool keyboard_init(void *param)
{
	param = param;

	// FIXME

	return true;
}

// ----------------------------------------------------------------------------

static void keyboard_release(void)
{
	// FIXME
}

// ----------------------------------------------------------------------------

static void keyboard_recv(uint8_t data)
{
	data = data;

	// FIXME
}

// ----------------------------------------------------------------------------

static void keyboard_enable(void)
{
	// FIXME
}

// ----------------------------------------------------------------------------

static void keyboard_disable(void)
{
	// FIXME
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

struct ps2_device keyboard_device = {
	.name = "KEYBOARD",
	.init = &keyboard_init,
	.release = &keyboard_release,
	.recv = &keyboard_recv,
	.enable = &keyboard_enable,
	.disable = &keyboard_disable,
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
