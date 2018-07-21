#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/serial.h>
#include <kernel/memman.h>

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
	serial_init();
	terminal_initialize();
	printf("tty initialized in VGA text mode\n");

	memman_init();

	// TODO: init serial driver
}

void kernel_main(void)
{
	kernel_init();
	print_banner();
}
