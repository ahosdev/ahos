/*
 * init.c
 *
 * Kernel initialization code.
 */

#include <kernel/init.h>
#include <kernel/interrupt.h>
#include <kernel/log.h>
#include <kernel/symbol.h>

#include <drivers/serial.h>
#include <drivers/clock.h>
#include <drivers/ps2ctrl.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>

#include <mem/memory.h>
#include <mem/pmm.h>

#include <arch/gdt.h>

#define LOG_MODULE "init"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void ps2_init(void)
{
	info("starting PS/2 subsystem initialization...");

	ps2ctrl_init();
	keyboard_init();

	dbg("starting PS/2 device identification...");
	if (ps2ctrl_identify_devices() == false) {
		error("failed to identify PS/2 devices");
		return;
	}
	dbg("PS/2 devices identification succeed");

	dbg("starting PS/2 drivers...");
	if (ps2ctrl_start_drivers() == false) {
		error("failed to start PS/2 device drivers");
		return;
	}
	dbg("PS/2 device drivers started");

	success("PS/2 subsystem initialization complete");
}

// ----------------------------------------------------------------------------

static void mem_init(multiboot_info_t *mbi)
{
	info("initializing memory...");

	if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
		if (phys_mem_map_init(mbi) == false)
		{
			panic("failed to initialize memory map");
		}
	} else {
		panic("no memory map from multiboot info, cannot initialize memory");
	}
	// we cannot use 'mbi' past this point (it is sitting in available memory)

	if (pfa_init() == false) {
		panic("failed to init the page frame allocator");
	}

	// the page frame allocator is ready, we can now setup paging
	paging_setup();

	success("memory initialization complete");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Setup early subsystems (gdt, serial driver, terminal driver) which provides
 * a minimal environment to access physical memory (i.e. GDT) and print
 * debug/boot information.
 */

void kernel_early_init(void)
{
	// XXX: interrupts are already disabled by the bootloader

	gdt_setup();

	// initialise output early for debugging
	serial_init();
	terminal_initialize();
}

// ----------------------------------------------------------------------------

void kernel_init(multiboot_info_t *mbi)
{
	mem_init(mbi);

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

	if (symbol_init((char*)module_addr, module_len) == false) {
		// this is not critical
		warn("failed to load symbol from module");
	}

	ps2_init();

	success("kernel initialization complete");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
