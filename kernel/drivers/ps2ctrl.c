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
 * WARNING: The "input/output" buffer are from the controller/devices
 * perspective. In other words, writing to controller/devices means writing
 * to the "input" buffer. On the other hand, reading from the controller/devices
 * means reading from the "output" buffer.
 *
 * Documentation:
 * - http://www.diakom.ru/el/elfirms/datashts/Smsc/42w11.pdf (datasheet)
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller
 * - https://wiki.osdev.org/PS/2_Keyboard
 *
 * TODO:
 * - handle second device (once we have one)
 */

#include <drivers/ps2ctrl.h>
#include <drivers/ps2driver.h>
#include <drivers/clock.h>

#include <arch/io.h>

#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/timeout.h>
#include <kernel/log.h>

#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "ps2ctrl"

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

#define PS2CTRL_MAX_DRIVERS 4

// true if a driver is registered in a slot
static bool registered_drivers[PS2CTRL_MAX_DRIVERS];
static struct ps2driver* drivers[PS2CTRL_MAX_DRIVERS];

static bool ps2ctrl_initialized = false;
static bool ps2ctrl_single_channel = true;

// installed drivers
static struct ps2driver * ps2_drivers[2] = {
	NULL, // first port
	NULL, // second port
};

typedef void (*ps2_irq_handler)(uint8_t data);
static ps2_irq_handler ps2_irq_handlers[2] = {
	NULL, // first port
	NULL, // second port
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Wait until the controller's input buffer is EMPTY.
 *
 * Returns true on success, false on time out.
 */

static bool wait_ctrl_input_buffer_ready(void)
{
	uint8_t status = 0;
	struct timeout timeo;

	timeout_init(&timeo, 200);
	timeout_start(&timeo);

	do {
		status = inb(STATUS_PORT);
		// TODO: implement timeout
	} while ((status & SR_INPUT_BUFFER_STATUS) && !timeout_expired(&timeo));

	if (status & SR_INPUT_BUFFER_STATUS) {
		warn("waiting control input buffer ready timed out");
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Wait until the controller's output buffer is FULL.
 *
 * Returns true on success, false on time out.
 */

static bool wait_ctrl_output_buffer_ready(void)
{
	uint8_t status = 0;
	struct timeout timeo;

	timeout_init(&timeo, 200);
	timeout_start(&timeo);

	do {
		status = inb(STATUS_PORT);
		// TODO: implement timeout
	} while (((status & SR_OUTPUT_BUFFER_STATUS) == 0) && !timeout_expired(&timeo));

	if ((status & SR_OUTPUT_BUFFER_STATUS) == 0) {
		warn("waiting control output buffer ready timed out");
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Send a command to the controller. If there is two bytes, then @data must not
 * be NULL. On the other hand, if the command expect a response, @response must
 * not be NULL.
 *
 * Returns true on success, false otherwise.
 */

static bool __send_ctrl_cmd(enum ctrl_command cmd, uint8_t *data, uint8_t *response)
{
	if (data && response) {
		error("there is no known command that send data and expect a response");
		return false;
	}

	if (!wait_ctrl_input_buffer_ready()) {
		error("failed to wait control input buffer ready");
		return false;
	}

	outb(CMD_PORT, (uint8_t) cmd);

	if (data) {
		if (!wait_ctrl_input_buffer_ready()) {
			error("failed to wait control input buffer ready");
			return false;
		}
		outb(DATA_PORT, *data);
	} else if (response) {
		if (!wait_ctrl_output_buffer_ready()) {
			error("failed to wait control output buffer ready");
			return false;
		}
		*response = inb(DATA_PORT);
	}

	return true;
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
	NOT_IMPLEMENTED();
	// TODO: implement me
}
#endif

// ----------------------------------------------------------------------------

__attribute__ ((unused)) // debugging function: skip compilation warning
static void dump_configuration_byte(uint8_t conf_byte)
{
	dbg("dumping configuration byte:");
	dbg("- first PS/2 port interrupt: %s",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_INTERRUPT) ? "enabled" : "disabled");
	dbg("- first PS/2 port clock: %s",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_CLOCK) ? "disabled" : "enabled");
	dbg("- first PS/2 port translation: %s",
		(conf_byte & CTRL_CONF_FIRST_PS2_PORT_TRANSLATION) ? "enabled" : "disabled");
	dbg("- second PS/2 port interrupt: %s",
		(conf_byte & CTRL_CONF_SECOND_PS2_PORT_INTERRUPT) ? "enabled" : "disabled");
	dbg("- second PS/2 port clock: %s",
		(conf_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK) ? "disabled" : "enabled");
	dbg("- system flag: %s",
		(conf_byte & CTRL_CONF_SYSTEM_FLAG) ? "system passed POST" : "ERROR");
	dbg("- zero0: %d", !!(conf_byte & CTRL_CONF_ZERO1));
	dbg("- zero1: %d", !!(conf_byte & CTRL_CONF_ZERO2));
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
		error("failed to disable first channel");
		return false;
	}

	// we don't know yet if the device is a single or dual channel
	// disabling the second channel will be ignored in the first case
	if (!send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		error("failed to disable second channel");
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
		dbg("controller output buffer is empty, skipping...");
		return;
	}

	dbg("controller output buffer is full, flushing...");
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
		error("failed to read configuration byte");
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
		error("failed to write back configuration byte");
		return -1; // FIXME: is '-1' always a bad configuration ?
	}

	return conf_byte;
}

// ----------------------------------------------------------------------------

static bool check_controller_selt_test(void)
{
	uint8_t result;

	if (!send_ctrl_cmd_with_response(TEST_PS2_CONTROLLER, &result)) {
		error("failed to send/receive test PS2 controller command/response");
	}

	// 0x55=test passed, 0xFC=test failed
	if (result != 0x55 && result != 0xFC) {
		error("unexpected value (0x%x)", result);
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
		error("failed to send enable second PS/2 port command");
		return -1;
	}

	// read controller configuration byte
	if (!send_ctrl_cmd_with_response(READ_BYTE_0, &conf_byte)) {
		error("failed to read configuration byte");
		return -1;
	}

	// this should be clear (=enabled) on dual channel configuration
	if (conf_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK) {
		return 0;
	}

	dbg("dual channels controller detected");

	// re-disable the second channel for now
	if (!send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		error("failed to send disabled second PS/2 port command");
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
		error("failed to send 'test %s port' cmd", name);
		return false;
	} else if (result > 0x04) {
		error("unknown test response (0x%x)", result);
		return false;
	} else if (result == 0x00) {
		dbg("testing %s interface succeed", name);
		return true;
	}

	warn("testing %s interface failed, reason: %s", name, reasons[result]);
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
		dbg("skipping second interface test");
		return true;
	}

	return check_single_interface_test(false);
}

// ----------------------------------------------------------------------------

static bool enable_devices(bool single_channel, bool enable_irq)
{
	uint8_t conf_byte = 0;

	if (!send_ctrl_cmd(ENABLE_FIRST_PS2_PORT)) {
		error("failed to enable first interface");
		return false;
	}
	dbg("first interface enabled");

	if (!single_channel) {
		if (!send_ctrl_cmd(ENABLE_SECOND_PS2_PORT)) {
			error("failed to enable second interface");
			goto disable_first_interface;
		}
		dbg("second interface enabled");
	}

	if (!enable_irq) {
		// no need to enable IRQ, we are done
		return true;
	}

	if (!send_ctrl_cmd_with_response(READ_BYTE_0, &conf_byte)) {
		error("failed to read configuration byte");
		goto disable_second_interface;
	}

	// enable IRQ(s)
	conf_byte |= CTRL_CONF_FIRST_PS2_PORT_INTERRUPT;
	if (!single_channel) {
		conf_byte |= CTRL_CONF_SECOND_PS2_PORT_INTERRUPT;
	}

	if (!send_ctrl_cmd_with_data(WRITE_BYTE_0, conf_byte)) {
		error("failed to write configuration byte");
		goto disable_second_interface;
	}

	// XXX: should we read it again to test that it has been applied ?
	return true;

disable_second_interface:
	if (!single_channel && !send_ctrl_cmd(DISABLE_SECOND_PS2_PORT)) {
		warn("failed to disable second interface");
	}

disable_first_interface:
	if (!send_ctrl_cmd(DISABLE_FIRST_PS2_PORT)) {
		warn("failed to disable first interface");
	}

	return false;
}

// ----------------------------------------------------------------------------

/*
 * Send a single byte to first device.
 *
 * Returns true on success, false otherwise.
 */

static bool send_byte_to_first_port(uint8_t data)
{
	struct timeout timeo;
	uint8_t status;

	timeout_init(&timeo, 200);
	timeout_start(&timeo);

	do {
		status = inb(STATUS_PORT);
	} while ((status & SR_INPUT_BUFFER_STATUS) && !timeout_expired(&timeo));

	if (status & SR_INPUT_BUFFER_STATUS) {
		// we timed out
		error("failed to send byte to first port");
		return false;
	}

	outb(DATA_PORT, data);
	dbg("sending byte to first port succeed");
	return true;
}

// ----------------------------------------------------------------------------

__attribute__ ((unused))
static bool send_byte_to_second_port(uint8_t data)
{
	data = data;

	NOT_IMPLEMENTED();

	// TODO: implement me

	return false;
}

// ----------------------------------------------------------------------------

/*
 * Receive a byte from first device by polling (i.e. sync).
 *
 * Returns true if a byte has been received and set @data, or false otherwise.
 *
 * On timeout, @data is untouched.
 */

static bool recv_byte_from_first_port_sync(uint8_t *data)
{
	struct timeout timeo;
	uint8_t status;

	timeout_init(&timeo, 200);
	timeout_start(&timeo);

	do {
		status = inb(STATUS_PORT);
	} while (((status & SR_OUTPUT_BUFFER_STATUS) == 0) && !timeout_expired(&timeo));
	// FIXME

	if ((status & SR_OUTPUT_BUFFER_STATUS) == 0) {
		// we timed out
		return false;
	}

	*data = inb(DATA_PORT);
	dbg("receiving byte from first device succeed (0x%x)", *data);
	return true;
}

// ----------------------------------------------------------------------------

/*
 * This one is actually "trickier" than it looks! The reason being, that we need
 * to send AND receive data from devices. In order to do so, there is two ways:
 * polling and IRQs.
 *
 * As we are only supporting a single channel for now, we do it with polling
 * (i.e. sync) while interrupts are disabled.
 */

static bool reset_devices(bool single_channel)
{
	int max_try = 3;
	uint8_t response = 0;

retry:
	if (max_try-- < 0) {
		error("failed to reset device (max try reached)");
		return false;
	}

	// send 'reset' command
	if (!send_byte_to_first_port(0xFF)) {
		error("failed to send 'reset' command to first device");
		goto retry;
	}

	// receive ACK, failure or no response
	if (!recv_byte_from_first_port_sync(&response)) {
		// no response
		warn("did not receive response for 'reset' command");
		goto retry;
	} else if (response == 0xFC) {
		error("received failure in response to 'reset' command");
		goto retry;
	} else if (response != 0xFA) {
		error("unknown response received");
	} else {
		// command ACK'ed. Now, selt-test has started. Receive the result.
		if (!recv_byte_from_first_port_sync(&response)) {
			error("device self-test failed");
			goto retry;
		}

		switch (response) {
			case 0xAA: goto next_device;
			case 0xFC: /* fallthrough */
			case 0xFD: /* fallthrough */
			case 0xFE: goto retry;
			default: {
				warn("unknown response (0x%x)", response);
				goto retry;
			}
		}
	}

next_device:
	if (!single_channel) {
		NOT_IMPLEMENTED();
	}

	return true;
}

// ----------------------------------------------------------------------------

static enum ps2_device_type device_type_from_id_bytes(uint8_t *bytes, uint8_t nbytes)
{
	if ((bytes == NULL) || (nbytes > 2)) {
		error("invalid argument");
		abort();
	}

	if (nbytes == 0) {
		return PS2_DEVICE_KEYBOARD_AT_WITH_TRANSLATION;
	} else if (nbytes == 1) {
		switch (bytes[0]) {
			case 0x00: return PS2_DEVICE_MOUSE_STD;
			case 0x03: return PS2_DEVICE_MOUSE_WITH_SCROLL_WHEEL;
			case 0x04: return PS2_DEVICE_MOUSE_5BUTTON;
		}
	} else if (nbytes == 2) {
		if (bytes[0] != 0xAB) {
			return PS2_DEVICE_UNKNOWN;
		}
		switch (bytes[1]) {
			case 0x41: /* fallthrough */
			case 0xC1: return PS2_DEVICE_KEYBOARD_MF2_WITH_TRANSLATION;
			case 0x83: return PS2_DEVICE_KEYBOARD_MF2;
		}
	}

	return PS2_DEVICE_UNKNOWN;
}

// ----------------------------------------------------------------------------

/*
 * Find a registered driver from device @type.
 *
 * Returns a pointer to the driver, NULL otherwise.
 */

static struct ps2driver* find_driver(enum ps2_device_type type)
{
	struct ps2driver *driver = NULL;

	if (type == PS2_DEVICE_UNKNOWN) {
		error("can't find a driver for an unknown device\n");
		return NULL;
	}

	for (size_t slot = 0; slot < PS2CTRL_MAX_DRIVERS; ++slot) {
		if (registered_drivers[slot] && drivers[slot]->type == type) {
			if (driver != NULL) {
				warn("found another driver candidate for this device\n");
			} else {
				driver = drivers[slot];
			}
		}
	}

	return driver;
}

// ----------------------------------------------------------------------------

/*
 * Installs the @driver on port number @port.
 *
 * Returns true on success, false otherwise.
 */

static bool install_driver(struct ps2driver *driver, uint8_t port)
{
	if (driver == NULL || port > 1) {
		error("invalid argument");
		return false;
	}

	if (ps2_drivers[port] != NULL) {
		error("a driver is already installed on that port");
		return false;
	}

	ps2_drivers[port] = driver;

	return true;
}

// ----------------------------------------------------------------------------

static inline bool ps2ctrl_input_buffer_empty(uint8_t status)
{
	return ((status & SR_INPUT_BUFFER_STATUS) == 0);
}

// ----------------------------------------------------------------------------

/*
 * Send @data byte to the data port.
 *
 * This will fail if the input buffer is still full after @timeout milleseconds.
 *
 * NOTE: It does not know if the data will ends up in the controller, first or
 * second port. It needs to be configured (via controller command) before hand.
 *
 * Returns true on success, false otherwise.
 */

static bool ps2ctrl_send_data(uint8_t data, size_t timeout)
{
	uint8_t status = 0;
	struct timeout timeo;
	size_t nb_tries = 0;

	timeout_init(&timeo, timeout);
	timeout_start(&timeo);

	do {
		// don't sleep on first try
		if (nb_tries++ > 0) {
			clock_sleep(20); // wait 20ms before retrying
		}
		status = inb(STATUS_PORT);
	} while (!ps2ctrl_input_buffer_empty(status) && !timeout_expired(&timeo));

	if (!ps2ctrl_input_buffer_empty(status)) {
		error("failed to send data: input buffer is full (timeout)");
		return false;
	}

	outb(DATA_PORT, data);

	// XXX: it's pointless to test the input buffer status right here. It can
	// be either full (unprocessed yet or re-filled by another thread) or empty
	// (already processed) when we test the status register.

	dbg("sending '0x%x' byte to input buffer succeed", data);
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Send @data byte to the first PS/2 input buffer.
 *
 * Returns true on success, false otherwise.
 */

static bool ps2ctrl_send_data_first_port(uint8_t data, size_t timeout)
{
	dbg("sending data (0x%x) to first PS/2 input buffer...", data);

	if (ps2ctrl_send_data(data, timeout) == false) {
		error("failed to send data to the first PS/2 input buffer");
		return false;
	}

	dbg("sending data (0x%x) to first PS/2 input buffer succeed", data);
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Send @data byte to the second PS/2 input buffer.
 *
 * Unlike the first PS/2 input buffer, it needs to issue a "write next byte to
 * second PS/2 port" command before sending data.
 *
 * WARNING: This code is untested for now (don't have a second device).
 *
 * Returns true on success, false otherwise.
 */

static bool ps2ctrl_send_data_second_port(uint8_t data, size_t timeout)
{
	dbg("sending data (0x%x) to second PS/2 input buffer...", data);

	// we cannot test this code for now as we don't have a second PS/2 device
	UNTESTED_CODE();

	if (ps2ctrl_single_channel) {
		error("cannot send data to second port on a single channel controller");
		return false;
	}

	// TODO: the timeout must be parametrable
	if (send_ctrl_cmd(WRITE_BYTE_SECOND_PS2_INPUT_PORT) == false) {
		error("failed to send 'write to second input buffer' command");
		return false;
	}

	if (ps2ctrl_send_data(data, timeout) == false) {
		error("failed to send data to the second PS/2 input buffer");
		return false;
	}

	dbg("sending data (0x%x) to second PS/2 input buffer succeed", data);
	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the PS/2 8042 Controller.
 *
 * It assumes that:
 * - interrupts are enabled (required by timeouts)
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
		error("PS/2 controller is already initialized");
		return -1;
	}

	info("initializing PS/2 controller...");

	// clear the driver list
	memset(drivers, 0, sizeof(drivers));
	memset(registered_drivers, 0, sizeof(registered_drivers));

	if (!disable_usb_legacy_support()) {
		error("failed to disable USB legacy support");
		return -1;
	} else {
		dbg("USB legacy support disabled (fake)");
	}

	if (!ps2ctrl_exists()) {
		error("PS/2 Controller does not exist");
		return -1;
	} else {
		dbg("PS/2 Controller exist (fake)");
	}

	if (!disable_devices()) {
		error("failed to disable devices");
		return -1;
	} else {
		dbg("devices are disabled");
	}

	flush_controller_output_buffer();
	dbg("controller's output buffer is flushed");

	if ((configuration_byte = set_controller_configuration_byte()) == (uint8_t)-1) {
		error("failed to set controller configuration byte");
		return -1;
	} else {
		dbg("controller's configuration byte set: 0x%x", configuration_byte);
	}

	// first channel number check
	single_channel = (configuration_byte & CTRL_CONF_SECOND_PS2_PORT_CLOCK);
	dbg("controller handles %s channel(s) (FIRST TEST)",
		single_channel ? "single" : "dual");

	if (!check_controller_selt_test()) {
		error("failed to perform controller self test");
		return -1;
	} else {
		dbg("controller self test succeed");
	}

	// reset configuration byte as self-test can reset the controller
	if ((configuration_byte = set_controller_configuration_byte()) == (uint8_t)-1) {
		error("failed to set controller configuration byte");
		return -1;
	} else {
		dbg("controller's configuration byte set: 0x%x", configuration_byte);
	}

	if (!single_channel) {
		if ((ret = has_two_channels()) < 0) {
			error("failed to test dual-channel controller");
			return -1;
		} else if (ret == 0) {
			dbg("PS/2 Controller has only one channel");
			single_channel = true;
		} else {
			dbg("PS/2 Controller has two channels");
			// single_channel is already set to false
		}
	}

	if (!check_interface_test(single_channel)) {
		error("interface(s) test failed");
		return -1;
	} else {
		dbg("interface(s) test succeed");
	}

	if (!enable_devices(single_channel, true)) {
		error("failed to enable devices");
		return -1;
	} else {
		dbg("enabling devices succeed");
	}

	if (!reset_devices(single_channel)) { // XXX: do it during identification ?
		error("failed to reset devices");
		return -1;
	} else {
		dbg("resetting devices succeed");
	}

	ps2ctrl_initialized = true;
	ps2ctrl_single_channel = single_channel;

	success("PS/2 controller initialization complete");

	return 0;
}

// ----------------------------------------------------------------------------

/*
 * Identify devices plugged to the PS/2 controller.
 *
 * It should be invoked after interrupts has been enabled but IRQ1/IRQ12 are
 * still masked out.
 */

bool ps2ctrl_identify_devices(void)
{
	uint8_t identify_bytes[2];
	uint8_t identify_nbytes;
	uint8_t data;
	struct timeout timeo;
	enum ps2_device_type device_type;
	struct ps2driver *driver = NULL;

	info("identifying devices...");

	if (!ps2ctrl_initialized) {
		error("PS/2 controller isn't initialized");
		return false;
	}

	// send "disable scanning" to first device
	if (!send_byte_to_first_port(0xF5)) {
		error("failed to send 'disable scanning' command to first device");
		return false;
	}

	// wait for device to send "ACK" back
	if (!recv_byte_from_first_port_sync(&data) || (data != 0xFA)) {
		error("failed to received ACK from first device");
		return false;
	}
	// FIXME: re-send 'disable scanning' command if device sent "resend" (0xFE)

	// send "identify to device
	if (!send_byte_to_first_port(0xF2)) {
		error("failed to send 'identify' command to first device");
		return false;
	}

	// wait for device to send "ACK" back
	if (!recv_byte_from_first_port_sync(&data) || (data != 0xFA)) {
		error("failed to received ACK from first device");
		return false;
	}
	// FIXME: re-send 'identify' command if device sent "resend" (0xFE)

	// wait for device to send 0, 1 or 2 bytes with timeout
	memset(&identify_bytes, 0, sizeof(identify_bytes));
	identify_nbytes = 0;
	timeout_init(&timeo, 1000);
	timeout_start(&timeo);

	do {
		if (recv_byte_from_first_port_sync(&identify_bytes[identify_nbytes])) {
			// we received a byte
			identify_nbytes++;
		}
	} while ((identify_nbytes < 2) && !timeout_expired(&timeo));

	dbg("received %u identification bytes from first device", identify_nbytes);

	// identify keyboard from identification bytes
	device_type = device_type_from_id_bytes(identify_bytes, identify_nbytes);
	if (device_type == PS2_DEVICE_UNKNOWN) {
		error("failed to identify device type from identification code");
		return false;
	}
	info("device on port 0 has been identified (type = 0x%u)", device_type);

	if ((driver = find_driver(device_type)) == NULL) {
		error("no driver found for device type (0x%x)", device_type);
		return false;
	}
	dbg("driver found <%s>", driver->name);

	if (install_driver(driver, 0) == false) {
		error("failed to install driver <%s> on port 0", driver->name);
		return false;
	}
	info("driver <%s> successfully installed", driver->name);

	if (!ps2ctrl_single_channel) {
		// TODO: identify second port device (if any)
		NOT_IMPLEMENTED();
	}

	success("devices identification complete");
	return true;
}

// ----------------------------------------------------------------------------

void ps2ctrl_irq1_handler(void)
{
	uint8_t data = 0;

	if (!ps2ctrl_initialized) {
		error("PS/2 controller not initialized!");
		abort();
	}

	// no need to check 'output' status in Status Register (we come from IRQ)
	data = inb(DATA_PORT);
	//info("IRQ1 handler: receveid data 0x%x", data); // debug only

	if (ps2_irq_handlers[0] == NULL) {
		error("IRQ1 does not have an associated handler, data is lost!");
	} else {
		ps2_irq_handlers[0](data);
	}

	irq_send_eoi(IRQ1_KEYBOARD);
}

// ----------------------------------------------------------------------------

void ps2ctrl_irq12_handler(void)
{
	uint8_t data = 0;

	if (!ps2ctrl_initialized) {
		error("PS/2 controller not initialized!");
		abort();
	}

	if (!ps2ctrl_single_channel) {
		// XXX: this should never happend since the IRQ is cleared during
		// driver start up.
		error("PS/2 controller has a single channel!");
		abort();
	}

	// no need to check 'output' status in Status Register (we come from IRQ)
	data = inb(DATA_PORT);
	//info("IRQ12 handler: receveid data 0x%x", data); // debug only

	if (ps2_irq_handlers[1] == NULL) {
		error("IRQ12 does not have an associated handler, data is lost!");
	} else {
		ps2_irq_handlers[1](data);
	}

	irq_send_eoi(IRQ12_PS2_MOUSE);
}

// ----------------------------------------------------------------------------

/*
 * Reset the CPU using the PS/2 Controller (an extra feature).
 *
 * This will either failed (return false) or succeed. In the later case, this
 * is a noreturn function.
 */

bool ps2ctrl_cpu_reset(void)
{
	info("resetting cpu");

	if (!wait_ctrl_input_buffer_ready()) {
		error("cannot cpu reset: input buffer is full");
		return false; // XXX: try it anyway ?
	}

	outb(CMD_PORT, 0xFE);

	/* no return */
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Registers a PS/2 driver into the driver list.
 *
 * Returns true on success, false otherwise.
 */

bool ps2ctrl_register_driver(struct ps2driver *driver)
{
	size_t slot = 0;

	if (driver == NULL) {
		error("invalid argument");
		return false;
	}

	if (ps2ctrl_initialized == false) {
		error(" PS/2 controller is not ready yet");
		return false;
	}

	// check if the driver is not already registered (by name for now)
	for (slot = 0; slot < PS2CTRL_MAX_DRIVERS; ++slot) {
		if (registered_drivers[slot] == false) {
			continue;
		} else if (driver == drivers[slot]) {
			warn("this driver is already registered");
			return false;
		} else if (!strcmp(driver->name, drivers[slot]->name)) {
			warn("a driver with that name is already registred");
			return false;
		}
	}

	// find an empty slot
	for (slot = 0; slot < PS2CTRL_MAX_DRIVERS; ++slot) {
		if (registered_drivers[slot] == false) {
			break;
		}
	}

	if (slot == PS2CTRL_MAX_DRIVERS) {
		error("no drivers slot available");
		return false;
	}

	// everything is fine, register it.
	drivers[slot] = driver;
	registered_drivers[slot] = true;

	success("driver <%s> registered at slot %u", driver->name, slot);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Starts drivers that has been installed. IRQ are cleared for ports which have
 * an associated driver. In addition, the driver's send() callback is set based
 * on the port where the driver has been installed.
 *
 * Returns true on success, false otherwise.
 */

bool ps2ctrl_start_drivers(void)
{
	if (ps2ctrl_initialized == false) {
		error("PS/2 controller isn't initialized");
		return false;
	}

	info("starting PS/2 drivers...");

	// there is only two possible port on a i8042.
	for (size_t port = 0; port < 2; ++port) {
		uint8_t irq_line = port == 0 ? IRQ1_KEYBOARD : IRQ12_PS2_MOUSE;
		struct ps2driver *driver = ps2_drivers[port];

		if (driver == NULL) {
			info("no driver installed on port %u, skipping...", port);
			continue;
		}

		// setup IRQ before starting the driver (it eventually needs it)
		if (driver->recv == NULL) {
			warn("driver <%s> does not have an IRQ handler", driver->name);
		} else {
			ps2_irq_handlers[port] = driver->recv;
		}
		info("enabling IRQ line %u...", irq_line);
		irq_clear_mask(irq_line);

		// route the send() callback to the proper port
		if (driver->send != NULL) {
			warn("overwriting an existing send() callback!");
		}
		driver->send = (port == 0) ?
			&ps2ctrl_send_data_first_port :
			&ps2ctrl_send_data_second_port;
		dbg("driver send() callback set");

		// start the driver
		if (driver->start == NULL) {
			warn("driver has no start function");
			continue;
		} else if (driver->start(irq_line) == false) {
			error("failed to start driver <%s> on IRQ line %u",
				driver->name, irq_line);
			// FIXME: error handling (disable IRQ? other driver?)
			return false;
		} else {
			info("starting driver <%s> with IRQ line %u succeed",
				driver->name, irq_line);
		}
	}

	success("PS/2 drivers successfully started");

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
