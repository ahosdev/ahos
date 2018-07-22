#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/memman.h>
#include <kernel/interrupt.h>

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

	printf("kernel initialization complete\n");

#if 0
	// enable interrupts now
	enable_nmi();
	enable_irq();
#endif
}

void kernel_main(void)
{
	kernel_init();
	print_banner();
}
