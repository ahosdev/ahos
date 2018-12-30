/*
 * memory.c
 *
 * Memory subsystem.
 */

#include <kernel/memory.h>

#undef LOG_MODULE
#define LOG_MODULE "memory"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the memory map from multiboot information.
 *
 * Arguments:
 * - mmap_addr: pointer to the first memory map entry
 * - mmap_length: total size of the memory map buffer
 *
 * Returns true on success, false otherwise.
 */

bool memory_map_init(multiboot_memory_map_t *mmap_addr, size_t mmap_length)
{
	info("initializing memory map...");

	if (mmap_addr == NULL || mmap_length == 0) {
		error("invalid argument");
		return false;
	}

	// TODO

	success("memory map initialization succeed");

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
