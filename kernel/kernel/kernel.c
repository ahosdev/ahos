/*
 * kernel.c
 *
 * Kernel Entry Point.
 */

#include <kernel/init.h>
#include <kernel/log.h>
#include <kernel/scheduler.h>

#include <drivers/keyboard.h>
#include <drivers/clock.h>

#include <multiboot.h>
#include <stdio.h>

#define LOG_MODULE "main"

#define AHOS_VERSION_MAJOR 0
#define AHOS_VERSION_MINOR 2

#if defined(__linux__)
#error "You are not using a cross-compiler"
#endif

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void print_banner(void)
{
	printf("\n\n");
	printf("\t+===========================+\n");
	printf("\t|                           |\n");
	printf("\t| Welcome to Ah!OS (v%d.%.2d)! |\n", AHOS_VERSION_MAJOR,
												   AHOS_VERSION_MINOR);
	printf("\t|                           |\n");
	printf("\t+===========================+\n");
	printf("\n\n");
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
		sched_run_task(100, "keyboard", &keyboard_task);
	}

	info("kernel main loop stopped");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void kernel_main(uint32_t magic, multiboot_info_t *multiboot_info)
{
	kernel_early_init();
	// we can use log printing now

	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		error("kernel NOT booted from a MULTIBOOT (v1) compliant boot loader");
		return;
	} else {
		success("kernel booted from a MULTIBOOT (v1) compliant boot loader");
	}

	kernel_init(multiboot_info);

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
