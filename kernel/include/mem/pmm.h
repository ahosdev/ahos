/*
 * pmm.h
 *
 * Physical Memory Map (layout) Handling.
 */

#ifndef MEM_PMM_H_
#define MEM_PMM_H_

#include <kernel/types.h>

#include <multiboot.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum phys_mmap_type {
	// follows the multiboot specification (do not re-order)
	MMAP_TYPE_AVAILABLE = 1,
	MMAP_TYPE_RESERVED,
	MMAP_TYPE_ACPI,
	MMAP_TYPE_NVS,
	MMAP_TYPE_BADRAM,
};

// ----------------------------------------------------------------------------

// describe a memory region
struct phys_mmap_entry {
	uint32_t addr; // starting address (physical)
	size_t len; // len in bytes
	enum phys_mmap_type type;
};

// ----------------------------------------------------------------------------

struct phys_mmap {
	size_t len; // number of entries
	struct phys_mmap_entry entries[0];
	// data will be appended here
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

extern struct phys_mmap *phys_mem_map;
extern void *module_addr;
extern size_t module_len;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

bool phys_mem_map_init(multiboot_info_t *mbi);
bool phys_mem_map_map_module(void);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !MEM_PMM_H_ */
