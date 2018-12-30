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

bool memory_map_init(multiboot_uint32_t mmap_addr, multiboot_uint32_t mmap_length);
bool memory_reserve(uint32_t from_addr, uint32_t *addr, size_t *len);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_MEMORY_H_ */
