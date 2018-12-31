/*
 * kmalloc.c
 *
 * A (very basic) fine-grained memory allocator called "Ah! allocator".
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
 * - handle missing cases (small allocs!)
 * - handle big allocations
 * - align pointers to 4 bytes boundary
 * - uses power-of-two sizes
 * - use two trees: one for alloc (sort by size), one for free (sort by addr)
 * - maybe a single circular list is enough
 */

#include <kernel/memory.h>

#undef LOG_MODULE
#define LOG_MODULE "kmalloc"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum chunk_type {
	CHUNK_FREE,
	CHUNK_USED,
};

// ----------------------------------------------------------------------------

// XXX: can we actually get rid of 'first_ptr' and save some memory?
struct aha_block {
	size_t elt_size;
	size_t tot_elts; // maxmimum number of elements
	size_t nb_frees;
	uint32_t first_ptr; // pointer to the first chunk
	struct aha_block *prev; // pointer to the previous block
	struct aha_block *next; // pointer to the next block
	enum chunk_type chunkmap[0]; // mark chunks as free/used (variable size)
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
	struct aha_block *block = (struct aha_block*) pfa_alloc();

	dbg("allocating new block");

	if (block == NULL) {
		error("not enough memory");
		return NULL;
	}

	// TODO: align size to 4 bytes

	dbg("new_block: elt_size = %d", elt_size);

	size_t block_size = PAGE_SIZE - sizeof(struct aha_block);
	//dbg("block_size = %d", block_size);

	size_t max_elt_per_block_size = block_size / elt_size;
	dbg("max_elt_per_block_size = %d", max_elt_per_block_size);

	size_t remaining = block_size - (max_elt_per_block_size * elt_size);
	//dbg("remaining = %d", remaining);

	if (remaining >= max_elt_per_block_size) {
		// all fine, we can store chunks in slack space
		block->elt_size = elt_size;
		block->tot_elts = max_elt_per_block_size;
		block->nb_frees = max_elt_per_block_size;
		// TODO: align first_ptr to 4 bytes boundary
		block->first_ptr = (uint32_t)block->chunkmap + block->tot_elts;
		block->prev = block;
		block->next = block;
		for (size_t elt = 0; elt < block->tot_elts; ++elt) {
			block->chunkmap[elt] = CHUNK_FREE;
		}
	} else {
		// we need to reduce the number of element per block until we can
		// store the chunkmap
		NOT_IMPLEMENTED();
	}

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
	}

	// TODO: compute the closest power-of-two to reduce external fragmentation

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
	} while (block->next != first_block);

	error("ptr (0x%p) does not belong to any block", ptr);
	abort();

found:
	// find the chunk
	for (size_t chunk = 0; chunk < block->tot_elts; ++chunk) {
		uint32_t chunk_ptr = block->first_ptr + chunk*block->elt_size;
		if (chunk_ptr == (uint32_t)ptr) {
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
