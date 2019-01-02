#ifndef KERNEL_TYPES_H_
#define KERNEL_TYPES_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <kernel/list.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// exported by linker
extern uint32_t kernel_start;
extern uint32_t kernel_end;
extern uint32_t kernel_code_start;
extern uint32_t kernel_code_end;
extern uint32_t kernel_rodata_start;
extern uint32_t kernel_rodata_end;
extern uint32_t kernel_data_start;
extern uint32_t kernel_data_end;
extern uint32_t kernel_bss_start;
extern uint32_t kernel_bss_end;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#include <kernel/log.h>
#include <stdlib.h>

// we declare it here since it is imported almost everywhere
#define NOT_IMPLEMENTED()\
{\
	error("NOT IMPLEMENTED [%s:%d]", __FILE__, __LINE__); \
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
