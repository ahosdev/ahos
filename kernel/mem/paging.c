/*
 * paging.c
 *
 * Paging Memory management with a single level.
 *
 * For now, we use an Identity Paging policy. However, the page tables are
 * linearly mapped to [0xfffc0000 - 0xfffff000] (4MB) with the last one being
 * the page directory itself (PDE self-mapping).
 *
 * Documentation:
 * - Intel (chapter 3 and 9)
 * - https://wiki.osdev.org/Paging
 * - https://wiki.osdev.org/Setting_Up_Paging
 * - https://forum.osdev.org/viewtopic.php?f=15&t=19387 // PDE self-mapping
 * - https://wiki.osdev.org/TLB
 * - https://forum.osdev.org/viewtopic.php?f=1&t=18222 // TLB invalidation
 */

#include <mem/memory.h>
#include <mem/pmm.h>

#include <kernel/log.h>

#include <arch/registers.h>

#define LOG_MODULE "paging"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define PD_INDEX(virt_addr) ((uint32_t)virt_addr >> 22) // highest 10-bits
#define PT_INDEX(virt_addr) (((uint32_t)virt_addr >> 12) & 0x3ff) // middle 10-bits

#define PDE_PRESENT(pd_index) \
	(!!((uint32_t)page_directory[pd_index] & PDE_MASK_PRESENT))

// ----------------------------------------------------------------------------

inline static void invalidate_tlb(void);
inline static void invalidate_tlb_page(uint32_t virt_addr);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static pde_t *page_directory = NULL;

// TODO: get rid of paging_enabled when we will have separates ".text" and
// ".text.init" sections, with a dedicated allocator. We don't want this to
// spread all-over the kernel.
static bool paging_enabled = false;

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

static bool load_page_directory(uint32_t pgd_phys_addr)
{
	reg_t reg;

	if (PAGE_OFFSET(pgd_phys_addr) != 0) {
		error("page directory address is not page-aligned");
		return false;
	}

	reg = read_cr3();

	reg.cr3.pdb = pgd_phys_addr >> 12;

	write_cr3(reg); // writing to CR3 invalidates the whole TLB cache

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
			.start	= kernel_start,
			.end	= kernel_end,
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

	pfa_map_metadata();
	phys_mem_map_map_module();

	dbg("bootstrap mapping succeed");
}

// ----------------------------------------------------------------------------

/*
 * Pretty print a Page-Directory Entry.
 */

__attribute__((unused)) /* debugging function */
static void dump_pde(pde_t pde)
{
	dbg("---[ dumping PDE: 0x%x ]---", pde);

	dbg("page table addr (phys) = 0x%p", pde & PDE_MASK_ADDR);
	dbg("flags = 0x%x", pde & ~PDE_MASK_ADDR);

	dbg("- present: %s", pde & PDE_MASK_PRESENT ? "yes" : "no");
	dbg("- ro/rw: %s", pde & PDE_MASK_READWRITE ? "read/write" : "read-only");
	dbg("- user/supervisor: %s",
		pde & PDE_MASK_SUPERVISOR ? "user" : "supervisor");
	dbg("- wt/wb: %s",
		pde & PDE_MASK_WRITE_THROUGH ? "write-through" : "write-back");
	dbg("- cache: %s", pde & PDE_MASK_CACHE_DISABLED ? "disabled" : "enabled");
	dbg("- accessed: %s", pde & PDE_MASK_ACCESSED ? "yes" : "no");
	dbg("- page size: %s", pde & PDE_MASK_PAGE_SIZE ? "4MB" : "4KB");
	dbg("- global: %s", pde & PDE_MASK_GLOBAL_PAGE ? "yes" : "no");

	dbg("---[ end of dump ]---");
}

// ----------------------------------------------------------------------------

/*
 * Pretty print a Page-Table Entry.
 */

__attribute__((unused)) /* debugging function */
static void dump_pte(pte_t pte)
{
	dbg("---[ dumping PTE: 0x%x ]---", pte);

	dbg("page addr (phys) = 0x%p", pte & PTE_MASK_ADDR);
	dbg("flags = 0x%x", pte & ~PTE_MASK_ADDR);

	dbg("- present: %s", pte & PTE_MASK_PRESENT ? "yes" : "no");
	dbg("- ro/rw: %s", pte & PTE_MASK_READWRITE ? "read/write" : "read-only");
	dbg("- user/supervisor: %s",
		pte & PTE_MASK_SUPERVISOR ? "user" : "supervisor");
	dbg("- wt/wb: %s",
		pte & PTE_MASK_WRITE_THROUGH ? "write-through" : "write-back");
	dbg("- cache: %s", pte & PTE_MASK_CACHE_DISABLED ? "disabled" : "enabled");
	dbg("- accessed: %s", pte & PTE_MASK_ACCESSED ? "yes" : "no");
	dbg("- dirty: %s", pte & PTE_MASK_DIRTY ? "yes" : "no");
	dbg("- PAT: %s", pte & PTE_MASK_PT_ATTRIBUTE_INDEX ? "enabled" : "disabled");
	dbg("- global: %sTLB invalidation", pte & PTE_MASK_GLOBAL_PAGE?"no ":"");

	dbg("---[ end of dump ]---");
}

// ----------------------------------------------------------------------------

/*
 * Dumps Page-Table Entries of @pg_table.
 *
 * If @only_present is set, non present PTE are not shown.
 */

__attribute__((unused)) /* debugging function */
static void dump_page_table(pte_t *pg_table, bool only_present)
{
	size_t nb_presents = 0;

	dbg("---[ dumping page table 0x%p ]---", pg_table);

	if (pg_table == NULL) {
		error("invalid argument");
		return;
	}

	for (size_t i = 0; i < 1024; ++i) {
		if (!only_present || (pg_table[i] & PTE_MASK_PRESENT)) {
			dbg("  pt[%d] = 0x%x", i, pg_table[i]);
			nb_presents++;
		}
	}

	if (nb_presents == 0) {
		dbg("page table is empty");
	}

	dbg("---[ end of dumping ]---");
}

// ----------------------------------------------------------------------------

/*
 * Allocates a new page table, marks all PTE non present and maps it.
 *
 * Returns the created page table on success, NULL otherwise.
 */

static pte_t* new_page_table(uint32_t pd_index, uint32_t flags)
{
	pde_t pde_flags = 0;
	uint32_t new_pt_phys = 0;
	pte_t *page_table = NULL;

	dbg("creating new page table");

	if (PDE_PRESENT(pd_index)) {
		error("PDE is already set!");
		return NULL;
	}

	if ((new_pt_phys = pfa_alloc(1)) == 0) {
		error("not enough memory");
		return NULL;
	}

	// insert the new page directory entry (don't copy GLOBAL or PAT flags)
	pde_flags  = flags & PG_CONSISTENT_MASK;
	pde_flags |= PDE_MASK_PRESENT; // mark it present
	page_directory[pd_index] = pde_flags | new_pt_phys;

	// now let's retrieve its virtual address
	if (paging_enabled) {
		// using PDE self-mapping tricks
		page_table = (pte_t*) (0xffc00000 + pd_index * PAGE_SIZE);
		invalidate_tlb(); // XXX: faster to invalidate 1024 pages instead?
	} else {
		// identity mapping
		page_table = (pte_t*) new_pt_phys;
	}
	dbg("page_table = %p", page_table);

	// mark all entries as "not present" but set the other flags
	for (size_t i = 0; i < 1024; ++i) {
		page_table[i] = flags & ~PDE_MASK_PRESENT;
	}

	if (paging_enabled) {
		invalidate_tlb(); // XXX: faster to invalidate 1024 pages instead?
	}

	dbg("new page table created");

	return page_table;
}

// ----------------------------------------------------------------------------

/*
 * Invalidates the whole TLB cache.
 *
 * NOTE: This is an EXPENSIVE operation and must be avoided if possible.
 *
 * In addition, this won't works on SMP as it only flush a single CPU TLB
 * cache. In SMP, this is more complexe, requires to send IPI, etc.
 */

inline static void invalidate_tlb(void)
{
	asm volatile("mov %%cr3, %%eax\n"
				 "mov %%eax, %%cr3"
				 : );
}

// ----------------------------------------------------------------------------

/*
 * Invalidates a single TLB page table entry for virtual address @virt_addr.
 *
 * The CPU automatically retrieves the TLB PTE from the virtual address.
 *
 * Uses this version instead of invalidate_tlb() whenever possible.
 *
 * The TLB cache must be invalidated on page table operations (creation,
 * modification, deletion).
 *
 * NOTE: The "invlpg" only exists since i486 processors. For older cpu, it
 * falls back to a full TLB cache invalidation.
 */

inline static void invalidate_tlb_page(uint32_t virt_addr)
{
#if 0
	// only for archictecture with a CPU lesser than i486 (not planned to
	// support).
	invalidate_tlb();
#else
	// we can use the 'invlpg' instruction
	asm volatile("invlpg (%0)"
				 : /* no output */
				 :"r" (virt_addr)
				 : "memory");
#endif
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Handles Page Fault (#PF) exception.
 *
 * As there is no userland, no demand paging nor copy-on-write right now, page
 * faulting always crash the kernel.
 */

void page_fault_handler(int error)
{
	reg_t cr2;
	uint32_t pd_index;
	uint32_t pt_index;
	pte_t *page_table = NULL; // virtual address

	info("\"Page Fault\" exception detected!");
	info("");

	// pretty print error code
	info("error code: %d (page %s, %s access)", error,
		(error & 0x1) ? "present" : "not present",
		(error & 0x2) ? "write" : "read");
	info("origin: %s mode", (error & 0x4) ? "user" : "supervisor");
	// TODO: handle others error code when supporting PSE/PSA/NX
	info("");

	if (error & 0x1) {
		error("protection violation");
		NOT_IMPLEMENTED();
	}

	// retrieve the faulty address
	cr2 = read_cr2();
	pd_index = PD_INDEX(cr2.val);
	pt_index = PT_INDEX(cr2.val);

	info("faulty address: 0x%p", cr2.val);
	info("PD index: %d (0x%x)", pd_index, pd_index);
	info("PT index: %d (0x%x)", pt_index, pt_index);
	info("");

	// TODO: print EIP and (eventually) EFLAGS

	if (PDE_PRESENT(pd_index) == false) {
		error("page directory entry NOT PRESENT");
		abort();
	}
	dump_pde(page_directory[pd_index]);
	dbg("");

	// retrieve the corresponding page table
	page_table = (pte_t*) (0xffc00000 + pd_index * PAGE_SIZE);
	info("page-table address (virt): 0x%p", page_table);
	info("");

	// we assume the page table is mapped, otherwise the page mapping code
	// is seriously flawed
	info("PTE: 0x%x", page_table[pt_index]);
	info("");

	if ((page_table[pt_index] & PTE_MASK_PRESENT) == 0) {
		error("page table entry NOT PRESENT");
		abort();
	}
	dump_pte(page_table[pt_index]);
	dbg("");

	NOT_IMPLEMENTED(); // mostly protection fault or new feature (nx/pae/pse)
}

// ----------------------------------------------------------------------------

/*
 * Maps a single page for @phys_addr to @virt_addr using @flags PTE flags.
 *
 * If a PDE entry is present, it is expected that @flag is consistent with it
 * (both read/write, both supervisor, etc.).
 *
 * If there is no PDE, a new page table is allocated from the PFA and the PDE's
 * flags is based on @flags.
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
	pd_index = PD_INDEX(virt_addr);
	pt_index = PT_INDEX(virt_addr);

	// is the PDE present ?
	if ((page_directory[pd_index] & PDE_MASK_PRESENT) == 0) {
		// nope, we need to allocate a new page table and initialize it first
		if ((page_table = new_page_table(pd_index, flags)) == NULL) {
			error("failed to create new page table");
			abort();
		}
	} else {
		// check for consistency between @flags and PDE's flags
		pde_flags = page_directory[pd_index] & PG_CONSISTENT_MASK;
		if ((flags & PG_CONSISTENT_MASK) != pde_flags) {
			error("flags are not consistent with page-directory entry");
			return false;
		}

		if (paging_enabled) {
			page_table = (pte_t*) (0xffc00000 + pd_index * PAGE_SIZE);
		} else {
			// identity mapping
			page_table = (pte_t*) (page_directory[pd_index] & PDE_MASK_ADDR);
		}

		// is there already a mapping present?
		if (page_table[pt_index] & PTE_MASK_PRESENT) {
			error("overwriting an already present mapping!");
			abort();
		}
	}

	// set the PTE
	page_table[pt_index] = phys_addr | flags | PTE_MASK_PRESENT;
	dbg("page 0x%x (phys) mapped to 0x%x (virt)", phys_addr, virt_addr);

	invalidate_tlb_page(virt_addr);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Unmaps the virtual page pointed by @virt_addr.
 *
 * If the page table hosting @virt_addr mapping becomes empty, the page table
 * is released and its PDE is updated accordingly (TODO).
 *
 * NOTE: @virt_addr must be page-aligned and must NOT point to a page
 * table/directory.
 *
 * Returns true on success, false otherwise.
 */

bool unmap_page(uint32_t virt_addr)
{
	uint32_t pd_index = PD_INDEX(virt_addr);
	uint32_t pt_index = PT_INDEX(virt_addr);
	pte_t *pg_table = NULL;

	dbg("unmapping page 0x%p", virt_addr);

	if (PAGE_OFFSET(virt_addr)) {
		error("address is not page aligned");
		return false;
	}

	if (pd_index == 1023) {
		// this is a serious issue
		error("cannot unmap page table/directory");
		abort();
	}

	if (PDE_PRESENT(pd_index) == false) {
		error("cannot unmap a page which has no page table");
		return false;
	}

	// retrieve page table address
	if (paging_enabled) {
		pg_table = (pte_t*) (0xffc00000 + pd_index * PAGE_SIZE);
	} else {
		// identity mapping
		pg_table = (pte_t*) (page_directory[pd_index] & PDE_MASK_ADDR);
	}
	dbg("pg_table = 0x%p", pg_table);

	// is virt_addr actually mapped?
	if ((pg_table[pt_index] & PTE_MASK_PRESENT) == false) {
		error("cannot unmap a page that is not present");
		return false;
	}

	// alright, everything is ok, unmap it
	pg_table[pt_index] &= ~(PTE_MASK_PRESENT|PTE_MASK_ADDR);

	invalidate_tlb_page((uint32_t)virt_addr);

	// TODO: scan the page table and unmap it if empty

	dbg("page 0x%p has been unmapped", virt_addr);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Setup an Identity Mapping for the first 4MB of memory and enable paging.
 */

void paging_setup(void)
{
	reg_t reg;
	pgframe_t pgd_phys_addr = 0;
	pde_t *pde = 0;

	info("paging setup...");

	if ((pgd_phys_addr = pfa_alloc(1)) == 0) {
		error("cannot allocate page_directory");
		abort();
	}
	dbg("pgd_phys_addr = 0x%p", pgd_phys_addr);

	// first clear the whole page directory
	for (size_t entry = 0; entry < (PAGE_SIZE/sizeof(pde_t)); ++entry) {
		pde = (pde_t*)(pgd_phys_addr + entry * sizeof(*pde));
		*pde = PDE_RW_KERNEL_NOCACHE;
	}

	/*
	 * Here comes the trick.
	 *
	 * We map the page-directory itself into the last page-directory entry
	 * so it can be manipulated as a page table from virtual address space.
	 *
	 * This is called "PDE self-mapping".
	 *
	 * That is, (virtual) 0xfffff000 points to the page directory's page
	 * table. In other words, we can modify the page directory by
	 * read/writing to 0xfffff000.
	 *
	 * Furthermore, because of this PDE self-mapping tricks, we can also
	 * quickly retrieves ANY pagetable's virtual address. In order to do so,
	 * we need to offset (virtual) address 0xfffc0000. That is, to get the
	 * second pagetable virtual address, we need 0xfffc0000 + 1*PAGE_SIZE.
	 *
	 * The MMU will first see the pd_index number 1023 and the 1023 entry
	 * is the last PDE which points to itself. Next, the pt_index will be
	 * 2, which points to our page table.
	 */

	// get the last entry physical address
	uint32_t last_pde_offset = ((PAGE_SIZE/sizeof(pde_t)) - 1) * sizeof(pde_t);
	pde = (pde_t*)(pgd_phys_addr + last_pde_offset);
	dbg("pgd's pde = 0x%p", pde);

	// make it point to the page-directory itself
	*pde = pgd_phys_addr | PDE_RW_KERNEL_NOCACHE | PDE_MASK_PRESENT;

	// here we lie to map_page() because paging is actually not enabled yet.
	page_directory = (pde_t*) pgd_phys_addr;
	bootstrap_mapping();

	// loads the physical address of page-directory into CR3
	if (load_page_directory(pgd_phys_addr) == false) {
		error("failed to load the new page directory");
		abort();
	}

	// finally, update page_directory to its correct virtual address
	page_directory = (pde_t*) 0xfffff000; // the very last virtual page

	// enable paging
	reg = read_cr0();
	reg.cr0.pg = 1;
	write_cr0(reg);

	paging_enabled = true;

	success("paging setup succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
