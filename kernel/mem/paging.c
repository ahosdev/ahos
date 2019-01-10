/*
 * paging.c
 *
 * Paging Memory management with a single level.
 *
 * Documentation:
 * - Intel (chapter 3)
 * - https://wiki.osdev.org/Paging
 * - https://wiki.osdev.org/Setting_Up_Paging
 */

#include <mem/memory.h>

#include <kernel/log.h>

#define LOG_MODULE "paging"

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

typedef uint32_t pde_t;


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
#define PTE_MASK_PT_ATTRIBUTE_INDEX	(1 << 7) // PAT enabled, otherwise reversed (=0)
#define PTE_MASK_GLOBAL_PAGE		(1 << 8) // 1=not invalidated by TLB (see doc)
#define PTE_MASK_ADDR				(0xfffff000) // Page Base Address

typedef uint32_t pte_t;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// TODO: use the pfa once transitionned into higher-half kernel
static pde_t page_directory[1024] __attribute__((aligned(PAGE_SIZE)));

// TODO: use the pfa once transitionned into higher-half kernel
static pte_t first_page_table[1024]  __attribute__((aligned(PAGE_SIZE)));

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Setup an Identity Mapping for the first 4MB of memory and enable paging.
 */

void paging_setup(void)
{
	size_t i = 0;

	info("paging setup...");

	// first clear the whole page directory
	for (i = 0; i < 1024; ++i) {
		pde_t flags = 0;

		// supervisor + page table not present/accessed and...
		// ...page size is 4096.
		flags |= PDE_MASK_READWRITE;
		flags |= PDE_MASK_WRITE_THROUGH; // TODO: deal with cache
		flags |= PDE_MASK_CACHE_DISABLED; // TODO: deal with cache

		page_directory[i] = flags;
	}

	// map the very first 4MB
	for (i = 0; i < 1024; ++i) {
		pte_t flags = 0;

		flags |= PTE_MASK_PRESENT;
		flags |= PTE_MASK_READWRITE;
		flags |= PDE_MASK_WRITE_THROUGH; // TODO: deal with cache
		flags |= PDE_MASK_CACHE_DISABLED; // TODO: deal with cache

		first_page_table[i] = (i * 0x1000) | flags;
	}

	// make the first entry of page directory present + read/write-able
	page_directory[0] =
		((uint32_t) first_page_table) | PDE_MASK_PRESENT | PDE_MASK_READWRITE;

	// load page directory into cr3
	asm volatile("mov %0, %%eax\n"
				 "mov %%eax, %%cr3"
				 : /* no output */
				 : "a"(page_directory) /* input */
				 );

	// enable paging
	asm volatile("mov %%cr0, %%eax\n"
				 "or $0x80000000, %%eax\n"
				 "mov %%eax, %%cr0" : : );

	success("paging setup succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
