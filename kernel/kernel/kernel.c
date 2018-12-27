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

#undef LOG_MODULE
#define LOG_MODULE "main"

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

static void ps2_init(void)
{
	info("starting PS/2 subsystem initialization...");

	ps2ctrl_init();
	keyboard_init();

	info("starting PS/2 device identification...");
	if (ps2ctrl_identify_devices() == false) {
		error("failed to identify PS/2 devices");
		return;
	}
	success("PS/2 devices identification succeed");

	info("starting PS/2 drivers...");
	if (ps2ctrl_start_drivers() == false) {
		error("failed to start PS/2 device drivers");
		return;
	}
	success("PS/2 device drivers started");

	success("PS/2 subsystem initialization complete");
}

// ----------------------------------------------------------------------------

static void kernel_init(void)
{
	memman_init();

	// initialise output early for debugging
	serial_init();
	terminal_initialize();
	info("kernel booting...");

	setup_idt();
	info("IDT setup");

	irq_init(IRQ0_INT, IRQ7_INT);
	info("IRQ initialized");

	clock_init(CLOCK_FREQ);
	info("clock initialized");

	// we can re-enable interrupts now
	info("enabling interrupts now");
	enable_nmi();
	enable_irq();

	ps2_init();

	success("kernel initialization complete");
}

// ----------------------------------------------------------------------------

/*
 * The kernel main loop.
 *
 * Since we don't handle task (and thus scheduler) we call any subsystem on a
 * regular basis in a sequential way. Think of it as a "cheap fake scheduler".
 */

static void kernel_main_loop(void)
{
	info("starting kernel main loop");

	for (;;) {
		// FIXME
	}

	info("kernel main loop stopped");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void kernel_main(void)
{
	kernel_init();
	print_banner();

	// it only accounts from the clock initialization
	info("kernel booted in %d tick(s)", clock_gettick());

	kernel_main_loop();

	for (;;) // do not quit yet, otherwise irq will be disabled
	{
		asm volatile("hlt" :: );
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
