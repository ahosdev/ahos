/*
 * symbol.h
 *
 * Kernel symbol facility.
 */

#ifndef KERNEL_SYMBOL_H_
#define KERNEL_SYMBOL_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define SYMBOL_MAX_LEN 96

struct symbol {
	void *addr;
	size_t len;
	char name[SYMBOL_MAX_LEN];
};

// ----------------------------------------------------------------------------

bool symbol_find(void *addr, struct symbol *sym);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_SYMBOL_H_ */
