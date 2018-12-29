#ifndef KERNEL_ATOMIC_H_
#define KERNEL_ATOMIC_H_

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#if defined(__i386__)
	#include "../arch/i386/atomic.h"
#else
	#error "Only ix86 architecture for now"
#endif

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif
