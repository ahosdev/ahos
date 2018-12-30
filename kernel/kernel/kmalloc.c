/*
 * kmalloc.c
 *
 * A (very basic) fine-grained memory allocator.
 */

#include <kernel/memory.h>

#undef LOG_MODULE
#define LOG_MODULE "kmalloc"

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
	if (size == 0) {
		error("invalid argument");
		return NULL;
	}

	if (size >= PAGE_SIZE) {
		NOT_IMPLEMENTED();
	}

	// TODO

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
	if (ptr == NULL) {
		error("freeing NULL pointer");
		abort();
	}

	// TODO
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
