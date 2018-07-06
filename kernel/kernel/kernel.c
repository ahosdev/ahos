#include <stdio.h>

#include <kernel/tty.h>

static void print_banner(void)
{
	printf("\n\n");
	printf("+====================+\n");
	printf("|                    |\n");
	printf("| Welcome to Ah!OS ! |\n");
	printf("|                    |\n");
	printf("======================\n");
	printf("\n\n");
}

void kernel_main(void) {
	terminal_initialize();

	print_banner();
}
