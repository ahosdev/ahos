/*
 * strncpy.c
 *
 * LIBC implementation of strncpy().
 */

#include <string.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Just like strcpy(), this function is also unsafe if @n is too big for @dest
 * (out-of-bound write). If @src is smaller than n, then @dest is filled with
 * null bytes.
 *
 * WARNING: if strlen(src) == n, @dest won't be null-terminated!
 */

char *strncpy(char *dest, const char *src, size_t n)
{
	size_t i = 0;

	for (i = 0; (i < n) && (src[i] != '\0'); ++i) {
		dest[i] = src[i];
	}

	for (; i < n; i++) {
		dest[i] = '\0';
	}

	return dest;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
