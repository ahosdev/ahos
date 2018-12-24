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
 *
 * TODO:
 * - handle time-out error (status register)
 * - handle parity error (status register)
 * - implement cpu reset
 */

#include <stdio.h>
#include <stdlib.h>

#include <kernel/ps2ctrl.h>
#include <kernel/types.h>
#include <kernel/interrupt.h>

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

enum ctrl_command {
	READ_BYTE_0	= 0x20, // Controller Configuration Byte
	READ_BYTE_N	= 0x21, // unknown
	// CMD_READ_BYTE_N up to 0x3f is "unknown"
	WRITE_BYTE_0 = 0x60, // Controller Configuration Byte
	WRITE_BYTE_1 = 0x61, // unknown
	// CMD_WRITE_BYTE_N	up to 0x7f is "unknown"
	DISABLE_SECOND_PS2_PORT = 0xA7, // only if 2 PS/2 ports supported
	ENABLE_SECOND_PS2_PORT = 0xA8, // only if 2 PS/2 ports supported
	TEST_SECOND_PS2_PORT = 0xA9, // only if 2 PS/2 ports supported
	TEST_PS2_CONTROLLER = 0xAA,
	TEST_FIRST_PS2_PORT = 0xAB,
	DIAGNOSTIC_DUMP = 0xAC,
	DISABLE_FIRST_PS2_PORT = 0xAD,
	ENABLE_FIRST_PS2_PORT = 0xAE,
	READ_CTRL_INPUT_PORT = 0xC0, // unknown
	COPY_BITS03_to_47 = 0xC1, // from input port to status bits 4-7
	COPY_BITS47_to_47 = 0xC2, // from input port to status bits 4-7
	READ_CTRL_OUTPUT_PORT = 0xD0,
	WRITE_BYTE_CTRL_OUTPUT_PORT = 0xD1,
	WRITE_BYTE_FIRST_PS2_OUTPUT_PORT = 0xD2, // only if 2 PS/2 supported
	WRITE_BYTE_SECOND_PS2_OUTPUT_PORT = 0xD3, // only if 2 PS/2 supported
	WRITE_BYTE_SECOND_PS2_INPUT_PORT = 0xD4, // only if 2 PS/2 supported
	PULSE_OUTPUT_LINE = 0xF0,
	// CMD_PULSE_OUTPUT_LINE_* up to '0xFF'
};

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

// ----------------------------------------------------------------------------

static bool ps2ctrl_initialized = false;
static bool ps2ctrl_single_channel = true;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Wait until the controller's input buffer is EMPTY.
 */

static void wait_ctrl_input_buffer_ready(void)
{
	uint8_t status = 0;

	do {
		status = inb(STATUS_PORT);
		// TODO: implement timeout
	} while (status & SR_INPUT_BUFFER_STATUS);
}

// ----------------------------------------------------------------------------

/*
 * Wait until the controller's output buffer is FULL.
 */

static void wait_ctrl_output_buffer_ready(void)
{
	uint8_t status = 0;

	do {
		status = inb(STATUS_PORT);
		// TODO: implement timeout
	} while ((status & SR_OUTPUT_BUFFER_STATUS) == 0);
}

// ----------------------------------------------------------------------------

/*
 * Send a command to the controller. If there is two bytes, then @data must not
 * be NULL. On the other hand, if the command expect a response, @response must
 * not be NULL.
 */

static bool __send_ctrl_cmd(enum ctrl_command cmd, uint8_t *data, uint8_t *response)
{
	if (data && response) {
		printf("[ps2ctrl] ERROR: there is no known command that send data "
			   "and expect a response\n");
		return false;
	}

	wait_ctrl_input_buffer_ready();
	outb(CMD_PORT, (uint8_t) cmd);

	if (data) {
		wait_ctrl_input_buffer_ready();
		outb(DATA_PORT, *data);
	} else if (response) {
		wait_ctrl_output_buffer_ready();
		*response = inb(DATA_PORT);
	}

	return true; // FIXME: never failed until timeout is implemented
}

// ----------------------------------------------------------------------------

/*
 * Helper to send a command to the controller.
 */

static inline bool send_ctrl_cmd(enum ctrl_command cmd)
{
	return __send_ctrl_cmd(cmd, NULL, NULL);
}

// ----------------------------------------------------------------------------

/*
 * Helper to send a command with data to the controller.
 */

static inline bool send_ctrl_cmd_with_data(enum ctrl_command cmd, uint8_t data)
{
	return __send_ctrl_cmd(cmd, &data, NULL);
}

// ----------------------------------------------------------------------------

/*
 * Helper to send a command with a response to the controller.
 */

static inline bool send_ctrl_cmd_with_response(enum ctrl_command cmd, uint8_t *response)
{
	return __send_ctrl_cmd(cmd, NULL, response);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#if 0
__attribute__ ((unused)) // debugging function: skip compilation warning
static void dump_status_register(uint8_t status)
{
	printf("[ps2ctrl] ERROR: NOT IMPLEMENTED\n");

	// TODO: implement me
}
#endif

// ----------------------------------------------------------------------------

__attribute__ ((unused)) // debugging function: skip compilation warning
static void dump_configuration_byte(uint8_t conf_byte)
{
	printf("[ps2ctrl] dumping configuration byte:\n");
	printf("- first PS/2 port interrupt: %s\n",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_INTERRUPT) ? "enabled" : "disabled");
	printf("- first PS/2 port clock: %s\n",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_CLOCK) ? "disabled" : "enabled");
	printf("- first PS/2 port translation: %s\n",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_TRANSLATION) ? "enabled" : "disabled");
	printf("- second PS/2 port interrupt: %s\n",
		(conf_byte & CTRL_CONF_SECOND_PS2_PORT_INTERRUPT) ? "enabled" : "disabled");
	printf("- second PS/2 port clock: %s\n",
		(conf_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK) ? "disabled" : "enabled");
	printf("- system flag: %s\n",
		(conf_byte & CTRL_CONF_SYSTEM_FLAG) ? "system passed POST" : "ERROR");
	printf("- zero0: %d\n", !!(conf_byte & CTRL_CONF_ZERO1));
	printf("- zero1: %d\n", !!(conf_byte & CTRL_CONF_ZERO2));
}

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

static bool disable_devices(void)
{
	if (!send_ctrl_cmd(DISABLE_FIRST_PS2_PORT)) {
		printf("[ps2ctrl] ERROR: failed to disable first channel\n");
		return false;
	}

	// we don't know yet if the device is a single or dual channel
	// disabling the second channel will be ignored in the first case
	if (!send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		printf("[ps2ctrl] ERROR: failed to disable second channel\n");
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------

static void flush_controller_output_buffer(void)
{
	uint8_t ctrl_output_buffer_state;

	ctrl_output_buffer_state = inb(STATUS_PORT);

	if ((ctrl_output_buffer_state & SR_OUTPUT_BUFFER_STATUS) == 0) {
		printf("[ps2ctrl] controller output buffer is empty, skipping...\n");
		return;
	}

	printf("[ps2ctrl] controller output buffer is full, flushing...\n");
	inb(DATA_PORT); // ignoring the data (garbage)
}

// ----------------------------------------------------------------------------

/*
 * Read the current configuration byte, clear IRQs (both) and translation bits,
 * then write configuration back.
 *
 * Returns the modified configuration byte or -1 on error.
 */

static uint8_t set_controller_configuration_byte(void)
{
	uint8_t conf_byte;

	// read configuration byte
	if (!send_ctrl_cmd_with_response(READ_BYTE_0, &conf_byte)) {
		printf("[ps2ctrl] ERROR: failed to read configuration byte\n");
		return -1; // FIXME: is '-1' always a bad configuration ?
	}
	//dump_configuration_byte(conf_byte);

	// disable all IRQs and translation
	conf_byte &= ~(CTRL_CONF_FIRST_PS2_PORT_INTERRUPT);
	conf_byte &= ~(CTRL_CONF_SECOND_PS2_PORT_INTERRUPT);
	conf_byte &= ~(CTRL_CONF_FIRST_PS2_PORT_TRANSLATION);
	//dump_configuration_byte(conf_byte);

	// write configuration byte back
	if (!send_ctrl_cmd_with_data(WRITE_BYTE_0, conf_byte)) {
		printf("[ps2ctrl] ERROR: failed to write back configuration byte\n");
		return -1; // FIXME: is '-1' always a bad configuration ?
	}

	return conf_byte;
}

// ----------------------------------------------------------------------------

static bool check_controller_selt_test(void)
{
	uint8_t result;

	if (!send_ctrl_cmd_with_response(TEST_PS2_CONTROLLER, &result)) {
		printf("[ps2ctrl] ERROR: failed to send/receive test PS2 controller command/response\n");
	}

	// 0x55=test passed, 0xFC=test failed
	if (result != 0x55 && result != 0xFC) {
		printf("[ps2ctrl] ERROR: unexpected value (0x%x)\n", result);
		return false;
	}

	return (result == 0x55);
}

// ----------------------------------------------------------------------------

/*
 * Test if the controller handle dual-channel by enabling the second port and
 * checking its clock status from configuration byte.
 *
 * Returns 1 if there is two channels, 0 if there is one, -1 on error.
 */

static int has_two_channels(void)
{
	uint8_t conf_byte;

	if (!send_ctrl_cmd(ENABLE_SECOND_PS2_PORT)) {
		printf("[ps2ctrl] ERROR: failed to send enable second PS/2 port command\n");
		return -1;
	}

	// read controller configuration byte
	if (!send_ctrl_cmd_with_response(READ_BYTE_0, &conf_byte)) {
		printf("[ps2ctrl] ERROR: failed to read configuration byte\n");
		return -1;
	}

	// this should be clear (=enabled) on dual channel configuration
	if (conf_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK) {
		return 0;
	}

	printf("[ps2ctrl] dual channels controller detected\n");

	// re-disable the second channel for now
	if (!send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		printf("[ps2ctrl] ERROR: failed to send disabled second PS/2 port command\n");
		return -1;
	}

	return 1;
}

// ----------------------------------------------------------------------------

static bool check_single_interface_test(bool first_interface)
{
	uint8_t result = 0;
	const enum ctrl_command cmd =
		first_interface ? TEST_FIRST_PS2_PORT : TEST_SECOND_PS2_PORT;
	const char *name = first_interface ? "first" : "second";
	const char *reasons[] = {
		NULL, // 0 == success
		"clock line stuck low",
		"clock line stuck high",
		"data line stuck low",
		"data line stuck high"
	};

	if (!send_ctrl_cmd_with_response(cmd, &result)) {
		printf("[ps2ctrl] ERROR: failed to send 'test %s port' cmd\n", name);
		return false;
	} else if (result > 0x04) {
		printf("[ps2ctrl] ERROR: unknown test response (0x%x)\n", result);
		return false;
	} else if (result == 0x00) {
		printf("[ps2ctrl] testing %s interface succeed\n", name);
		return true;
	}

	printf("[ps2ctrl] testing %s interface failed, reason: %s\n",
		name, reasons[result]);
	return false;
}

// ----------------------------------------------------------------------------

/*
 * Test both (if any)  PS/2 Port(s). If we have a dual channel controller, then
 * both interface should passed the test (i.e. all or nothing).
 *
 * TODO: handle that only one interface succeed while being in a dual-channel mode.
 */

static bool check_interface_test(bool single_channel)
{
	if (!check_single_interface_test(true)) {
		return false;
	}

	if (single_channel) {
		printf("[ps2ctrl] skipping second interface test\n");
		return true;
	}

	return check_single_interface_test(false);
}

// ----------------------------------------------------------------------------

static bool enable_devices(bool single_channel, bool enable_irq)
{
	uint8_t conf_byte = 0;

	if (!send_ctrl_cmd(ENABLE_FIRST_PS2_PORT)) {
		printf("[ps2ctrl] ERROR: failed to enable first interface\n");
		return false;
	}
	printf("[ps2ctrl] first interface enabled\n");

	if (!single_channel) {
		if (!send_ctrl_cmd(ENABLE_SECOND_PS2_PORT)) {
			printf("[ps2ctrl] ERROR: failed to enable second interface\n");
			goto disable_first_interface;
		}
		printf("[ps2ctrl] second interface enabled\n");
	}

	if (!enable_irq) {
		// no need to enable IRQ, we are done
		return true;
	}

	if (!send_ctrl_cmd_with_response(READ_BYTE_0, &conf_byte)) {
		printf("[ps2ctrl] ERROR: failed to read configuration byte\n");
		goto disable_second_interface;
	}

	// enable IRQ(s)
	conf_byte |= CTRL_CONF_FIRST_PS2_PORT_INTERRUPT;
	if (!single_channel) {
		conf_byte |= CTRL_CONF_SECOND_PS2_PORT_INTERRUPT;
	}

	if (!send_ctrl_cmd_with_data(WRITE_BYTE_0, conf_byte)) {
		printf("[ps2ctrl] ERROR: failed to write configuration byte\n");
		goto disable_second_interface;
	}

	// XXX: should we read it again to test that it has been applied ?
	return true;

disable_second_interface:
	if (!single_channel && !send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		printf("[ps2ctrl] WARNING: failed to disable second interface\n");
	}

disable_first_interface:
	if (!send_ctrl_cmd(DISABLE_FIRST_PS2_PORT)) {
		printf("[ps2ctrl] WARNING: failed to disable first interface\n");
	}

	return false;
}

// ----------------------------------------------------------------------------

__attribute__ ((unused)) // FIXME: implement me
static bool send_byte_to_first_port(uint8_t data)
{
	data = data;

	// TODO

	return false;
}

// ----------------------------------------------------------------------------

__attribute__ ((unused)) // FIXME: implement me
static bool send_byte_to_second_port(uint8_t data)
{
	data = data;

	// TODO

	return false;
}

// ----------------------------------------------------------------------------

/*
 * This one is actually "trickier" than it looks! The reason being, that we need
 * to send AND receive data from devices. In order to do so, there is two ways:
 * polling and IRQs.
 *
 * However, during initialization, IRQ1/IRQ12 are masked out. So we can send data,
 * but then we don't know how to get the response. If we do some polling, there is
 * no way to distinct if data comes from first or second port. While this is
 * easier to do with IRQ.
 *
 * Maybe, defers it until IRQs are enabled or to devices responsability?
 *
 * XXX: Let's start coding the keyboard driver, we will see!
 */

static bool reset_devices(bool single_channel)
{
	single_channel = single_channel;

	// TODO: send 0xFF to each usable device. 0xFA=succeed, 0xFC=failure

	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the PS/2 8042 Controller. It assumes that:
 * - interrupts are disabled
 * - IRQ1 (keyboard) is masked
 * - IRQ12 (mouse) is masked
 * - controller is in an unknown state
 *
 * Returns zero on success, -1 otherwise.
 *
 * TODO: error handling (disable devices/interrupts if enabled)
 */

int ps2ctrl_init(void)
{
	uint8_t configuration_byte;
	bool single_channel = true;
	int ret = 0;

	if (ps2ctrl_initialized) {
		printf("[ps2ctrl] ERROR: PS/2 controller is already initialized\n");
		return -1;
	}

	printf("[ps2ctrl] starting initialization...\n");

	if (!disable_usb_legacy_support()) {
		printf("[ps2ctrl] ERROR: failed to disable USB legacy support\n");
		return -1;
	} else {
		printf("[ps2ctrl] USB legacy support disabled (fake)\n");
	}

	if (!ps2ctrl_exists()) {
		printf("[ps2ctrl] ERROR: PS/2 Controller does not exist\n");
		return -1;
	} else {
		printf("[ps2ctrl] PS/2 Controller exist (fake)\n");
	}

	if (!disable_devices()) {
		printf("[ps2ctrl] ERROR: failed to disable devices\n");
		return -1;
	} else {
		printf("[ps2ctrl] devices are disabled\n");
	}

	flush_controller_output_buffer();
	printf("[ps2ctrl] controller's output buffer is flushed\n");

	if ((configuration_byte = set_controller_configuration_byte()) == (uint8_t)-1) {
		printf("[ps2ctrl] ERROR: failed to set controller configuration byte\n");
		return -1;
	} else {
		printf("[ps2ctrl] controller's configuration byte set: 0x%x\n",
			configuration_byte);
	}

	// first channel number check
	single_channel = (configuration_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK);
	printf("[ps2ctrl] controller handles %s channel(s) (FIRST TEST)\n",
		single_channel ? "single" : "dual");

	if (!check_controller_selt_test()) {
		printf("[ps2ctrl] ERROR: failed to perform controller self test\n");
		return -1;
	} else {
		printf("[ps2ctrl] controller self test succeed\n");
	}

	// reset configuration byte as self-test can reset the controller
	if ((configuration_byte = set_controller_configuration_byte()) == (uint8_t)-1) {
		printf("[ps2ctrl] ERROR: failed to set controller configuration byte\n");
		return -1;
	} else {
		printf("[ps2ctrl] controller's configuration byte set: 0x%x\n", configuration_byte);
	}

	if (!single_channel) {
		if ((ret = has_two_channels()) < 0) {
			printf("[ps2ctrl] ERROR: failed to test dual-channel controller\n");
			return -1;
		} else if (ret == 0) {
			printf("[ps2ctrl] PS/2 Controller has only one channel\n");
			single_channel = true;
		} else {
			printf("[ps2ctrl] PS/2 Controller has two channels\n");
			// single_channel is already set to false
		}
	}

	if (!check_interface_test(single_channel)) {
		printf("[ps2ctrl] ERROR: interface(s) test failed\n");
		return -1;
	} else {
		printf("[ps2ctrl] interface(s) test succeed\n");
	}

	if (!enable_devices(single_channel, true)) {
		printf("[ps2ctrl] ERROR: failed to enable devices\n");
		return -1;
	} else {
		printf("[ps2ctrl] enabling devices succeed\n");
	}

	#if 0
	// XXX: defer it to device's responsability ?
	if (!reset_devices(single_channel)) {
		printf("[ps2ctrl] ERROR: failed to reset devices\n");
		return -1;
	} else {
		printf("[ps2ctrl] resetting devices succeed\n");
	}
	#endif

	ps2ctrl_initialized = true;
	ps2ctrl_single_channel = single_channel;

	printf("[ps2ctrl] initialization complete\n");

	return 0;
}

// ----------------------------------------------------------------------------

void ps2ctrl_irq1_handler(void)
{
	uint8_t data = 0;

	if (!ps2ctrl_initialized) {
		printf("[ps2ctrl] ERROR: PS/2 controller not initialized!\n");
		abort();
	}

	// no need to check 'output' status in Status Register (we come from IRQ)
	data = inb(DATA_PORT);
	printf("[ps2ctrl] IRQ1 handler: receveid data 0x%x\n", data);

	// FIXME: handle it

	irq_send_eoi(IRQ1_KEYBOARD);
}

// ----------------------------------------------------------------------------

void ps2ctrl_irq12_handler(void)
{
	if (!ps2ctrl_initialized) {
		printf("[ps2ctrl] ERROR: PS/2 controller not initialized!\n");
		abort();
	}

	if (!ps2ctrl_single_channel) {
		printf("[ps2ctrl] ERROR: PS/2 controller has a single channel!\n");
		abort();
	}

	// FIXME: handle it

	printf("[ps2ctrl] ERROR: NOT IMPLEMENTED\n");
	abort();

	irq_send_eoi(IRQ12_PS2_MOUSE);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
