/*
 * page_frame_allocator.c
 *
 * The first implementation of a dummy page frame allocator.
 */

#include <kernel/types.h>

#include <mem/memory.h>

#define LOG_MODULE "pfa"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

typedef unsigned char page_state_t;

#define PAGE_FREE ((page_state_t) 0)
#define PAGE_USED ((page_state_t) 1) // single page frame
#define PAGE_USED_HEAD ((page_state_t) 2) // head of page frame block
#define PAGE_USED_TAIL ((page_state_t) 3) // tail of page frame block
#define PAGE_USED_PART ((page_state_t) 4) // part of page frame block

// ----------------------------------------------------------------------------

struct pfa_info {
	uint32_t first_page; // first allocatable page
	size_t nb_pages;
	page_state_t pagemap[0];
	// pagemap goes here
};

// ----------------------------------------------------------------------------

static uint32_t physmem_region_addr = 0;
static uint32_t physmem_region_len = 0;
static uint32_t pfa_info_reserved_pages = 0;
static struct pfa_info *pfa = NULL; // allocated at the very first pages

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Allocates a single page frame.
 *
 * Returns the physical address of the allocated page frame, or NULL on error.
 */

static pgframe_t pfa_alloc_single(void)
{
	dbg("allocating a single page frame");

	for (size_t page = 0; page < pfa->nb_pages; ++page) {
		if (pfa->pagemap[page] == PAGE_FREE) {
			pfa->pagemap[page] = PAGE_USED;
			return (pfa->first_page + page * PAGE_SIZE);
		}
	}

	warn("not enough memory");
	return BAD_PAGE;
}

// ----------------------------------------------------------------------------

/*
 * Allocates @nb_pages contiguous page frames.
 *
 * Returns the physical address of the first page frame, or NULL on error.
 */

pgframe_t pfa_alloc_multiple(size_t nb_pages)
{
	size_t start_page = 0;

	dbg("allocating %u page frames", nb_pages);

	if (nb_pages > pfa->nb_pages) {
		error("total page available is lesser than %u", nb_pages);
		return BAD_PAGE;
	}

	dbg("pfa->nb_pages = %u", pfa->nb_pages);

	/*
	 * This is a greedy algorithm. We start from a free page and see how many
	 * free pages we can get until we fulfill the request.
	 */

	while (start_page <= (pfa->nb_pages - nb_pages)) {
		if (pfa->pagemap[start_page] == PAGE_FREE) {
			size_t block_size = 0; // number of free pages so far
			size_t page = start_page;

			while ((block_size < nb_pages) && (pfa->pagemap[page] == PAGE_FREE)) {
				block_size++;
				page++;
				//dbg("page = %u", page);
			}

			if (block_size == nb_pages) {
				goto found;
			} else {
				// XXX: start_page is incremented below
				start_page = page; // skip this block (not big enough)
			}
		}

		start_page++;
	}

	// cannot find @nb_pages contiguous free page frames
	error("not enough memory");
	return BAD_PAGE;

found:

	for (size_t page = start_page; page < (start_page + nb_pages); ++page) {
		if (page == start_page) {
			pfa->pagemap[page] = PAGE_USED_HEAD;
		} else if (page == (start_page + nb_pages - 1)) {
			pfa->pagemap[page] = PAGE_USED_TAIL;
		} else {
			pfa->pagemap[page] = PAGE_USED_PART;
		}
	}

	return (pfa->first_page + start_page * PAGE_SIZE);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Maps the Page Frame Allocator meta-data.
 *
 * This must never failed.
 */

void pfa_map_metadata(void)
{
	info("mapping %d PFA metadata pages at 0x%p",
		pfa_info_reserved_pages, pfa);

	for (size_t i = 0; i < pfa_info_reserved_pages; ++i) {
		uint32_t addr = (uint32_t)pfa + i*PAGE_SIZE;
		if (map_page(addr, addr, PTE_RW_KERNEL_NOCACHE) == false) {
			// unrecoverable error
			error("failed to map page 0x%p", addr);
			abort();
		}
	}

	success("mapping PFA metadata succeed");
}

// ----------------------------------------------------------------------------

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
		(physmem_region_len / PAGE_SIZE) * sizeof(page_state_t);
	dbg("page frame allocator needs %u bytes", pfa_metadata_size);

	reserved_pages = page_align(pfa_metadata_size) / PAGE_SIZE;
	dbg("reserved pages = %u", reserved_pages);

	// store the PFA metadata at the first provided pages
	pfa = (struct pfa_info*) physmem_region_addr;
	pfa->first_page = physmem_region_addr + reserved_pages * PAGE_SIZE;
	pfa->nb_pages = (physmem_region_len / PAGE_SIZE) - reserved_pages;
	info("PFA first page is: 0x%x", pfa->first_page);
	info("PFA has %u available pages", pfa->nb_pages);

	// keep the number of reserved pages for bootstrapping (paging)
	pfa_info_reserved_pages = reserved_pages;

	// mark all pages as free
	for (size_t page = 0; page < pfa->nb_pages; ++page) {
		pfa->pagemap[page] = PAGE_FREE;
	}

	success("page frame allocator initialization succeed");
	return true;
}

// ----------------------------------------------------------------------------

/*
 * Allocates @nb_pages contiguous page frames.
 *
 * WARNING: once paging is enabled, returned page(s) frame MUST BE mapped
 * before being deref'ed (expect a page fault otherwise).
 *
 * Returns the physical address of the first page frame, or NULL on error.
 */

pgframe_t pfa_alloc(size_t nb_pages)
{
	if (nb_pages == 0) {
		error("invalid argument");
		return BAD_PAGE;
	}

	if (nb_pages == 1) {
		return pfa_alloc_single();
	} else {
		return pfa_alloc_multiple(nb_pages);
	}
}

// ----------------------------------------------------------------------------

/*
 * Frees a single page frame.
 *
 * NOTE: The page frame must has been previously allocated with pfa_alloc()
 * otherwise this will lead to a double-free -> panic.
 *
 * WARNING: if the page frame has been mapped, it is the caller responsability
 * to unmapped it (otherwise expect use-after-free / double-mapping issue).
 */

void pfa_free(pgframe_t pgf)
{
	const uint32_t max_pgf = pfa->first_page + pfa->nb_pages*PAGE_SIZE;
	size_t index = 0;

	dbg("freeing 0x%p", pgf);

	if (pgf < pfa->first_page || pgf >= max_pgf) {
		error("invalid page (out-of-bound)");
		abort();
	}

	if (PAGE_OFFSET(pgf)) {
		error("pgf is not aligned on a page boundary");
		abort();
	}

	index = (pgf - pfa->first_page) / PAGE_SIZE;

	if (pfa->pagemap[index] == PAGE_FREE) {
		error("double-free detected!");
		abort();
	} else if (pfa->pagemap[index] == PAGE_USED) {
		// freeing a single page frame
		pfa->pagemap[index] = PAGE_FREE;
	} else if (pfa->pagemap[index] != PAGE_USED_HEAD) {
		error("freeing a non head page frame block");
		abort();
	} else {
		// this is the head of a page frame block (i.e. contiguous pages)
		while (pfa->pagemap[index] != PAGE_USED_TAIL) {
			// sanity (debug) checks
			if (index >= pfa->nb_pages) {
				error("index out-of-bound");
				abort();
			} else if ((pfa->pagemap[index] != PAGE_USED_HEAD) &&
					   (pfa->pagemap[index] != PAGE_USED_PART)) {
				error("unexpected page state");
				abort();
			} else {
				pfa->pagemap[index] = PAGE_FREE;
			}
			index++;
		}

		// release the tail page
		pfa->pagemap[index] = PAGE_FREE;
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
