/*
 * memory.h
 */

#ifndef MEM_MEMORY_H_
#define MEM_MEMORY_H_

#include <kernel/types.h>

#include <multiboot.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define PAGE_SIZE	(4096)
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (~PAGE_MASK))

#define BAD_PAGE ((uint32_t) 0)

// ----------------------------------------------------------------------------

typedef uint32_t pgframe_t; // represent a 32-bit physical address of a page

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

bool phys_mem_map_init(multiboot_uint32_t mmap_addr, multiboot_uint32_t mmap_length);
bool phys_mem_map_reserve(uint32_t from_addr, uint32_t *addr, size_t *len);

// ----------------------------------------------------------------------------

bool pfa_init(void);
pgframe_t pfa_alloc(size_t nb_pages);
void pfa_free(pgframe_t pgf);

// ----------------------------------------------------------------------------

void* kmalloc(size_t size);
void kfree(void *ptr);

// ----------------------------------------------------------------------------

void paging_setup(void);
bool map_page(uint32_t phys_addr, uint32_t virt_addr, uint32_t flags);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_MEMORY_H_ */
