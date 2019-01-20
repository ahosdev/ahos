/*
 * symbol.c
 *
 * Kernel symbol facility.
 */

#include <kernel/symbol.h>

#include <mem/pmm.h>

#define LOG_MODULE "symbol"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Finds the closest symbol of @addr and fills the @sym structure.
 *
 * NOTE: If symbol A is really big, followed by a symbol B, even if @addr is
 * closest to B, it will returns A (i.e. the highest symbol before @addr).
 *
 * Returns true on success, false otherwise.
 */

bool symbol_find(void *addr, struct symbol *sym)
{
	dbg("searching symbol at 0x%p", addr);

	if (addr == NULL || sym == NULL) {
		error("invalid argument");
		return false;
	}

	if (module_len == 0) {
		error("cannot find symbol if module isn't loaded");
		return false;
	}

	// TODO

	return false;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
