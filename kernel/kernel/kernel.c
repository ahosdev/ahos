#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/memman.h>
#include <kernel/interrupt.h>
#include <kernel/clock.h>

#if defined(__linux__)
#error "You are not using a cross-compiler"
#endif

static void print_banner(void)
{
	printf("\n\n");
	printf("+====================+\n");
	printf("|                    |\n");
	printf("| Welcome to Ah!OS ! |\n");
	printf("|                    |\n");
	printf("+====================+\n");
	printf("\n\n");
}

static void kernel_init(void)
{
	memman_init();

	// initialise output early for debugging
	serial_init();
	terminal_initialize();

	setup_idt();
	irq_init(IRQ0_INT, IRQ7_INT);

	clock_init(CLOCK_FREQ);

	irq_clear_mask(IRQ0_CLOCK);
	//irq_clear_mask(IRQ1_KEYBOARD);

	// we can re-enable interrupts now
	enable_nmi();
	enable_irq();

	printf("kernel initialization complete\n");
}

void kernel_main(void)
{
	kernel_init();
	print_banner();

	printf("tick = %d\n", clock_gettick());

	printf("sleeping 3secs\n");
	clock_sleep(3000);

	printf("done!\n");
	printf("tick = %d\n", clock_gettick());

	for (;;) // do not quit yet, otherwise irq will be disabled
	{
		asm volatile("hlt" :: );
	}
}
