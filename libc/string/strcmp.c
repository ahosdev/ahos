/*
 * strcmp.c
 *
 * LIBC implementation of strcmp().
 */

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
	NAME
		strcmp - compare two strings

	SYNOPSIS
		#include <string.h>

		int strcmp(const char *s1, const char *s2);

	DESCRIPTION
		The  strcmp() function compares the two strings s1 and s2.  It returns
		an integer less than, equal to, or greater than zero if s1 is found,
		respectively, to be less than, to match, or be greater than s2.

RETURN VALUE
		The strcmp() functions return an integer less than, equal to, or greater
		than zero if s1 is found, respectively, to be  less  than,  to  match, or
		be greater than s2.
*/

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}

	if (*s1 < *s2) {
		return -1;
	} else if (*s1 > *s2) {
		return 1;
	} else {
		return 0;
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
