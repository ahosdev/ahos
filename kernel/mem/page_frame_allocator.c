/*
 * page_frame_allocator.c
 *
 * The first implementation of a dummy page frame allocator.
 */

#include <kernel/types.h>

#include <mem/memory.h>
#include <mem/pmm.h>

#include <string.h>

#define LOG_MODULE "pfa"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

typedef unsigned char page_state_t;

// XXX: we don't use enum here on purpose (keep page_state_t on 1 byte)
#define PAGE_FREE ((page_state_t) 0)
#define PAGE_USED ((page_state_t) 1) // single page frame
#define PAGE_USED_HEAD ((page_state_t) 2) // head of page frame block
#define PAGE_USED_TAIL ((page_state_t) 3) // tail of page frame block
#define PAGE_USED_PART ((page_state_t) 4) // part of page frame block

// ----------------------------------------------------------------------------

struct pfa_region {
	uint32_t first_page; // first allocatable page
	size_t nb_pages;
	page_state_t pagemap[0];
	// pagemap goes here
};

struct pfa_meta {
	size_t nb_regions;
	struct pfa_region* region_ptrs[0];
	// regions pointers are embedded here
	struct pfa_region regions[0];
	// regions data are embedded here
};

// ----------------------------------------------------------------------------

static uint32_t pfa_meta_reserved_pages = 0;
static struct pfa_meta *pfa_meta = NULL;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void dump_pfa_meta(struct pfa_meta *pm)
{
	dbg("---[ dump pfa_meta ]---");
	dbg("- nb_regions = %u", pm->nb_regions);
	for (size_t region = 0; region < pm->nb_regions; ++region) {
		struct pfa_region *pr = pm->region_ptrs[region];
		dbg("- region_ptrs[%u] = 0x%p", region, pm->region_ptrs[region]);
		dbg("\t[region #%u] nb_pages = %u", region, pr->nb_pages);
		dbg("\t[region #%u] first_page = 0x%p", region, pr->first_page);
	}
	dbg("-----------------------");
}

// ----------------------------------------------------------------------------

/*
 * Checks if the @pmme region is valid.
 *
 * A region is valid if:
 * - it is available
 * - it is not in low memory
 * - it has at least a page once the starting address has been page aligned
 */

static bool is_valid_region(struct phys_mmap_entry *pmme)
{
	uint32_t len = pmme->len;

	if (pmme->type != MMAP_TYPE_AVAILABLE) {
		return false;
	}

	// skip low mem (a hole is guaranteed)
	if (pmme->addr < 0x100000) {
		return false;
	}

	if (PAGE_OFFSET(pmme->addr)) {
		uint32_t addr = page_align(pmme->addr);

		if (addr < pmme->addr) {
			// int overflow
			return false;
		}

		if (addr >= (pmme->addr + pmme->len)) {
			// prevent int underflow (len)
			return false;
		} else {
			len -= addr - pmme->addr;
		}
	}

	return ((len / PAGE_SIZE) > 0);
}

// ----------------------------------------------------------------------------

/*
 * Returns the page-aligned size (in bytes) to hold the whole PFA metadata.
 */

static size_t pfa_meta_size(struct phys_mmap *pmm)
{
	size_t size = 0;

	size += sizeof(struct pfa_meta);

	for (size_t i = 0; i < pmm->len; ++i) {
		struct phys_mmap_entry *pmme = &pmm->entries[i];
		dbg("region[%u]: 0x%p (%u bytes)", i, pmme->addr, pmme->len);
		if (is_valid_region(pmme)) {
			size_t nb_pages = 0;
			size_t region_len = pmme->len;

			dbg("valid region");

			if (PAGE_OFFSET(pmme->addr)) {
				region_len -= page_align(pmme->addr) - pmme->addr;
			}
			nb_pages = region_len / PAGE_SIZE;

			size += sizeof(struct pfa_region*);
			size += sizeof(struct pfa_region);
			size += nb_pages * sizeof(page_state_t);
		}
	}

	return page_align(size);
}

// ----------------------------------------------------------------------------

/*
 * Finds a region which can host the PFA metadata on a page-aligned starting
 * address. If a region is found, @meta is updated to point to the first
 * page-aligned address.
 *
 * NOTE: A valid region might become invalid once "consumed" by the PFA
 * metadata (not enough bytes remaining to hold a single page). That is, we
 * reserved space (@pfa_size) for a region that won't exist...
 *
 * Returns the region index (pmm entries) on success, -1 otherwise.
 */

static size_t find_hosting_region(struct phys_mmap *pmm, size_t pfa_size,
								  struct pfa_meta **pfa)
{
	for (size_t region = 0; region < pmm->len; ++region) {
		struct phys_mmap_entry *pmme = &pmm->entries[region];
		size_t region_len = pmme->len;

		if (is_valid_region(pmme) == false) {
			continue;
		}

		if (PAGE_OFFSET(pmme->addr)) {
			region_len -= page_align(pmme->addr) - pmme->addr;
		}

		if (region_len >= pfa_size) {
			// found one
			*pfa = (struct pfa_meta*) page_align(pmme->addr);
			return region;
		}
	}

	return -1;
}

// ----------------------------------------------------------------------------

static void init_pfa_region(struct phys_mmap_entry *pmme,
							struct pfa_region *region)
{
	uint32_t len = pmme->len;

	if (PAGE_OFFSET(pmme->addr)) {
		// no int underflow check, the region is supposed to be valid
		len -= page_align(pmme->addr) - pmme->addr;
	}

	region->first_page = page_align(pmme->addr);
	region->nb_pages = len / PAGE_SIZE;

	for (size_t page = 0; page < region->nb_pages; ++page) {
		region->pagemap[page] = PAGE_FREE;
	}
}

// ----------------------------------------------------------------------------

/*
 * Reserves all valid regions and fills the PFA metadata.
 */

static void reserve_regions(struct phys_mmap *pmm, size_t pfa_size,
							size_t pfa_region, struct pfa_meta *pfa)
{
	struct pfa_region *new_region = NULL;

	if (PAGE_OFFSET(pfa_size) || (pfa_region >= pmm->len)) {
		error("invalid argument");
		abort();
	}

	pfa->nb_regions = 0;
	memset(pfa, 0, pfa_size);

	// we need to walk a first time to skip the 'region_ptrs' offset
	new_region = &pfa->regions[0];
	for (size_t region = 0; region < pmm->len; ++region) {
		if (is_valid_region(&pmm->entries[region])) {
			new_region = (struct pfa_region*)
				((uint32_t)new_region + sizeof(struct pfa_region*));
		}
	}

	for (size_t region = 0; region < pmm->len; ++region) {
		struct phys_mmap_entry *pmme = &pmm->entries[region];

		if (is_valid_region(pmme) == false) {
			continue;
		}

		init_pfa_region(pmme, new_region);

		if (region == pfa_region) {
			new_region->first_page += pfa_size;
			new_region->nb_pages -= pfa_size / PAGE_SIZE;
			if (new_region->nb_pages == 0) {
				warn("the PFA metadata consumed the whole region");
			}
		}

		// reserve the whole region
		pmme->type = MMAP_TYPE_RESERVED;

		dbg("region #%u has %u pages", region, new_region->nb_pages);

		pfa->region_ptrs[pfa->nb_regions++] = new_region;

		new_region = (struct pfa_region*)((uint32_t)new_region +
					 sizeof(*new_region) +
					 (new_region->nb_pages * sizeof(page_state_t)));
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Allocates a single page frame.
 *
 * Returns the physical address of the allocated page frame, or NULL on error.
 */

static pgframe_t pfa_alloc_single(struct pfa_region *region)
{
	dbg("allocating a single page frame");

	for (size_t page = 0; page < region->nb_pages; ++page) {
		if (region->pagemap[page] == PAGE_FREE) {
			region->pagemap[page] = PAGE_USED;
			return (region->first_page + page * PAGE_SIZE);
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

pgframe_t pfa_alloc_multiple(struct pfa_region *region, size_t nb_pages)
{
	size_t start_page = 0;

	dbg("allocating %u page frames", nb_pages);

	if (nb_pages > region->nb_pages) {
		error("total page available is lesser than %u", nb_pages);
		return BAD_PAGE;
	}

	dbg("pfa->nb_pages = %u", region->nb_pages);

	/*
	 * This is a greedy algorithm. We start from a free page and see how many
	 * free pages we can get until we fulfill the request (aka "first fit").
	 */

	while (start_page <= (region->nb_pages - nb_pages)) {
		if (region->pagemap[start_page] == PAGE_FREE) {
			size_t block_size = 0; // number of free pages so far
			size_t page = start_page;

			while ((block_size < nb_pages) && (region->pagemap[page] == PAGE_FREE)) {
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
			region->pagemap[page] = PAGE_USED_HEAD;
		} else if (page == (start_page + nb_pages - 1)) {
			region->pagemap[page] = PAGE_USED_TAIL;
		} else {
			region->pagemap[page] = PAGE_USED_PART;
		}
	}

	return (region->first_page + start_page * PAGE_SIZE);
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
		pfa_meta_reserved_pages, pfa_meta);

	for (size_t i = 0; i < pfa_meta_reserved_pages; ++i) {
		uint32_t addr = (uint32_t)pfa_meta + i*PAGE_SIZE;
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
	size_t pfa_size = 0;
	size_t pfa_region = 0;

	info("page frame allocator initialization...");

	// compute the total size needed to host all regions
	pfa_size = pfa_meta_size(phys_mem_map);
	dbg("pfa_size = %u", pfa_size);

	// find a region which can host the PFA metadata
	pfa_region = find_hosting_region(phys_mem_map, pfa_size, &pfa_meta);
	if (pfa_region == (uint32_t)-1) {
		error("no region can host the PFA metadata");
		return false;
	}
	dbg("region #%u can host PFA metadata", pfa_region);
	dbg("PFA metadata is stored at 0x%p", pfa_meta);

	// now reserve the regions and fills the PFA metadata
	reserve_regions(phys_mem_map, pfa_size, pfa_region, pfa_meta);

	// keep the number of reserved pages for bootstrapping (paging)
	pfa_meta_reserved_pages = pfa_size / PAGE_SIZE;

	dump_pfa_meta(pfa_meta);

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
	struct pfa_region *region = pfa_meta->region_ptrs[0];

	if (nb_pages == 0) {
		error("invalid argument");
		return BAD_PAGE;
	}

	if (nb_pages == 1) {
		return pfa_alloc_single(region);
	} else {
		return pfa_alloc_multiple(region, nb_pages);
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
	struct pfa_region *region = pfa_meta->region_ptrs[0];
	const uint32_t max_pgf = region->first_page + region->nb_pages*PAGE_SIZE;
	size_t index = 0;

	dbg("freeing 0x%p", pgf);

	if (pgf < region->first_page || pgf >= max_pgf) {
		error("invalid page (out-of-bound)");
		abort();
	}

	if (PAGE_OFFSET(pgf)) {
		error("pgf is not aligned on a page boundary");
		abort();
	}

	index = (pgf - region->first_page) / PAGE_SIZE;

	if (region->pagemap[index] == PAGE_FREE) {
		error("double-free detected!");
		abort();
	} else if (region->pagemap[index] == PAGE_USED) {
		// freeing a single page frame
		region->pagemap[index] = PAGE_FREE;
	} else if (region->pagemap[index] != PAGE_USED_HEAD) {
		error("freeing a non head page frame block");
		abort();
	} else {
		// this is the head of a page frame block (i.e. contiguous pages)
		while (region->pagemap[index] != PAGE_USED_TAIL) {
			// sanity (debug) checks
			if (index >= region->nb_pages) {
				error("index out-of-bound");
				abort();
			} else if ((region->pagemap[index] != PAGE_USED_HEAD) &&
					   (region->pagemap[index] != PAGE_USED_PART)) {
				error("unexpected page state");
				abort();
			} else {
				region->pagemap[index] = PAGE_FREE;
			}
			index++;
		}

		// release the tail page
		region->pagemap[index] = PAGE_FREE;
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
