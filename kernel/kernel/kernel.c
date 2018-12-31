/*
 * kernel.c
 *
 * Kernel Entry Point.
 */

#include <stdio.h>

#include <drivers/serial.h>
#include <drivers/clock.h>
#include <drivers/ps2ctrl.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>

#include <kernel/memman.h>
#include <kernel/interrupt.h>
#include <kernel/log.h>
#include <kernel/scheduler.h>

#include <mem/memory.h>

#include <multiboot.h>

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

/*
 * Setup early subsystems (gdt, serial driver, terminal driver) which provides
 * a minimal environment to access physical memory and print debug/boot
 * information.
 */

static void kernel_early_init(void)
{
	// XXX: interrupts are already disabled by the bootloader

	memman_init();

	// initialise output early for debugging
	serial_init();
	terminal_initialize();
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

static void kernel_init(multiboot_info_t *mbi)
{
	if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
		if (phys_mem_map_init(mbi->mmap_addr, mbi->mmap_length) == false)
		{
			error("failed to initialize memory map");
			abort();
		}
	} else {
		error("no memory map from multiboot info, cannot initialize memory");
		abort();
	}
	// we cannot use 'mbi' past this point (it is sitting in available memory)

	if (pfa_init() == false) {
		error("failed to init the page frame allocator");
		abort();
	}

	setup_idt();
	info("IDT setup");

	irq_init(IRQ0_INT, IRQ7_INT); // TODO: move it into setup_idt()
	info("IRQ initialized");

	clock_init(CLOCK_FREQ);
	info("clock initialized");

	// we can re-enable interrupts now
	info("enabling interrupts now");
	enable_nmi();
	enable_interrupts();

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
