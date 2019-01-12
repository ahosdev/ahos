/*
 * paging.c
 *
 * Paging Memory management with a single level.
 *
 * For now, we use an Identity Paging policy.
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

static pde_t *page_directory = NULL;

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

// ----------------------------------------------------------------------------

/*
 * Identity maps critical memory region (kernel, VRAM, etc.) before enabling
 * paging, otherwise the kernel will instant crash.
 *
 * This must NEVER failed.
 */

static void bootstrap_mapping(void)
{
	struct bootstrap_range {
		char name[16];
		uint32_t start; // must be page aligned
		uint32_t end;
	};

	struct bootstrap_range maps[] = {
		{
			.name	= "kernel",
			.start	= (uint32_t) &kernel_start,
			.end	= (uint32_t) &kernel_end,
		},
		{
			.name	= "vram",
			.start	= 0xa0000,
			.end	= 0xfffff,
		},
	};

	dbg("starting bootstrap mapping...");

	for (size_t i = 0; i < (sizeof(maps) / sizeof(maps[0])); ++i) {
		struct bootstrap_range *range = &maps[i];
		uint32_t end = 0;

		if (PAGE_OFFSET(range->start)) {
			error("starting range is not page-aligned");
			abort();
		}

		end = page_align(range->end + 1); // XXX: is this correct?

		dbg("mapping [%p - %p] %s", range->start, end - 1, range->name);

		for (size_t addr = range->start; addr < end; addr += PAGE_SIZE) {
			if (map_page(addr, addr, PTE_RW_KERNEL_NOCACHE) == false) {
				// unrecoverable
				error("failed to map 0x%p", addr);
				abort();
			}
		}
	}

	dbg("bootstrap mapping succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Handle Page Fault (#PF) exception.
 *
 * The fault can be either resolved or the kernel panic.
 */

void page_fault_handler(int error)
{
	reg_t cr2;

	info("\"Page Fault\" exception detected!");

	info("error code: %d", error);

	// retrieve the faulty address
	cr2 = read_cr2();
	info("faulty address: 0x%p", cr2.val);

	// TODO: page table walking

	NOT_IMPLEMENTED();
}

// ----------------------------------------------------------------------------

/*
 * Maps a single page for @phys_addr to @virt_addr using @flags PTE flags.
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

	if ((page_directory = (pde_t*)pfa_alloc(1)) == NULL) {
		error("cannot allocate page_directory");
		abort();
	}

	// first clear the whole page directory
	for (i = 0; i < 1024; ++i) {
		page_directory[i] = PDE_RW_KERNEL_NOCACHE;
	}

	if (load_page_directory(page_directory) == false) {
		error("failed to load the new page directory");
		abort();
	}

	bootstrap_mapping();

	// enable paging
	reg = read_cr0();
	reg.cr0.pg = 1;
	write_cr0(reg);

	success("paging setup succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
