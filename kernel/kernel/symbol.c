/*
 * symbol.c
 *
 * Kernel symbol facility.
 */

#include <kernel/symbol.h>

#include <mem/pmm.h>
#include <mem/memory.h>

#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "symbol"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

struct symbol_map
{
	size_t nb_syms;
	struct symbol *symbols;
};

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static struct symbol_map sym_map;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Parses a single @line (null terminated) from a memory mapped symbol map file
 * and update the @sym content.
 *
 * WARNING: just like parse_symbol_map(), the file content is updated (and
 * restored) during parsing (see comments).
 *
 * This parser expects the following grammar:
 *
 * SYMBOL_NAME SYMBOL_TYPE SYMBOL_ADDR [SYMBOL_LEN]\0
 *									  ^   ^--- optionnal
 *									  \--- mandatory
 *
 * Returns true on success, false otherwise.
 */

static bool parse_line(char *line, struct symbol *sym)
{
	char *sep = NULL;
	size_t name_len = 0;
	size_t addr_len = 0;

	if (line == NULL || *line == '\0' || sym == NULL) {
		error("invalid argument");
		return false;
	}

	dbg("parsing line '%s'", line);

	// extract symbol name
	if ((sep = strchr(line, ' ')) == NULL) {
		error("unexpected character");
		return false;
	}
	*sep = '\0'; // update file content
	if ((name_len = strlen(line)) == 0) {
		error("empty symbol name");
		goto fail;
	} else if (name_len >= sizeof(sym->name)) {
		error("symbol name is too big");
		goto fail;
	}
	strncpy(sym->name, line, sizeof(sym->name) - 1);
	sym->name[sizeof(sym->name) - 1] = '\0';
	dbg("sym->name = %s", sym->name);
	*sep = ' '; // restore file content
	line = sep + 1;

	// extract symbol type
	if ((sep = strchr(line, ' ')) == NULL) {
		error("unexpected character");
		return false;
	}
	line = sep + 1; // skip it

	// extract symbol starting addr (in hexa)
	if ((sep = strchr(line, ' ')) == NULL) {
		error("unexpected character");
		return false;
	}
	*sep = '\0'; // update file content
	if (strlen(line) > 2*sizeof(sym->addr)) {
		error("address is too large");
		goto fail;
	}
	sym->addr = (void*) atoh(line);
	dbg("sym->addr = 0x%p", sym->addr);
	*sep = ' '; // restore file content
	line = sep + 1;

	// extract symbol len (if any)
	if (*line == '\0') {
		sym->len = 0;
	} else if (strlen(line) > 2*sizeof(sym->len)) {
		error("address is too large");
		return false; // nothing to restore
	} else {
		sym->len = atoh(line);
	}
	dbg("sym->len = 0x%x", sym->len);

	return true;

fail:
	*sep = ' ';
	return false;
}

// ----------------------------------------------------------------------------

/*
 * Parses the memory mapped symbol map file from @symbol_map_start to
 * @symbol_map_end and fills the @sm's symbols array. The @sm's nb_syms field
 * must already be set.
 *
 * WARNING: the file's content is updated (then restored) during the parsing
 * (null bytes are inserted), so we should setup some sort of 'file locking'
 * while walking this file. At the time of writing their shouldn't be any
 * concurrent reader as the file is read during system startup.
 *
 * Returns true on success, false otherwise.
 */

static bool parse_symbol_map(char *symbol_map_start, char *symbol_map_end,
							 struct symbol_map *sm)
{
	char *ptr = symbol_map_start;
	char *eol = NULL;
	size_t sym_index = 0;
	bool ret = false;

	if ((symbol_map_start == symbol_map_end) || (sm->nb_syms == 0)) {
		error("invalid argument");
		return false;
	}

	do {
		struct symbol *sym = &sm->symbols[sym_index];

		if ((eol = strchr(ptr, '\n')) == NULL) {
			// this is the last line
			if ((uint32_t)(symbol_map_end - ptr) == 0) {
				// empty line
				break;
			}
		}

		*eol = '\0'; // update file content
		ret = parse_line(ptr, sym);
		*eol = '\n'; // restore the original character

		if (ret == false) {
			error("parsing line failed");
			return false;
		}

		ptr = eol + 1;
		sym_index++;
	} while (ptr < symbol_map_end);

	if (sym_index != sm->nb_syms) {
		error("oops! something went wrong!");
		return false;
	}

	return true;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Initialize the symbol list from a memory mapped file at @symbol_map_addr of
 * @symbol_map_len bytes.
 *
 * Returns true on success, false otherwise.
 */

bool symbol_init(char* symbol_map_start, size_t symbol_map_len)
{
	char *symbol_map_end = symbol_map_start + symbol_map_len;
	char *ptr = NULL;
	char *eol = NULL;

	info("initializing symbol list");

	if (symbol_map_start == NULL || symbol_map_len == 0) {
		error("invalid argument");
		return false;
	}

	// first walking to count the number of entries for kmalloc
	sym_map.nb_syms = 0;
	ptr = symbol_map_start;
	do {
		if ((eol = strchr(ptr, '\n')) == NULL) {
			if ((uint32_t)(symbol_map_end - ptr) > 0) {
				error("missing last line feed");
				goto fail;
			}
		}
		sym_map.nb_syms++;
		ptr = eol + 1;
	} while (ptr < symbol_map_end);
	dbg("sym_map.nb_syms = %u", sym_map.nb_syms);

	// allocates everything with a single call to kmalloc()
	sym_map.symbols =
		(struct symbol*) kmalloc(sym_map.nb_syms * sizeof(sym_map.symbols[0]));
	if (sym_map.symbols == NULL) {
		warn("not enough memory for a single alloc");
		// TODO: fallback to a kmalloc() for each symbol (or a bucketed alloc)
		NOT_IMPLEMENTED();
	}
	dbg("sym_map.symbols = 0x%p", sym_map.symbols);

	// finally parse the memory mapped file
	if (parse_symbol_map(symbol_map_start, symbol_map_end,
						 &sym_map) == false)
	{
		error("failed to parse symbol memory mapped file");
		kfree(sym_map.symbols);
		goto fail;
	}

	success("symbol list initialized (%u symbols)", sym_map.nb_syms);
	return true;

fail:
	sym_map.nb_syms = 0;
	return false;
}

// ----------------------------------------------------------------------------

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
