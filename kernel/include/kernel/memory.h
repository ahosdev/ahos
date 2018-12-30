/*
 * memory.h
 */

#ifndef KERNEL_MEMORY_H_
#define KERNEL_MEMORY_H_

#include <kernel/types.h>

#include <multiboot.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

bool memory_map_init(multiboot_memory_map_t *mmap_addr, size_t mmap_length);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_MEMORY_H_ */
