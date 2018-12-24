/*
 * ps2ctrl.c
 *
 * Implementation for 8042 PS/2 Controller.
 *
 * The 8042 controller is multi-purpose controller responsible to manage:
 * - communication with PS/2 keyboard
 * - communication with PS/2 mouse
 * - A20 gate handling
 * - system reset
 *
 * Documentation:
 * - http://www.diakom.ru/el/elfirms/datashts/Smsc/42w11.pdf (datasheet)
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller
 * - https://wiki.osdev.org/PS/2_Keyboard
 */

#include <stdio.h>

#include <kernel/ps2ctrl.h>
#include <kernel/types.h>

#include "io.h"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// I/O port mapping
#define DATA_PORT	0x0060 // read/write
#define STATUS_PORT	0x0064 // read only
#define CMD_PORT	0x0064 // write only

// Status Register
#define SR_OUTPUT_BUFFER_STATUS (1 << 0) // 0=empty, 1=full
#define SR_INPUT_BUFFER_STATUS	(1 << 1) // 0=empty, 1=full
#define SR_SYSTEM_FLAG			(1 << 2)
#define SR_CMD_DATA				(1 << 3) // 0=to PS/2 device, 1=to PS/2 controller
#define SR_UNKNOWN1				(1 << 4) // chipset specific
#define SR_UNKNOWN2				(1 << 5) // chipset specific
#define SR_TIMEOUT_ERROR		(1 << 6) // 0=no error, 1=time-out error
#define SR_PARITY_ERROR			(1 << 7) // 0=no error, 1=parity error

// Command Register (only to PS/2 controller, NOT devices)
#define CMD_READ_BYTE_0				0x20 // Controller Configuration Byte
#define CMD_READ_BYTE_N				0x21 // unknown
// CMD_READ_BYTE_N up to 0x3f is "unknown"
#define CMD_WRITE_BYTE_0			0x60 // Controller Configuration Byte
#define CMD_WRITE_BYTE_1			0x61 // unknown
// CMD_WRITE_BYTE_N	up to 0x7f is "unknown"
#define CMD_DISABLE_SECOND_PS2_PORT 0xA7 // only if 2 PS/2 ports supported
#define CMD_ENABLE_SECOND_PS2_PORT	0xA8 // only if 2 PS/2 ports supported
#define CMD_TEST_SECOND_PS2_PORT	0xA9 // only if 2 PS/2 ports supported
#define CMD_TEST_PS2_CONTROLLER		0xAA
#define CMD_TEST_FIRST_PS2_PORT		0xAB
#define CMD_DIAGNOSTIC_DUMP			0xAC
#define CMD_DISABLE_FIRST_PS2_PORT	0xAD
#define CMD_ENABLE_FIRST_PS2_PORT	0xAE
#define CMD_READ_CTRL_INPUT_PORT	0xC0 // unknown
#define CMD_COPY_BITS03_to_47		0xC1 // from input port to status bits 4-7
#define CMD_COPY_BITS47_to_47		0xC2 // from input port to status bits 4-7
#define CMD_READ_CTRL_OUTPUT_PORT	0xD0
#define CMD_WRITE_BYTE_CTRL_OUTPUT_PORT			0xD1
#define CMD_WRITE_BYTE_FIRST_PS2_OUTPUT_PORT	0xD2 // only if 2 PS/2 supported
#define CMD_WRITE_BYTE_SECOND_PS2_OUTPUT_PORT	0xD3 // only if 2 PS/2 supported
#define CMD_WRITE_BYTE_SECOND_PS2_INPUT_PORT	0xD4 // only if 2 PS/2 supported
#define CMD_PULSE_OUTPUT_LINE		0xF0
// CMD_PULSE_OUTPUT_LINE_* up to '0xFF'

// PS/2 Controller Configuration Byte
#define CTRL_CONF_FIRST_PS2_PORT_INTERRUPT	(1 << 0) // 0=disabled, 1=enabled
#define CTRL_CONF_SECOND_PS2_PORT_INTERRUPT	(1 << 1) // 0=disabled, 1=enabled (only if 2 PS/2 supported)
#define CTRL_CONF_SYSTEM_FLAG				(1 << 2) // 1=system passed POST, 0=OS shouldn't be running
#define CTRL_CONF_ZERO1						(1 << 3) // must be zero
#define CTRL_CONF_FIRST_PS2_PORT_CLOCK		(1 << 4) // 0=enabled, 1=disabled
#define CTRL_CONF_SECOND_PS2_PORT_CLOCK		(1 << 5) // 0=enabled, 1=disabled (only if 2 PS/2 supported)
#define CTRL_CONF_FIRST_PS2_PORT_TRANSLATION (1 << 6) // 0=disabled, 1=enabled
#define CTRL_CONF_ZERO2						(1 << 7) // must be zero

// PS/2 Controller Output Port
#define CTRL_OUTPUT_PORT_SYSTEM_RESET			(1 << 0) // always set to 1 (warning: 0 can lock computer!)
#define CTRL_OUTPUT_PORT_A20_GATE				(1 << 1)
#define CTRL_OUTPUT_PORT_SECOND_PS2_PORT_CLOCK	(1 << 2) // only if 2 PS/2 supported
#define CTRL_OUTPUT_PORT_SECOND_PS2_PORT_DATA	(1 << 3) // only if 2 PS/2 supported
#define CTRL_OUTPUT_PORT_FULL_FROM_FIRST_PS2	(1 << 4) // connected to IRQ1
#define CTRL_OUTPUT_PORT_FULL_FROM_SECOND_PS2	(1 << 5) // connected to IRQ12, only if 2 PS/2 supported
#define CTRL_OUTPUT_PORT_FIRST_PS2_PORT_CLOCK	(1 << 6)
#define CTRL_OUTPUT_PORT_FIRST_PS2_PORT_DATA	(1 << 7)

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static bool disable_usb_legacy_support(void)
{
	// TODO: initialize USB controllers and disable USB legacy support

	return true; // we assume it is done for now
}

// ----------------------------------------------------------------------------

static bool ps2ctrl_exists(void)
{
	// TODO: check with ACPI

	return true; // we assume it exists for now
}

// ----------------------------------------------------------------------------

static void disable_devices(void)
{
	// TODO: disable first device (0xAD)
	// TODO: disable second device (0xA7)
}

// ----------------------------------------------------------------------------

static void flush_controller_output_buffer(void)
{
	// TODO: check status register and/or read from data port
}

// ----------------------------------------------------------------------------

static void set_controller_configuration_byte(void)
{
	// TODO
}

// ----------------------------------------------------------------------------

static bool check_controller_selt_test(void)
{
	// TODO

	return true;
}

// ----------------------------------------------------------------------------

static bool has_two_channels(void)
{
	// TODO

	return false;
}

// ----------------------------------------------------------------------------

static bool check_interface_test(void)
{
	// TODO

	return true;
}

// ----------------------------------------------------------------------------

static bool enable_devices(void)
{
	// TODO

	return true;
}

// ----------------------------------------------------------------------------

static bool reset_devices(void)
{
	// TODO

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the PS/2 8042 Controller. It assumes interrupts are disabled.
 */

int ps2ctrl_init(void)
{
	printf("[ps2ctrl] starting initialization...\n");

	if (!disable_usb_legacy_support()) {
		printf("[ps2ctrl] ERROR: failed to disable USB legacy support\n");
	} else {
		printf("[ps2ctrl] USB legacy support disabled (fake)\n");
	}

	if (!ps2ctrl_exists()) {
		printf("[ps2ctrl] ERROR: PS/2 Controller does not exist\n");
		return -1;
	} else {
		printf("[ps2ctrl] PS/2 Controller exist (fake)\n");
	}

	disable_devices();
	printf("[ps2ctrl] devices are disabled\n");

	flush_controller_output_buffer();
	printf("[ps2ctrl] controller's output buffer is flushed\n");

	set_controller_configuration_byte();
	printf("[ps2ctrl] controller's configuration byte set\n");

	if (!check_controller_selt_test()) {
		printf("[ps2ctrl] ERROR: failed to perform controller self test\n");
	} else {
		printf("[ps2ctrl] controller self test succeed\n");
	}

	if (has_two_channels()) {
		printf("[ps2ctrl] controller has two channels\n");
	} else {
		printf("[ps2ctrl] controller has one channel\n");
	}

	if (!check_interface_test()) {
		printf("[ps2ctrl] ERROR: interface test failed\n");
	} else {
		printf("[ps2ctrl] interface test succeed\n");
	}

	if (!enable_devices()) {
		printf("[ps2ctrl] ERROR: failed to enable devices\n");
	} else {
		printf("[ps2ctrl] enabling devices succeed\n");
	}

	if (!reset_devices()) {
		printf("[ps2ctrl] ERROR: failed to reset devices\n");
	} else {
		printf("[ps2ctrl] resetting devices succeed\n");
	}

	printf("[ps2ctrl] initialization complete\n");

	return 0;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
