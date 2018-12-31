/*
 * page_frame_allocator.c
 *
 * The first implementation of a dummy page frame allocator.
 */

#include <kernel/types.h>

#include <mem/memory.h>

#undef LOG_MODULE
#define LOG_MODULE "pfa"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum page_status {
	PAGE_FREE,
	PAGE_USED,
};

// ----------------------------------------------------------------------------

struct pfa_info {
	uint32_t first_page; // first allocatable page
	size_t nb_pages;
	enum page_status pagemap[0]; // XXX: we can use bitmap to reduce footprint
	// pagemap goes here
};

// ----------------------------------------------------------------------------

static uint32_t physmem_region_addr = 0;
static uint32_t physmem_region_len = 0;

static struct pfa_info *pfa = NULL; // allocated at the very first pages

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Aligns @addr on a PAGE_SIZE boundary.
 *
 * Returns the next page aligned address, or @addr if it was already aligned.
 */

static inline uint32_t page_align(uint32_t addr)
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

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the Page Frame Allocator.
 *
 * Returns true on success, false otherwise.
 */

bool pfa_init(void)
{
	uint32_t addr = 0;
	uint32_t len = 0;
	size_t pfa_metadata_size = 0;
	size_t reserved_pages = 0;

	info("page frame allocator initialization...");

	// we reserve all memory past kernel image
	if (phys_mem_map_reserve((uint32_t)&kernel_end, &addr, &len) == false) {
		error("failed to reserved region after 0x%x", &kernel_end);
		return false;
	}
	dbg("memory region [0x%08x - 0x%08x] reserved", addr, addr+len-1);

	// align the starting address and len to a page boundary
	// NOTE: the first (non aligned) bytes and last (non aligned) bytes are lost
	physmem_region_addr = page_align(addr);
	physmem_region_len = (len - PAGE_OFFSET(addr)) & PAGE_MASK; // page aligned

	dbg("physmem_region_addr = 0x%08x", physmem_region_addr);
	dbg("physmem_region_len  = 0x%08x", physmem_region_len);

	/*
	 * Alright, we now have a plenty of pages to play with. Let's store the
	 * allocator's metadata at the very first pages.
	 */

	pfa_metadata_size = sizeof(struct pfa_info) +
		(physmem_region_len / PAGE_SIZE) * sizeof(enum page_status);
	dbg("page frame allocator needs %u bytes", pfa_metadata_size);

	reserved_pages = page_align(pfa_metadata_size) / PAGE_SIZE;
	dbg("reserved pages = %u", reserved_pages);

	// store the PFA metadata at the first provided pages
	pfa = (struct pfa_info*) physmem_region_addr;
	pfa->first_page = physmem_region_addr + reserved_pages * PAGE_SIZE;
	pfa->nb_pages = (physmem_region_len / PAGE_SIZE) - reserved_pages;
	info("PFA first page is: 0x%x", pfa->first_page);
	info("PFA has %u available pages", pfa->nb_pages);

	// mark all pages as free
	for (size_t page = 0; page < pfa->nb_pages; ++page) {
		pfa->pagemap[page] = PAGE_FREE;
	}

	success("page frame allocator initialization succeed");
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Allocates a single page frame.
 *
 * Returns the physical address of the allocated page frame, or NULL on error.
 */

pgframe_t pfa_alloc(void)
{
	for (size_t page = 0; page < pfa->nb_pages; ++page) {
		if (pfa->pagemap[page] == PAGE_FREE) {
			pfa->pagemap[page] = PAGE_USED;
			return (pfa->first_page + page * PAGE_SIZE);
		}
	}

	warn("no memory available");
	return BAD_PAGE;
}

// ----------------------------------------------------------------------------

/*
 * Frees a single page frame.
 *
 * NOTE: The page frame must has been previously allocated with pfa_alloc()
 * otherwise this will lead to a double-free -> panic.
 */

void pfa_free(pgframe_t pgf)
{
	const uint32_t max_pgf = pfa->first_page + pfa->nb_pages*PAGE_SIZE;
	size_t index = 0;

	if (pgf < pfa->first_page || pgf >= max_pgf) {
		error("invalid page (out-of-bound)");
		abort();
	}

	if (PAGE_OFFSET(pgf)) {
		error("pgf is not aligned on a page boundary");
		abort();
	}

	index = (pgf - pfa->first_page) / PAGE_SIZE;

	if (pfa->pagemap[index] != PAGE_USED) {
		error("double-free detected!");
		abort();
	}

	pfa->pagemap[index] = PAGE_FREE;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
