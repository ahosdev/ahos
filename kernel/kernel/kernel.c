/*
 * kernel.c
 *
 * Kernel Entry Point.
 */

#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/memman.h>
#include <kernel/interrupt.h>
#include <kernel/clock.h>
#include <kernel/ps2ctrl.h>
#include <kernel/timeout.h>
#include <kernel/keyboard.h>
#include <kernel/log.h>

#if defined(__linux__)
#error "You are not using a cross-compiler"
#endif

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void print_banner(void)
{
	printf("\n\n");
	printf("\t+====================+\n");
	printf("\t|                    |\n");
	printf("\t| Welcome to Ah!OS ! |\n");
	printf("\t|                    |\n");
	printf("\t+====================+\n");
	printf("\n\n");
}

// ----------------------------------------------------------------------------

static void kernel_init(void)
{
	memman_init();

	// initialise output early for debugging
	serial_init();
	terminal_initialize();

	setup_idt();
	irq_init(IRQ0_INT, IRQ7_INT);

	clock_init(CLOCK_FREQ);

	// we can re-enable interrupts now
	info("enabling interrupts now");
	enable_nmi();
	enable_irq();

	// Initialize PS/2 controllers and devices
	ps2ctrl_init();
	keyboard_init();
	if (ps2ctrl_identify_devices() == false) {
		error("ERROR: failed to identify PS/2 devices");
	} else {
		success("PS/2 devices identification succeed");
	}

	success("kernel initialization complete");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void kernel_main(void)
{
	struct timeout timeo;

	kernel_init();
	print_banner();

	info("tick = %d", clock_gettick());

	clock_sleep(3);

	timeout_init(&timeo, 2000);
	info("starting timeout now");

	timeout_start(&timeo);
	do {
		info("waiting timeout to expire...");
		clock_sleep(400); // sleep 400ms
	} while (!timeout_expired(&timeo));

	success("timeout expired");

	for (;;) // do not quit yet, otherwise irq will be disabled
	{
		asm volatile("hlt" :: );
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
