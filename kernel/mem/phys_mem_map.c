/*
 * phys_mem_map.c
 *
 * Physical Memory Mapping handling.
 *
 * Documentation:
 * - https://wiki.osdev.org/Memory_Map_(x86)
 */

#include <mem/memory.h>

#include <string.h>

#define LOG_MODULE "memory"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Right now we only make space for two more reserved region (phys_mem_map and
 * kernel image). Past this point, the region hosting the phys_mem_map itself
 * will overlap another region. Hence the hard limit.
 */

#define MAX_RESERVED 2

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

static void dump_phys_mem_map(void)
{
	dbg("-----[ dumping phys_mem_map ]-----");

	for (size_t entry = 0; entry < phys_mem_map->len; ++entry)
	{
		struct phys_mmap_entry *pmme = &phys_mem_map->entries[entry];
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

	// search an available region of @len bytes starting from @addr
	for (entry = 0; entry < phys_mem_map->len; ++entry) {
		pmme = &phys_mem_map->entries[entry];
		//dbg("entry = %d", entry);
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
	} else {
		// the region needs to be splitted
		// [pmme->start, addr-1] [addr, len-1] [addr+len, pme->start+len]
		if (addr > pmme->addr) {
			// insert a new available region before
			NOT_IMPLEMENTED();
		} else {
			// assuming addr == pmme->start and len < pmme->len
			struct phys_mmap_entry new_pmme;
			size_t nb_regions_moved = 0;

			memset(&new_pmme, 0, sizeof(new_pmme));
			new_pmme.addr = addr;
			new_pmme.len = len;
			new_pmme.type = MMAP_TYPE_RESERVED;

			// reduce the selected region
			pmme->len -= len;
			pmme->addr += len;

			// make room for the new entry
			nb_regions_moved = phys_mem_map->len - entry;
			//dbg("nb moved = %d", nb_regions_moved);
			memmove(&phys_mem_map->entries[entry+1],
					&phys_mem_map->entries[entry],
					nb_regions_moved * sizeof(struct phys_mmap_entry));

			// insert the new entry
			memcpy(&phys_mem_map->entries[entry], &new_pmme, sizeof(new_pmme));
			phys_mem_map->len++;

			nb_reserved++;
		}
	}

	dbg("region from 0x%x to 0x%x reserved", addr, addr+len-1);

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initializes the memory map from multiboot information.
 *
 * Arguments:
 * - mmap_addr: pointer to the first memory map entry
 * - mmap_length: total size of the memory map buffer
 *
 * Returns true on success, false otherwise.
 *
 * NOTE:
 *
 * Okay, we have an egg'n'chicken issue here. We need to store the memory
 * map in memory but we don't have any memory allocator yet AND the memory
 * map provided by multiboot is of variable size.
 *
 * Furthermore, multiboot marks our kernel image (code, rodata, data, bss)
 * as "available", so we must be careful to not overwrite it. In addition,
 * some I/O mapped region (such as video memory) are not listed in multi-
 * boot. This is just considered as a "hole" (neither reserved nor available).
 *
 * Finally, the multiboot information structure is also stored in memory
 * and shouldn't be overwritten until we don't need it anymore. Just like
 * the kernel image, is is currently sitting in "available" memory.
 */

bool phys_mem_map_init(multiboot_uint32_t mmap_addr, multiboot_uint32_t mmap_length)
{
	multiboot_memory_map_t *mmap = NULL;
	size_t nb_regions = 0;
	size_t needed_mem = 0;
	size_t entry = 0;
	size_t len = 0;

	info("initializing memory map...");

	// compute the number of region detected by boot loader
	nb_regions = mmap_length / sizeof(*mmap);
	dbg("%u memory region detected", nb_regions);

	// compute the memory needed to store those region information. We add two
	// since we will need to store the phys_mem_map (itself) and kernel image
	// regions
	needed_mem = (nb_regions + MAX_RESERVED) * sizeof(struct phys_mmap_entry);
	dbg("need %d bytes of memory", needed_mem);

	for (mmap = (multiboot_memory_map_t *) mmap_addr;
		(unsigned long) mmap < mmap_addr + mmap_length;
		mmap = (multiboot_memory_map_t *) ((unsigned long) mmap
			+ mmap->size + sizeof (mmap->size)))
	{
		uint32_t cur_addr = mmap->addr & 0xffffffff;

		if ((mmap->addr + mmap->len - 1) >> 32) {
			error("64-bits addresses not supported (too much memory)");
			NOT_IMPLEMENTED();
		}

#if 0
		dbg(" base_addr = 0x%x%08x,"
			" length = 0x%x%08x, type = 0x%x",
			(unsigned) (mmap->addr >> 32),
			(unsigned) (mmap->addr & 0xffffffff),
			(unsigned) (mmap->len >> 32),
			(unsigned) (mmap->len & 0xffffffff),
			(unsigned) mmap->type);
#endif

		if (needed_mem > (mmap->len & 0xffffffff)) {
			dbg("not enough space in this region");
			continue;
		}

		// doesn't collides with multiboot or kernel image
		if (collides(cur_addr, needed_mem, (uint32_t) mmap_addr, mmap_length) ||
			collides(cur_addr, needed_mem, kernel_start, kernel_end))
		{
			dbg("phys_mmap cannot fit into this region (will collides)");
			continue;
		}

		// we found a suitable place, fill the phys_mmap
		phys_mem_map = (struct phys_mmap*) cur_addr;
		// the phys_mem_map and kernel image region aren't marked yet
		phys_mem_map->len = nb_regions;
		dbg("initializing phys_mem_map at 0x%x (%u entries)",
			phys_mem_map, nb_regions);
		goto found;
	}

	// we didn't find any region big enough (we've got a serious issue!)
	error("cannot find a suitable memory region");
	return false;

found:
	// fill the phys_mem_map
	for (mmap = (multiboot_memory_map_t *) mmap_addr;
		(unsigned long) mmap < mmap_addr + mmap_length;
		mmap = (multiboot_memory_map_t *) ((unsigned long) mmap
			+ mmap->size + sizeof (mmap->size)), entry++)
	{
		struct phys_mmap_entry *pmme = &phys_mem_map->entries[entry];
		pmme->addr = (uint32_t)(mmap->addr & 0xffffffff);
		pmme->len = mmap->len & 0xffffffff;
		pmme->type = mmap->type;
	}

	dump_phys_mem_map();

	//  mark the phys_mem_map itself as reserved
	len = phys_mem_map->len * sizeof(phys_mem_map->entries[0]);
	if (reserve_region((uint32_t)phys_mem_map, len) == false) {
		error("failed to reserve phys_mem_map region");
		abort();
	}

	//  mark the kernel image as reserved
	//dbg("kernel_start = 0x%x", &kernel_start);
	//dbg("kernel_end = 0x%x", &kernel_end);
	len = (uint32_t)&kernel_end - (uint32_t)&kernel_start + 1;
	if (reserve_region((uint32_t)&kernel_start, len) == false) {
		error("failed to reserve kernel image region");
		abort();
	}

	dump_phys_mem_map();

	success("memory map initialization succeed");

	return true;
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

	dump_phys_mem_map();

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

		dump_phys_mem_map();

		return true;
	}

	dbg("failed to find a memory region");
	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
