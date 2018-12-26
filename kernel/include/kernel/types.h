#ifndef KERNEL_TYPES_H_
#define KERNEL_TYPES_H_

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// ----------------------------------------------------------------------------

#include <kernel/log.h>
#include <stdlib.h>

// we declare it here since it is imported almost everywhere
#define NOT_IMPLEMENTED()\
{\
	error("NOT IMPLEMENTED!"); \
	abort(); \
}

#define UNTESTED_CODE()\
{\
	warn("UNTESTED CODE!");\
	warn("UNTESTED CODE!");\
	warn("UNTESTED CODE!");\
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* KERNEL_TYPES_H_ */
