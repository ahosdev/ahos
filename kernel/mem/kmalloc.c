/*
 * kmalloc.c
 *
 * A (very basic) fine-grained memory allocator called "Ah! allocator".
 *
 * NOTE: as we don't handle paging nor virtual memory yet, this is a physical
 * memory allocator.
 *
 * DESIGN:
 *
 * The allocator basic unit is called a "block". A block is a page frame which
 * can be further divided into smaller chunk. Each block is independent from
 * each other.
 *
 * The block metadata is stored in the block itself. It holds the element
 * size, the number of elements hosted by the block as well as a chunkmap
 * to keep track of used/free chunks.
 *
 * Each block is divided into chunk of the same size which is dictated by the
 * first allocation (block creation).
 *
 * All blocks are linked together in a circular linked list accessible from
 * "first_block".
 *
 * Right now, when all chunks in a block are free, the block just sit there
 * (i.e. memory is not reclaimed).
 *
 * Why designing such a bad allocator? Hmm... ask Denis!
 *
 * TODO:
 * - handle big allocations
 * - use two trees: one for alloc (sort by size), one for free (sort by addr)
 * - maybe a single circular list is enough
 */

#include <mem/memory.h>

#include <string.h>

#define LOG_MODULE "kmalloc"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

typedef unsigned char chunk_type_t;

#define CHUNK_FREE ((chunk_type_t) 0)
#define CHUNK_USED ((chunk_type_t) 1)

// ----------------------------------------------------------------------------

// XXX: can we actually get rid of 'first_ptr' and save some memory?
struct aha_block {
	size_t elt_size;
	size_t tot_elts; // maxmimum number of elements
	size_t nb_frees;
	uint32_t first_ptr; // pointer to the first chunk
	struct aha_block *prev; // pointer to the previous block
	struct aha_block *next; // pointer to the next block
	chunk_type_t chunkmap[0]; // mark chunks as free/used (variable size)
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct aha_block *first_block = NULL;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct aha_block* new_block(size_t elt_size)
{
	size_t remaining = 0;
	size_t nb_elts = 0;
	struct aha_block *block = NULL;

	dbg("allocating new block");

	// first we substract the metadata size (chunkmap excluded)
	// we reserve extra bytes to guarantee 'first_ptr' is pointer aligned
	remaining = PAGE_SIZE - sizeof(struct aha_block) - sizeof(void*);

	nb_elts = remaining / (sizeof(chunk_type_t) + elt_size);

	if (nb_elts == 0) {
		// should never happen
		error("block cannot even hold a single element");
		abort();
	}

	if ((block = (struct aha_block*) pfa_alloc(1)) == NULL) {
		error("not enough memory");
		return NULL;
	}

	dbg("new_block: elt_size = %u (nb_elts=%u)", elt_size, nb_elts);

	block->elt_size = elt_size;
	block->tot_elts = nb_elts;
	block->nb_frees = nb_elts;
	// align first ptr to pointer boundary
	block->first_ptr = (uint32_t)block->chunkmap + nb_elts;
	//dbg("[before] first_ptr = 0x%p", block->first_ptr);
	if (block->first_ptr & (sizeof(void*) - 1)) {
		// align first_ptr to the next pointer boundary
		block->first_ptr +=
			sizeof(void*) - (block->first_ptr & (sizeof(void*) - 1));
	}
	dbg("first_ptr = 0x%p", block->first_ptr);
	block->prev = block;
	block->next = block;

	// chunkmap element are guaranteed to have 1 byte size.
	memset(block->chunkmap, CHUNK_FREE, nb_elts*sizeof(chunk_type_t));

	return block;
}

// ----------------------------------------------------------------------------

static struct aha_block* find_block(size_t size)
{
	struct aha_block *block = first_block;

	if (first_block == NULL) {
		return NULL;
	}

#if 0
	dbg("dumping block list");
	do {
		dbg("[0x%p] size=%d, free=%d, next=%p", block, block->elt_size, block->nb_frees, block->next);
		block = block->next;
	} while (block != first_block);
#endif

	//dbg("size=%d, free=%d, next=%p", block->elt_size, block->nb_frees, block->next);
	while ((block->elt_size != size || (block->nb_frees == 0))
			&& block->next != first_block)
	{
		block = block->next;
		//dbg("size=%d, free=%d, next=%p", block->elt_size, block->nb_frees, block->next);
	}

	if ((block->elt_size == size) && (block->nb_frees > 0)) {
		dbg("found block with free chunks (%d remain)", block->nb_frees);
		return block;
	}

	return NULL;
}

// ----------------------------------------------------------------------------

/*
 * Returns the next highest power of two (e.g. 127 -> 128, 129 -> 256).
 */

static size_t next_highest_power_of_two(size_t size)
{
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size++;

	return size;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Allocates @size bytes of memory.
 *
 * Returns the allocated memory area or NULL on error.
 */

void* kmalloc(size_t size)
{
	struct aha_block *block = NULL;

	dbg("allocating %d bytes", size);

	if (size == 0) {
		error("invalid argument");
		return NULL;
	} else if (size < 8) {
		// is it worth calling kmalloc() in those case ? Warn the caller.
		warn("very small allocation detected");
		// this is the minimal size
		size = 8;
	}

	/*
     * Round up to the next highest power-of-two.
     *
     * This has two benefits and one drawback. First, it reduces the external
     * fragmentation since less blocks will be created to handle every size
     * (thus reducing page frame allocation). Secondly, it guarantess that the
     * internal fragmentation will always be lesser than 50% (minus metadata
     * slack space).
     *
     * However, it means more memory will be wasted especially when allocating
     * an object which size is not close to its higher power-of-two (e.g. 130
     * bytes). Those objects should have *dedicated* blocks.
     */

	size = next_highest_power_of_two(size);
	dbg("new size %u", size);

	if (size >= PAGE_SIZE) {
		NOT_IMPLEMENTED();
	}

	dbg("searching block...");
	if ((block = find_block(size)) == NULL) {
		// we didn't find any block, create a new one
		dbg("no block found");
		if ((block = new_block(size)) == NULL) {
			error("failed to create new block");
			return NULL;
		}

		// insert it into the linked list
		if (first_block == NULL) {
			first_block = block;
		} else {
			block->next = first_block;
			block->prev = first_block->prev;
			first_block->prev = block;
			block->prev->next = block;
			first_block = block;
		}

		dbg("new block created");
	}

	dbg("found block 0x%p", block);

	// allocates a new chunk
	for (size_t chunk = 0; chunk < block->tot_elts; ++chunk) {
		if (block->chunkmap[chunk] == CHUNK_FREE) {
			block->chunkmap[chunk] = CHUNK_USED;
			block->nb_frees--;
			return (void*)(block->first_ptr + chunk*block->elt_size);
		}
	}

	error("found block does not have any free chunk!");
	abort();

	return NULL;
}

// ----------------------------------------------------------------------------

/*
 * Free the memory area pointed by @ptr.
 *
 * NOTE: the memory area must have been allocated with kmalloc().
 */

void kfree(void *ptr)
{
	struct aha_block *block = first_block;

	dbg("freeing 0x%p", ptr);

	if (ptr == NULL) {
		error("freeing NULL pointer");
		abort();
	}

	// search which block this @ptr might belong to
	do {
		if (((uint32_t)ptr >= block->first_ptr) &&
			((uint32_t)ptr < ((uint32_t)block + PAGE_SIZE)))
		{
			dbg("block found 0x%p", block);
			goto found;
		}
		block = block->next;
	} while (block != first_block);

	error("ptr (0x%p) does not belong to any block", ptr);
	abort();

found:
	// find the chunk
	for (size_t chunk = 0; chunk < block->tot_elts; ++chunk) {
		uint32_t chunk_ptr = block->first_ptr + chunk * block->elt_size;
		if (chunk_ptr == (uint32_t)ptr) {
			if (block->chunkmap[chunk] == CHUNK_FREE) {
				error("double-free detected!");
				abort();
			}
			// we got it
			dbg("chunk found: %d", chunk);
			block->chunkmap[chunk] = CHUNK_FREE;
			block->nb_frees++;
			// TODO: handle empty block
			return ;
		}
	}

	error("ptr (0x%p) hasn't matching chunk in block 0x%p", ptr, block);
	abort();
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
