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
	size_t len; // (optionnal) might be zero
	char name[SYMBOL_MAX_LEN]; // a null terminated string
};

// ----------------------------------------------------------------------------

bool symbol_init(char *symbol_map_start, size_t symbol_map_len);
bool symbol_find(void *addr, struct symbol *sym);
bool symbol_lookup(char *name, struct symbol *sym);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_SYMBOL_H_ */
