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
	ps2ctrl_init();

	// XXX: devices should be responsible to enable their own IRQ?
	//irq_clear_mask(IRQ1_KEYBOARD);

	// we can re-enable interrupts now
	printf("enabling interrupts now\n");
	enable_nmi();
	enable_irq();

	printf("kernel initialization complete\n");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void kernel_main(void)
{
	struct timeout timeo;

	kernel_init();
	print_banner();

	printf("tick = %d\n", clock_gettick());

	clock_sleep(3);

	timeout_init(&timeo, 2000);
	printf("starting timeout now\n");

	timeout_start(&timeo);
	do {
		printf("waiting timeout to expire...\n");
		clock_sleep(400); // sleep 400ms
	} while (!timeout_expired(&timeo));

	printf("timeout expired\n");

	for (;;) // do not quit yet, otherwise irq will be disabled
	{
		asm volatile("hlt" :: );
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
