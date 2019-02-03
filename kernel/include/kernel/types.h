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
extern uint32_t kernel_start_ldsym;
extern uint32_t kernel_end_ldsym;
extern uint32_t kernel_code_start_ldsym;
extern uint32_t kernel_code_end_ldsym;
extern uint32_t kernel_rodata_start_ldsym;
extern uint32_t kernel_rodata_end_ldsym;
extern uint32_t kernel_data_start_ldsym;
extern uint32_t kernel_data_end_ldsym;
extern uint32_t kernel_bss_start_ldsym;
extern uint32_t kernel_bss_end_ldsym;

#define kernel_start ((uint32_t)&kernel_start_ldsym)
#define kernel_end ((uint32_t)&kernel_end_ldsym)
#define kernel_code_start ((uint32_t)&kernel_code_start_ldsym)
#define kernel_code_end ((uint32_t)&kernel_code_end_ldsym)
#define kernel_rodata_start ((uint32_t)&kernel_rodata_start_ldsym)
#define kernel_rodata_end ((uint32_t)&kernel_rodata_end_ldsym)
#define kernel_data_start ((uint32_t)&kernel_data_start_ldsym)
#define kernel_data_end ((uint32_t)&kernel_data_end_ldsym)
#define kernel_bss_start ((uint32_t)&kernel_bss_start_ldsym)
#define kernel_bss_end ((uint32_t)&kernel_bss_end_ldsym)

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#include <kernel/log.h>

// we declare it here since it is imported almost everywhere
#define NOT_IMPLEMENTED()\
{\
	panic("NOT IMPLEMENTED [%s:%d]", __FILE__, __LINE__); \
}

#define UNTESTED_CODE()\
{\
	warn("UNTESTED CODE!");\
	warn("UNTESTED CODE!");\
	warn("UNTESTED CODE!");\
}

// ----------------------------------------------------------------------------

/*
 * Arch-specific handlers.
 */

extern void panic(char *msg, ...);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* KERNEL_TYPES_H_ */
