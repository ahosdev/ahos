/*
 * strcpy.c
 *
 * LIBC implementation of strcpy().
 */

#include <string.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Of course, this is unsafe as hell (@dest must be big enough and @src must be
 * NULL terminated).
 */

char *strcpy(char *dest, const char *src)
{
	char *ptr = dest;

	while (*src) {
		*ptr++ = *src++;
	}
	*ptr = '\0'; // dest include the terminating NULL byte

	return dest;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
