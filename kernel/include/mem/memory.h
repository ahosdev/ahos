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
#define PAGE_OFFSET(x) (((uint32_t)x) & (~PAGE_MASK))

#define BAD_PAGE ((uint32_t) 0)

// ----------------------------------------------------------------------------

typedef uint32_t pgframe_t; // represent a 32-bit physical address of a page

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

bool phys_mem_map_init(multiboot_info_t *mbi);
bool phys_mem_map_reserve(uint32_t from_addr, uint32_t *addr, size_t *len);

// ----------------------------------------------------------------------------

bool pfa_init(void);
void pfa_map_metadata(void);
pgframe_t pfa_alloc(size_t nb_pages);
void pfa_free(pgframe_t pgf);

// ----------------------------------------------------------------------------

void* kmalloc(size_t size);
void kfree(void *ptr);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// Page-Directory Entry masks
#define PDE_MASK_PRESENT			(1 << 0) // 1=page is in physical memory,
											 // 0=attempt to access generates
											 //	a page-fault exception (#PF)
#define PDE_MASK_READWRITE			(1 << 1) // 0=readonly, 1=read/write
#define PDE_MASK_SUPERVISOR			(1 << 2) // 0=supervisor, 1=user
#define PDE_MASK_WRITE_THROUGH		(1 << 3) // 0=write-back, 1=write-through
#define PDE_MASK_CACHE_DISABLED		(1 << 4) // 0=cached, 1=cache disabled
#define PDE_MASK_ACCESSED			(1 << 5) // set by processor at first access
#define PDE_MASK_RESERVED			(1 << 6) // unused (set to 0)
#define PDE_MASK_PAGE_SIZE			(1 << 7) // 0=4kb, 1=4Mb
#define PDE_MASK_GLOBAL_PAGE		(1 << 8) // ignored if point to page table
#define PDE_MASK_ADDR				(0xfffff000) // Page-Table Base Address

// Page-Table Entry masks
#define PTE_MASK_PRESENT			(1 << 0) // 1=page is in physical memory,
											 // 0=attempt to access generates
											 //	a page-fault exception (#PF)
#define PTE_MASK_READWRITE			(1 << 1) // 0=readonly, 1=read/write
#define PTE_MASK_SUPERVISOR			(1 << 2) // 0=supervisor, 1=user
#define PTE_MASK_WRITE_THROUGH		(1 << 3) // 0=write-back, 1=write-through
#define PTE_MASK_CACHE_DISABLED		(1 << 4) // 0=cached, 1=cache disabled
#define PTE_MASK_ACCESSED			(1 << 5) // set by processor at first access
#define PTE_MASK_DIRTY				(1 << 6) // 1=page has been written to
#define PTE_MASK_PT_ATTRIBUTE_INDEX	(1 << 7) // PAT enabled, otherwise reserved (=0)
#define PTE_MASK_GLOBAL_PAGE		(1 << 8) // 1=not invalidated by TLB (see doc)
#define PTE_MASK_ADDR				(0xfffff000) // Page Base Address

// ----------------------------------------------------------------------------

// common flags for supervisor page-directory entry (read/write, not present)
#define PDE_RW_KERNEL_NOCACHE ((pde_t) (PDE_MASK_READWRITE | \
							   PDE_MASK_WRITE_THROUGH | \
							   PDE_MASK_CACHE_DISABLED))

// common flags for supervisor page-table entry (read/write, not present)
#define PTE_RW_KERNEL_NOCACHE ((pte_t) (PTE_MASK_READWRITE | \
							   PTE_MASK_WRITE_THROUGH | \
							   PTE_MASK_CACHE_DISABLED))

// flags to check for consistenty between a pte flag and its pde. We want it to
// be sync for now (until copy-on-write implementation?)
#define PG_CONSISTENT_MASK (PTE_MASK_READWRITE | PTE_MASK_SUPERVISOR | \
						   PTE_MASK_WRITE_THROUGH | PTE_MASK_CACHE_DISABLED)

// ----------------------------------------------------------------------------

typedef uint32_t pte_t;
typedef uint32_t pde_t;

// ----------------------------------------------------------------------------

/*
 * Aligns @addr on a PAGE_SIZE boundary.
 *
 * Returns the next page aligned address, or @addr if it was already aligned.
 */

inline uint32_t page_align(uint32_t addr)
{
	if (PAGE_OFFSET(addr)) {
		// not aligned
		addr += PAGE_SIZE;
		return (addr & PAGE_MASK);
	} else {
		// already aligned
		return addr;
	}
}

// ----------------------------------------------------------------------------

void paging_setup(void);

bool map_page(uint32_t phys_addr, uint32_t virt_addr, uint32_t flags);
bool unmap_page(uint32_t virt_addr);

void page_fault_handler(int error);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * alloca()-like macros. Take care when manipulating pointers from those and
 * never transfer ownership to a calling function. This must only be used
 * during startup since the page frame allocator is not ready yet.
 */

#define stack_alloc(size, ptr) \
	asm volatile("sub %1, %%esp\n" \
				 "mov %%esp, %0" \
				 : "=r"(ptr) : "r"(size) : "%esp")

#define stack_free(size) \
	asm volatile("add %0, %%esp" \
				 : : "r"(size) : "%esp")

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_MEMORY_H_ */
