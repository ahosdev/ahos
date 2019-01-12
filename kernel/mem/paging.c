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

#include <arch/registers.h>

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

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// TODO: use the pfa once transitionned into higher-half kernel
static pde_t page_directory[1024] __attribute__((aligned(PAGE_SIZE)));

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Loads a new page_directory located at @pg_dir (physical address) into CR3.
 *
 * It left the CR3's flags untouched.
 *
 * Returns true on success, or false on failure.
 */

static bool load_page_directory(pde_t *pg_dir)
{
	const uint32_t pg_dir_addr = (uint32_t) pg_dir;
	reg_t reg;

	if (PAGE_OFFSET(pg_dir_addr) != 0) {
		error("page directory address is not page-aligned");
		return false;
	}

	reg = read_cr3();

	reg.cr3.pdb = ((uint32_t)pg_dir_addr) >> 12;

	write_cr3(reg);

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Maps a single page for @phys_addr to @virt_addr using @flags flags.
 *
 * If a PDE entry is present, it is expected that @flag is consistent with it
 * (both read/write, both supervisor, etc.). If not, a new page table is
 * allocated from the PFA and the PDE's flags is based on @flags.
 *
 * Rewriting an existing PTE (i.e. page present) is not allowed with map_page().
 *
 * NOTE: @phys_addr can point to memory mapped I/O (e.g. VGA buffer). It does
 * not have to be real memory.
 *
 * Returns true on success, false otherwise.
 */

bool map_page(uint32_t phys_addr, uint32_t virt_addr, uint32_t flags)
{
	uint32_t pd_index = 0;
	uint32_t pt_index = 0;
	pte_t *page_table = NULL;
	pde_t pde_flags = 0;

	//dbg("phys_addr = 0x%x", phys_addr);
	//dbg("virt_addr = 0x%x", virt_addr);
	//dbg("flags = 0x%x", flags);

	// validate addresses are page-aligned and flags doesn't have bad values
	if (PAGE_OFFSET(phys_addr) != 0 ||
		PAGE_OFFSET(virt_addr) != 0 ||
		(flags & PTE_MASK_ADDR))
	{
		error("invalid argument");
		return false;
	}

	// compute page-directory and page-table index from virtual address
	pd_index = virt_addr >> 22; // The "highest" 10-bits
	pt_index = virt_addr >> 12 & 0x03ff; // the "middle" 10-bits

	// is the PDE present ?
	if ((page_directory[pd_index] & PDE_MASK_PRESENT) == 0) {
		// nope, we need to allocate a new page table and initialize it first
		pte_t *new_page_table = (pte_t*) pfa_alloc(1);

		if (new_page_table == NULL) {
			error("not enough memory");
			return false;
		}

		// paranoid check (should be debug only)
		if ((uint32_t)new_page_table & ~PTE_MASK_ADDR) {
			error("new page is not page-aligned");
			return false;
		}

		// mark all entries as "not present" but set the other flags
		for (size_t i = 0; i < 1024; ++i) {
			new_page_table[i] = flags & ~PDE_MASK_PRESENT;
		}

		// insert the new page directory entry (don't copy GLOBAL or PAT flags)
		pde_flags  = flags & PG_CONSISTENT_MASK;
		pde_flags |= PDE_MASK_PRESENT; // mark it present
		page_directory[pd_index] = pde_flags | ((uint32_t) new_page_table);

		dbg("new page table created");

		page_table = new_page_table;
	} else {
		// check for consistency between @flags and PDE's flags
		pde_flags = page_directory[pd_index] & PG_CONSISTENT_MASK;
		if ((flags & PG_CONSISTENT_MASK) != pde_flags) {
			error("flags are not consistent with page-directory entry");
			return false;
		}

		page_table = (pte_t*) (page_directory[pd_index] & PDE_MASK_ADDR);

		// is there already a mapping present?
		if (page_table[pt_index] & PTE_MASK_PRESENT) {
			error("overwriting an already present mapping!");
			abort();
		}
	}

	// set the PTE
	page_table[pt_index] = phys_addr | flags | PTE_MASK_PRESENT;
	dbg("page 0x%x (phys) mapped to 0x%x (virt)", phys_addr, virt_addr);

	// TODO: flush TLB (when cache will be enables)

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Setup an Identity Mapping for the first 4MB of memory and enable paging.
 */

void paging_setup(void)
{
	reg_t reg;
	size_t i = 0;

	info("paging setup...");

	// first clear the whole page directory
	for (i = 0; i < 1024; ++i) {
		page_directory[i] = PDE_RW_KERNEL_NOCACHE;
	}

	// map the very first 4MB
	for (i = 0; i < 1024; ++i) {
		bool ret = map_page(i*PAGE_SIZE, i*PAGE_SIZE, PTE_RW_KERNEL_NOCACHE);
		if (ret == false) {
			error("failed to map page number %u", i);
			abort();
		}
	}
	success("first 4MB mapped");

	if (load_page_directory(page_directory) == false) {
		error("failed to load the new page directory");
		abort();
	}

	// enable paging
	reg = read_cr0();
	reg.cr0.pg = 1;
	write_cr0(reg);

	success("paging setup succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
