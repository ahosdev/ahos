/*
 * phys_mem_map.c
 *
 * Physical Memory Map (layout) Handling.
 *
 * The goal here is to design a physical memory layout such as:
 *
 *		+-------------------+ 0x100000 (1MB)
 *		| kernel image      |
 *		+-------------------+ <--- page aligned
 *		| phys memory map   |
 *		+-------------------+ <--- page aligned
 *		| [optional] initrd |
 *		+-------------------+
 *
 * Multiboot has been filled by the bootloader and is sitting "somewhere"
 * in memory. While the multiboot_info structure has a fixed size and is
 * pointed by @mbi, other multiboot structures might vary in size. There
 * is no guarantee that they are contiguous from @mbi.
 *
 * In addition, the memory detection (from bootloader) does NOT reserve any
 * memory region that we are currently using (such as the kernel or the
 * multiboot structures themselves). They sit in "available" memory.
 *
 * Note that memory below the first megabyte (low mem) is completly ignored.
 * It can be either available, reserved, or whatever. However, it can be used
 * later on (if available) for DMA or legacy ISA devices.
 *
 * The only guarantee is the kernel image is completly loaded at the first
 * megabyte.
 *
 * Available memory past the "initrd" can (and must) be used by the page
 * frame allocator.
 *
 * Finally, once the phys mem map initialization is complete, the @mbi as well
 * as other multiboot structures are trashed and shouldn't be dereferenced
 * anymore.
 *
 * If, for some reasons, some multiboot structures landed after the kernel
 * image, the "phys mem map" as well as "initrd" can be placed in temporary
 * locations. Once the process is complete, the whole memory layout is
 * "repacked", overwriting (now trash) multiboot structures in the process.
 *
 * Documentation:
 * - https://wiki.osdev.org/Memory_Map_(x86)
 * - https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 * - https://wiki.osdev.org/Multiboot
 */

#include <mem/memory.h>

#include <string.h>

#define LOG_MODULE "physmm"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define MAX_RESERVED 3 // kernel + pmm + module

// ----------------------------------------------------------------------------

enum phys_mmap_type {
	// follows the multiboot specification (do not re-order)
	MMAP_TYPE_AVAILABLE = 1,
	MMAP_TYPE_RESERVED,
	MMAP_TYPE_ACPI,
	MMAP_TYPE_NVS,
	MMAP_TYPE_BADRAM,
};

// ----------------------------------------------------------------------------

// describe a memory region
struct phys_mmap_entry {
	uint32_t addr; // starting address (physical)
	size_t len; // len in bytes
	enum phys_mmap_type type;
};

// ----------------------------------------------------------------------------

struct phys_mmap {
	size_t len; // number of entries
	struct phys_mmap_entry entries[0];
	// data will be appended here
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct phys_mmap *phys_mem_map = NULL;
static void *module_addr = NULL;
static size_t module_len = 0;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Helper macro to manipulate multiboot memory map.
 */

#define mmap_first(mbi) \
	(multiboot_memory_map_t *) mbi->mmap_addr

#define mmap_next(mmap) \
	(multiboot_memory_map_t*) ((unsigned long) mmap \
		+ mmap->size + sizeof(mmap->size))

#define mmap_for_each(mmap, mbi) \
	for (mmap = mmap_first(mbi); \
		(unsigned long) mmap < mbi->mmap_addr + mbi->mmap_length; \
		mmap = mmap_next(mmap))

#define mmap_dump(mmap)\
	dbg("base_addr = 0x%x%08x length = 0x%x%08x, type = 0x%x",\
		(unsigned) (mmap->addr >> 32),\
		(unsigned) (mmap->addr & 0xffffffff),\
		(unsigned) (mmap->len >> 32),\
		(unsigned) (mmap->len & 0xffffffff),\
		(unsigned) mmap->type);

#define mmap_swap(a, b, swap) \
	swap = *a; \
	*a = *b; \
	*b = swap;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static bool collides(uint32_t src_addr, uint32_t src_len,
					 uint32_t dst_addr, uint32_t dst_len)
{
	if ((src_addr < (dst_addr + dst_len)) &&
		((src_addr + src_len) > dst_addr))
	{
		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------

static void dump_phys_mem_map(struct phys_mmap *pmm)
{
	dbg("-----[ dumping phys_mem_map ]-----");

	for (size_t entry = 0; entry < pmm->len; ++entry)
	{
		struct phys_mmap_entry *pmme = &pmm->entries[entry];
		char *type = NULL;

		switch (pmme->type) {
			case MMAP_TYPE_AVAILABLE: type = "AVAILABLE"; break;
			case MMAP_TYPE_RESERVED: type = "RESERVED"; break;
			case MMAP_TYPE_ACPI: type = "ACPI"; break;
			case MMAP_TYPE_NVS: type = "NVS"; break;
			case MMAP_TYPE_BADRAM: type = "BADRAM"; break;
			default: type = "UNKNOWN"; break;
		}

		dbg("[0x%08x - 0x%08x] %s",
			pmme->addr, (uint32_t)pmme->addr + pmme->len - 1, type);
	}

	dbg("----------------------------------");
}

// ----------------------------------------------------------------------------

/*
 * Splits the @entry region at @addr (must be in entry's range, boundary
 * are excluded).
 *
 * WARNING: It is expected that @pmm is big enough to hold an additional entry,
 * otherwise this will lead to an out-of-bound memory corruption.
 *
 * Returns true on success, false otherwise.
 */

static bool split_region(struct phys_mmap *pmm, size_t entry, uint32_t addr)
{
	struct phys_mmap_entry *pmme = NULL;
	struct phys_mmap_entry *next = NULL;

	if (entry >= pmm->len) {
		error("invalid argument");
		return false;
	}

	pmme = &pmm->entries[entry];
	// we cannot split at the boundary
	if ((addr <= pmme->addr) || (addr >= (pmme->addr + pmme->len))) {
		error("addr is not in range");
		return false;
	}

	// shift all entries after @entry assuming there is enough space
	memmove(&pmm->entries[entry+2],
			&pmm->entries[entry+1],
			(pmm->len - (entry + 1)) * sizeof(*pmme));
	pmm->len++;

	next = &pmm->entries[entry + 1];
	next->type = pmme->type;
	next->addr = addr;
	next->len = pmme->len - (addr - pmme->addr);
	pmme->len -= next->len;

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Search an available region of @len bytes, starting at @addr.
 *
 * Regions in phys_mem_map are expected to be sorted by address and doesn't
 * overlap.
 *
 * Returns true on success, false otherwise.
 */

static bool reserve_region(uint32_t addr, size_t len)
{
	static size_t nb_reserved = 0;
	struct phys_mmap_entry *pmme = NULL;
	size_t entry = 0;

	if (nb_reserved == MAX_RESERVED) {
		error("cannot reserve more region");
		return false;
	}

retry:
	// search an available region of @len bytes starting from @addr
	for (entry = 0; entry < phys_mem_map->len; ++entry) {
		pmme = &phys_mem_map->entries[entry];
		if (pmme->type != MMAP_TYPE_AVAILABLE) {
			continue;
		}

		if (addr >= pmme->addr && ((addr+len) <= (pmme->addr + pmme->len))) {
			goto found;
		}
	}

	error("cannot reserve region");
	return false;

found:
	dbg("entry found at %d", entry);
	if (pmme->len == len) {
		// this occupy the whole region, no need for splitting
		pmme->type = MMAP_TYPE_RESERVED;
		nb_reserved++;
	} else {
		// the region needs to be splitted
		// [pmme->start, addr-1] [addr, len-1] [addr+len, pme->start+len]
		uint32_t split_addr = (addr > pmme->addr) ? addr : (addr+len);
		if (split_region(phys_mem_map, entry, split_addr) == false) {
			error("failed to split region at 0x%p", split_addr);
			return false;
		}
		dbg("splitted region at 0x%p", split_addr);
		goto retry;
	}

	dbg("region from 0x%x to 0x%x reserved", addr, addr+len-1);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Computes the total size (in bytes) of the phys_mem_map.
 *
 * Check if there is an additionnal single module loaded (initrd) from @mbi.
 *
 * Returns the size in bytes.
 */

static size_t phys_mem_map_size(multiboot_info_t *mbi)
{
	size_t nb_entries;
	size_t size;

	// add each memory map entries...
	nb_entries = (mbi->mmap_length / sizeof(multiboot_memory_map_t));

	// ...save some space for the kernel and the pmm itself...
	nb_entries += 2;

	// ...some more if there is a module.
	nb_entries += !!(mbi->flags & MULTIBOOT_INFO_MODS);

	// in the worst scenario case, each "reserve" will split an available
	// region in three parts (two availables, one reserved). Make room for
	// them.
	nb_entries *= 3;

	size = sizeof(struct phys_mmap) +
		nb_entries * sizeof(struct phys_mmap_entry);
	dbg("phys mem map has %u entries (size = %u bytes)", nb_entries, size);

	return size;
}

// ----------------------------------------------------------------------------

#define dump_range(start, end, name) \
	dbg("[0x%08x - 0x%08x] %s", start, end - 1, name)

static void dump_multitboot(multiboot_info_t* mbi)
{
	dbg("-------[ dump multiboot");

	dump_range(kernel_start, kernel_end, "kernel");

	dump_range(mbi, (uint32_t)mbi + sizeof(*mbi), "mbi");

	if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count) {
		for (size_t i = 0; i < mbi->mods_count; ++i) {
			multiboot_module_t *mod =
				(multiboot_module_t*) ((uint32_t)mbi->mods_addr + i*sizeof(*mod));
			dump_range(mod, (uint32_t)mod + sizeof(*mod), "mod_header");
			dump_range(mod->mod_start, mod->mod_end, "mod");
		}
	}

	if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
		multiboot_memory_map_t *mmap;
		dump_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length, "mem map");
		mmap_for_each(mmap, mbi) {
			if (mmap->type == MMAP_TYPE_AVAILABLE) {
				dbg("base_addr = 0x%x%08x,"
					"length = 0x%x%08x, type = 0x%x",
					(unsigned) (mmap->addr >> 32),
					(unsigned) (mmap->addr & 0xffffffff),
					(unsigned) (mmap->len >> 32),
					(unsigned) (mmap->len & 0xffffffff),
					(unsigned) mmap->type);
			}
		}

	}

	dbg("-------[ end-of-dump multiboot");
}

// ----------------------------------------------------------------------------

/*
 * Bubble sort (small number of elements) multiboot memory map by address.
 *
 * It assumes that no region overlap each others.
 */

static void sort_multiboot_mmap(multiboot_info_t *mbi)
{
	multiboot_memory_map_t swap;
	multiboot_memory_map_t *mmap = NULL;
	multiboot_memory_map_t *next = NULL;
	size_t nb_elts = mbi->mmap_length / sizeof(multiboot_memory_map_t);
	size_t last_swap = 0;

	while (nb_elts > 0) {
		last_swap = 0;
		mmap = (multiboot_memory_map_t*) mbi->mmap_addr;
		for (size_t i = 0; i < (nb_elts - 1); ++i) {
			next = mmap_next(mmap);
			if (mmap->addr > next->addr) {
				mmap_swap(mmap, next, swap);
				last_swap = i+1;
				mmap = mmap_next(mmap);
			} else {
				mmap = next;
			}
		}
		nb_elts = last_swap; // optim, yeah! ...
	}
}

// ----------------------------------------------------------------------------

/*
 * Finds a suitable location to store the phys mem map without overlapping the
 * kernel, multiboot structures or the (optional) initrd.
 *
 * NOTE: "Low Memory" region(s) (< 1MB) are discarded.
 *
 * Returns the physical address where a phys mem map of @pmm_size bytes can
 * safely be stored, or -1 on error.
 */

static uint32_t find_pmm_location(multiboot_info_t *mbi, struct phys_mmap *mbr,
								  size_t pmm_size)
{
	multiboot_memory_map_t *mmap = NULL;

	if (pmm_size == 0) {
		error("pmm_size is zero");
		return -1;
	}

	// multiboot does not guarantee that mmap are sorted, now they are
	sort_multiboot_mmap(mbi);

	mmap_for_each(mmap, mbi) {
		mmap_dump(mmap);

		if (mmap->type != MMAP_TYPE_AVAILABLE) {
			continue;
		}

		// beyond the 4GB limit ?
		if (mmap->addr >> 32) {
			warn("memory above 4GB is not supported");
			continue;
		}

		uint32_t seg_addr = mmap->addr & 0xffffffff; // 32-bits only
		uint32_t seg_len  = mmap->len & 0xffffffff; // 32-bits only

		 // more than 4GB available?
		if (mmap->len >> 32) {
			// shrink it
			seg_len = 0xffffffff - seg_addr + 1;
		}

		// region in low memory?
		if (seg_addr < 0x100000) {
			// we are guaranteed that there is a hole in low mem (e.g. VRAM),
			// so we don't need to consider the length (i.e. split)
			continue;
		}

		/*
		 * alright... we have a valid 32-bits segment within:
		 *
		 *		[seg_addr, seg_addr + seg_len -1]
		 *
		 * where: seg_addr is not in low memory
		 */

		// starts after the kernel image
		uint32_t pmm_addr = page_align(kernel_end + 1);

		// segment starts after current pmm?
		if (pmm_addr < seg_addr) {
			// pmm now starts at the segment beginning
			pmm_addr = seg_addr;
		}

		// checks if it overlap any multiboot entry (must be sorted)
		for (size_t i = 0; i < mbr->len; ++i) {
			struct phys_mmap_entry *entry = &mbr->entries[i];
			if (collides(pmm_addr, pmm_size, entry->addr, entry->len)) {
				pmm_addr = page_align(entry->addr + entry->len);
			}
		}

		// are we still in range?
		uint32_t seg_end = seg_addr + seg_len;
		uint32_t pmm_end = pmm_addr + pmm_size;
		if (pmm_end <= seg_end) {
			// yes, we found our location!
			return pmm_addr;
		}
	}

	return -1;
}

// ----------------------------------------------------------------------------

/*
 * Sorts @pmm's entries by addresses.
 */

static void sort_phys_mmap(struct phys_mmap *pmm)
{
	struct phys_mmap_entry *entry = NULL;
	struct phys_mmap_entry *next = NULL;
	struct phys_mmap_entry tmp;
	size_t last_swap = 0;
	size_t nb_unsorted = 0;

	/*
	 * It's okay to use a bubble sort here as the list is expected to be very
	 * small. In addition, it is very simple and works "in place".
	 */

	nb_unsorted = pmm->len;
	while (nb_unsorted > 0) {
		last_swap = 0;
		for (size_t i = 0; i < (nb_unsorted - 1); ++i) {
			entry = &pmm->entries[i];
			next  = &pmm->entries[i + 1];
			if (entry->addr > next->addr) {
				// swap
				tmp = *next;
				*next = *entry;
				*entry = tmp;
				last_swap = i + 1;
			}
		}
		nb_unsorted = last_swap;
	}
}

// ----------------------------------------------------------------------------

/*
 * Identify where each pieces of multiboot are located in memory and store it
 * in @mb_regions (sorted).
 *
 * The @mb_regions argument is expected to be big enough to hold all infos.
 *
 * NOTE: If the MULTIBOOT_INFO_MODS flag is set, only one module is expected.
 *
 * Returns true on success, false otherwise.
 */

static bool identify_multiboot_regions(multiboot_info_t *mbi,
									   struct phys_mmap *mb_regions)
{
	struct phys_mmap_entry *entry = NULL;

	dbg("identifying multiboot regions");

	mb_regions->len = 0;

	// multiboot info header
	entry = &mb_regions->entries[mb_regions->len++];
	entry->addr = (uint32_t) mbi;
	entry->len = sizeof(*mbi);

	// memory map
	entry = &mb_regions->entries[mb_regions->len++];
	entry->addr = mbi->mmap_addr;
	entry->len = mbi->mmap_length;

	// optional module (zero or one)
	if (mbi->flags & MULTIBOOT_INFO_MODS) {
		multiboot_module_t *mod = (multiboot_module_t*) mbi->mods_addr;

		// module structure
		entry = &mb_regions->entries[mb_regions->len++];
		entry->addr = mbi->mods_addr;
		entry->len = sizeof(multiboot_module_t);

		// module itself
		entry = &mb_regions->entries[mb_regions->len++];
		entry->addr = mod->mod_start;
		entry->len = mod->mod_end - mod->mod_start; // mod_end is exclusive
	}

	sort_phys_mmap(mb_regions);

	return true;
}

// ----------------------------------------------------------------------------

/*
 * Fills @pmm from the @mbi's memory map.
 */

static void fill_phys_mmap(multiboot_info_t *mbi, struct phys_mmap *pmm)
{
	multiboot_memory_map_t *mmap = NULL;
	size_t entry = 0;

	mmap_for_each(mmap, mbi) {
		struct phys_mmap_entry *pmme = &pmm->entries[entry];

		if (mmap->addr >> 32) {
			warn("ignoring memory above 4GB");
			continue;
		}

		pmme->addr = (uint32_t)(mmap->addr & 0xffffffff);

		 // more than 4GB available?
		if (mmap->len >> 32) {
			// shrink it
			pmme->len = 0xffffffff -pmme->addr + 1;
		} else {
			pmme->len = (uint32_t) (mmap->len & 0xffffffff);
		}

		pmme->type = mmap->type;
		entry++;
	}

	pmm->len = entry;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the memory map from multiboot information.
 *
 * Returns true on success, false otherwise.
 */

bool phys_mem_map_init(multiboot_info_t *mbi)
{
	size_t nb_multiboot_regions = 0;
	struct phys_mmap *mb_regions = NULL;
	size_t mbr_size = 0;
	bool ret = false;

	info("initializing physical memory map...");

	// this is nonsense to continue here if we don't have memory map
	if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) == 0) {
		error("memory map from multiboot is required");
		return false;
	}

	// count the number of multiboot regions to not overwrite
	nb_multiboot_regions = 2; // the mbi and the memory map
	if (mbi->flags & MULTIBOOT_INFO_MODS) {
		// XXX: QEMU set the MULTIBOOT_INFO_MODS flags without modules
		if (mbi->mods_count > 0) {
			if (mbi->mods_count != 1) {
				error("only one module (initrd) is expected");
				return false;
			}
			// one for multiboot module structure and one for module itself
			nb_multiboot_regions += 2;
		} else {
			// remove the flag if there is no module
			mbi->flags &= ~MULTIBOOT_INFO_MODS;
		}
	}
	dbg("nb_multiboot_regions = %u", nb_multiboot_regions);

	mbr_size = sizeof(*mb_regions) +
		nb_multiboot_regions * sizeof(struct phys_mmap_entry);
	dbg("mbr_size = %u", mbr_size);

	// TODO: check if it will stack overflow
	stack_alloc(mbr_size, mb_regions);
	memset(mb_regions, 0, mbr_size);
	dbg("mbi_regions = 0x%p", mb_regions);

	dump_multitboot(mbi);
	if (identify_multiboot_regions(mbi, mb_regions) == false) {
		error("failed to identify multiboot region");
		goto out;
	}
	//dump_phys_mem_map(mb_regions);
	dbg("multiboot regions identified");

	/*
	 * first we need to compute the final size of the phys_mem_map
	 */

	size_t pmm_size = phys_mem_map_size(mbi);
	dbg("pmm_size = %u", pmm_size);

	/*
	 * then we need to find a location where it won't overlap the kernel or
	 * existing multiboot structures (including the not copied yet 'initrd'
	 * if any)
	 */

	uint32_t pmm_addr = find_pmm_location(mbi, mb_regions, pmm_size);
	if (pmm_addr == (uint32_t)-1) {
		error("cannot find a suitable location for phys mem map");
		goto out;
	}
	dbg("pmm_addr = 0x%p", pmm_addr);

	/*
	 * from here, we can fill it with memory detection from multiboot (but
	 * don't reserve any region yet)
	 */

	phys_mem_map = (struct phys_mmap*) pmm_addr;
	fill_phys_mmap(mbi, phys_mem_map);
	dump_phys_mem_map(phys_mem_map);

	/*
	 * the next step is to reserve the kernel and the phys mem map. No need
	 * to copy memory here as the kernel is already loaded and we filled the
	 * phys mem map in the previous step
	 */

	if (reserve_region(kernel_start, kernel_end - kernel_start) == false) {
		error("failed to reserved kernel region");
		goto out;
	}

	if (reserve_region(pmm_addr, pmm_size) == false) {
		error("failed to reserve phys mem map region");
		goto out;
	}

	// then reserve the initrd region (if any)
	if (mbi->flags & MULTIBOOT_INFO_MODS) {
		multiboot_module_t *mod = (multiboot_module_t*) mbi->mods_addr;
		if (reserve_region(mod->mod_start, mod->mod_end - mod->mod_start) == false) {
			error("failed to reserve module region");
			goto out;
		}
		module_addr = (void*) mod->mod_start;
		module_len = mod->mod_end - mod->mod_start;
		info("module loaded at 0x%p (%u bytes)", module_addr, module_len);
		dump_phys_mem_map(phys_mem_map);
	}

	ret = true;
	success("memory map initialization succeed");

out:
	stack_free(mbr_size); // don't forget it or the stack will be misaligned
	return ret;
}

// ----------------------------------------------------------------------------

/*
 * Find the largest available contiguous memory region starting after
 * @from_addr (typically, after kernel image) and reserve it.
 *
 * On success, @addr and @len are set and true is returned. Otherwise, @addr
 * and @len are untouched and false is returned.
 */

bool phys_mem_map_reserve(uint32_t from_addr, uint32_t *addr, size_t *len)
{
	dbg("reserving memory after 0x%x", from_addr);

	if (addr == NULL || len == NULL) {
		error("invalid argument");
		return false;
	}

	dump_phys_mem_map(phys_mem_map);

	for (size_t entry = 0; entry < phys_mem_map->len; ++entry) {
		struct phys_mmap_entry *pmme = &phys_mem_map->entries[entry];

		if (pmme->type != MMAP_TYPE_AVAILABLE || pmme->addr < from_addr) {
			continue;
		}

		// we found one
		dbg("found an available memory region at 0x%x", pmme->addr);
		pmme->type = MMAP_TYPE_RESERVED; // reserve the whole region
		*addr = pmme->addr;
		*len = pmme->len;

		dump_phys_mem_map(phys_mem_map);

		return true;
	}

	dbg("failed to find a memory region");
	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
