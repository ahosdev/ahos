/*
 * atoh.h
 *
 * LIBC implementation of atoh().
 */

#include <stdlib.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Converts an hexadecimal strings from @nptr. It is *expected* that @nptr is
 * properly formatted.
 */

size_t atoh(const char *nptr)
{
	size_t res = 0;

	while (*nptr) {
		res = res << 4;
		if (*nptr >= '0' && *nptr <= '9') {
			res += *nptr - '0';
		} else if (*nptr >= 'a' && *nptr <= 'f') {
			res += *nptr - 'a' + 10;
		} else if (*nptr >= 'A' && *nptr <= 'F') {
			res += *nptr - 'A' + 10;
		} else {
			// ERROR: badly formatted nptr
			abort();
		}
		nptr++;
	}

	return res;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
