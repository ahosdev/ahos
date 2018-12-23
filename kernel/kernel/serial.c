/*
 * serial.c
 *
 * Serial Driver (8250 UART).
 *
 * Recommended readings:
 * - https://wiki.osdev.org/UART
 * - https://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming
 *
 * TODO:
 * - detect UART controller
 * - handle 64 bytes fifo
 * - asynchronous writing
 * - error handling (LSR)
 */

#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <stdlib.h>

// ----------------------------------------------------------------------------

#define COM1 0x3f8
#define COM2 0x2f8
#define COM3 0x3e8
#define COM4 0x2e8

// ----------------------------------------------------------------------------

// UART has 12 registers mapped over 8 I/O ports. Accessing them might depends
// on the Divisor Latch Byte (DLAB) status or the access type (read or write).
#define THR 0x0 // Transmit Holding Buffer (write + no dlab)
#define RBR 0x0 // Receive Buffer (read + no dlab)
#define DLL 0x0 // Divisor Latch Low Byte (read/write + dlab)
#define IER 0x1 // Interrupt Enable Register (read/write + no dlab)
#define DLH 0x1 // Divisor Latch High Byte (read/write + dlab)
#define IIR 0x2 // Interrupt Identification Register (read)
#define FCR 0x2 // FIFO Control Register (write)
#define LCR 0x3 // Line Control Register (read/write)
#define MCR 0x4 // Modem Control Register (read/write)
#define LSR 0x5 // Line Status Register (read)
#define MSR 0x6 // Modem Status Register (read)
#define SR  0x7 // Scratch Register (read/write)

// ----------------------------------------------------------------------------

// Interrupt Enable Register (IER)
#define IER_RECEIVED_DATA_AVAILABLE_INT_MASK 		(1 << 0)
#define IER_TRANSMITTER_HOLDING_REGISTER_EMPTY_INT_MASK (1 << 1)
#define IER_RECEIVER_LINE_STATUS_INT_MASK 		(1 << 2)
#define IER_MODEM_STATUS_INT_MASK 			(1 << 3)
#define IER_SLEEP_MODE 					(1 << 4)
#define IER_LOW_POWER_MODE_MASK 			(1 << 5)
#define IER_RESERVED6_MASK 				(1 << 6)
#define IER_RESERVED7_MASK 				(1 << 7)

// ----------------------------------------------------------------------------

// Interrupt Identification Register (IIR)
#define IIR_INT_PENDING_FLAG_MASK 	(1 << 0)
#define IIR_INT_REASONS_MASK 		(7 << 1)
#define IIR_RESERVED4_MASK 		(1 << 4)
#define IIR_64BYTES_FIFO_MASK 		(1 << 5)
#define IIR_FIFO_STATUS_MASK 		(3 << 6)

#define IIR_INT_MODEM_STATUS_INTERRUPT 			(0 << 1)
#define IIR_INT_TRANSMITTER_HOLDING_REGISTER_EMPTY 	(1 << 1)
#define IIR_INT_RECEIVED_DATA_AVAILABLE 		(2 << 1)
#define IIR_INT_RECEIVER_LINE_STATUS 			(3 << 1)
#define IIR_INT_TIMEOUT 				(6 << 1)

#define IIR_FIFO_NOFIFO (0 << 6)
#define IIR_FIFO_RESERVED1 (1 << 6)
#define IIR_FIFO_ENABLED_NOT_FUNCTIONING (2 << 6)
#define IIR_FIFO_ENABLED (3 << 6)

// ----------------------------------------------------------------------------

// FIFO Control Register (FCR)
#define FCR_ENABLE_FIFOS_MASK 		(1 << 0)
#define FCR_CLEAR_RECEIVE_FIFO_MASK 	(1 << 1)
#define FCR_CLEAR_TRANSMIT_FIFO_MASK 	(1 << 2)
#define FCR_DMA_MODE_SELECT_MASK 	(1 << 3)
#define FCR_RESERVED4_MASK		(1 << 4)
#define FCR_64BYTE_FIFO_MASK		(1 << 5)
#define FCR_TRIGGER_LEVEL_MASK		(3 << 6)

// 64-bytes fifo disabled
#define FCR_INT16_1BYTE 	(0 << 6)
#define FCR_INT16_4BYTES 	(1 << 6)
#define FCR_INT16_8BYTES 	(2 << 6)
#define FCR_INT16_14BYTES	(3 << 6)

// 64-bytes fifo enabled
#define FCR_INT64_1BYTE		(0 << 6)
#define FCR_INT64_16BYTES	(1 << 6)
#define FCR_INT64_32BYTES	(2 << 6)
#define FCT_INT64_56BYTES	(3 << 6)

// ----------------------------------------------------------------------------

// Line Control Register (LCR)
#define LCR_WORD_LEN_MASK 	(3 << 0)
#define LCR_STOPBIT_MASK 	(1 << 2)
#define LCR_PARITY_MASK		(7 << 3)
#define LCR_BREAK_ENABLE_MASK	(1 << 6)
#define LCR_DLAB_MASK		(1 << 7)

#define LCR_WORD_LEN_5BITS 	(0 << 0)
#define LCR_WORD_LEN_6BITS	(1 << 0)
#define LCR_WORD_LEN_7BITS 	(2 << 0)
#define LCR_WORD_LEN_8BITS 	(3 << 0)
#define LCR_STOPBIT_ONE		(0 << 2)
#define LCR_STOPBIT_TWO  	(1 << 2) // might be 1.5 bits
#define LCR_PARITY_NO    	(0 << 3)
#define LCR_PARITY_ODD   	(1 << 3)
#define LCR_PARITY_EVEN  	(3 << 3)
#define LCR_PARITY_MARK  	(5 << 3)
#define LCR_PARITY_SPACE 	(7 << 3)

#define LCR_PROTO_8N1 (LCR_WORD_LEN_8BITS | LCR_PARITY_NO | LCR_STOPBIT_ONE)

// ----------------------------------------------------------------------------

// Modem Control Register (MCR)
#define MCR_DATA_TERMINAL_READY_MASK	(1 << 0)
#define MCR_REQUEST_TO_SEND_MASK	(1 << 1)
#define MCR_AUX_OUT1_MASK		(1 << 2)
#define MCR_AUX_OUT2_MASK		(1 << 3)
#define MCR_LOOPBACK_MODE		(1 << 4)
#define MCR_AUTOFLOW_CONTROL_MASK	(1 << 5)
#define MCR_RESERVED6_MASK		(1 << 6)
#define MCR_RESERVED7_MASK		(1 << 7)

// ----------------------------------------------------------------------------

// Line Status Register (LSR)
#define LSR_DATA_READY_MASK 				(1 << 0)
#define LSR_OVERRUN_ERROR_MASK				(1 << 1)
#define LSR_PARITY_ERROR_MASK				(1 << 2)
#define LSR_FRAMING_ERROR_MASK				(1 << 3)
#define LSR_BREAK_INT_MASK				(1 << 4)
#define LSR_EMPTY_TRANSMITTER_HOLDING_REGISTER_MASK 	(1 << 5)
#define LSR_EMPTY_DATA_HOLDING_REGISTERS_MASK 		(1 << 6)
#define LSR_ERROR_IN_RECEIVED_FIFO_MASK 		(1 << 7)

// ----------------------------------------------------------------------------

// Modem Status Register (MSR)
#define MSR_DELTA_CLEAR_TO_SEND_MASK 		(1 << 0)
#define MSR_DELTA_DATA_SET_READY_MASK		(1 << 1)
#define MSR_TRAILING_EDGE_RING_INDICATOR_MASK 	(1 << 2)
#define MSR_DELTA_DATA_CARRIER_DETECT_MASK	(1 << 3)
#define MSR_CLEAR_TO_SEND_MASK			(1 << 4)
#define MSR_DATA_SET_READY_MASK			(1 << 5)
#define MSR_RING_INDICATOR_MASK			(1 << 6)
#define MSR_CARRIER_DETECT_MASK			(1 << 7)

// ----------------------------------------------------------------------------

/*
 * Assumes that UART interruptions are disabled.
 */

static void serial_set_baud_rate(uint32_t rate)
{
	uint16_t divisor_latch_value = 0;
	uint8_t lcr = 0;

	// prevent division-by-zero and zero divisor latch bytes
	if ((rate == 0) || (rate > 115200))
		abort();

	// enable dlab
	lcr = inb(COM1 + LCR);
	lcr |= LCR_DLAB_MASK;
	outb(COM1 + LCR, lcr);

	// set dlab latch bytes
	divisor_latch_value = 115200 / rate;
	outb(COM1 + DLL, divisor_latch_value & 0xff);
	outb(COM1 + DLH, (divisor_latch_value >> 8) & 0xff);

	// disable dlab
	lcr &= ~LCR_DLAB_MASK;
	outb(COM1 + LCR, lcr);
}

/*
 * Assumes DLAB is disabled.
 */

static void serial_disable_irqs(void)
{
	outb(COM1 + IER, 0);
}

/*
 * Assumes DLAB is disabled.
 */

static void serial_enable_irqs(void)
{
	uint8_t ier = 0;

	ier |= IER_RECEIVED_DATA_AVAILABLE_INT_MASK;
	ier |= IER_TRANSMITTER_HOLDING_REGISTER_EMPTY_INT_MASK; // necessary ?

	outb(COM1 + IER, ier);
}

static void serial_set_protocol(uint8_t protocol)
{
	uint8_t lcr = 0;

	// keep dlab + break enable untouched
	lcr = inb(COM1 + LCR);
	lcr = protocol | (lcr & (LCR_DLAB_MASK|LCR_BREAK_ENABLE_MASK));

	outb(COM1 + LCR, lcr);
}

static void serial_enable_fifo(void)
{
	uint8_t fcr = 0;

	fcr |= FCR_ENABLE_FIFOS_MASK;
	// clear fifo bits will be automatically reset to 0 by the controller afterwards.
	fcr |= FCR_CLEAR_RECEIVE_FIFO_MASK | FCR_CLEAR_TRANSMIT_FIFO_MASK;
	// trigger an interrupt when a fifo exceed those size
	fcr |= FCR_INT16_14BYTES;

	outb(COM1 + FCR, fcr);
}

void serial_init(void)
{
	serial_disable_irqs();
	serial_set_baud_rate(38400);
	serial_set_protocol(LCR_PROTO_8N1);
	serial_enable_fifo();
	serial_enable_irqs();
}

void serial_write(const char *data, size_t size)
{
	for (size_t i = 0; i < size; ++i)
		outb(COM1 + THR, data[i]);
}
