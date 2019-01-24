/*
 * panic.c
 *
 * i386 Architecture-specific panic handler.
 */

#include <kernel/types.h>
#include <kernel/symbol.h>
#include <kernel/interrupt.h>

#include <stdio.h>

#include "registers.h"

#define LOG_MODULE "panic"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Panic handler entry point.
 */

__attribute__((__noreturn__))
void panic(char *msg, ...)
{
	char error_buf[256];
	va_list args;

	// disable interrupts as soon as possible
	disable_interrupts();

	/*
	 * The stack layout is (low addresses first):
	 * - second arg
	 * - first arg
	 * - return address of calling function
	 * - EBP of calling function
	 */

	reg_t *ebp = (reg_t*)&msg - 2; // use pointer arithmetic

	printf("\n=============\n");
	printf("=== PANIC ===\n");
	printf("=============\n\n");

	va_start(args, msg);
	// FIXME: use vsnprintf() to avoid buffer overflow
	vsprintf(error_buf, msg, args);
	va_end(args);
	error_buf[sizeof(error_buf) - 1] = '\0';
	printf("error: %s\n\n", error_buf);

	// dump stack trace
	printf("Call trace:\n");
	for (;;) {
		struct symbol sym;
		reg_t eip = ebp[1];

		if ((eip.val == 0) || (ebp[0].val == 0)) {
			// no more caller
			break;
		}

		if (symbol_find((void*)eip.val, &sym)) {
			printf("- (ebp=0x%.8x) %s() + 0x%x/0x%x\n", ebp[0].val, sym.name,
				(eip.val - (uint32_t)sym.addr), sym.len);
		} else {
			printf("- (ebp=0x%.8x) ????? / 0x%x\n", ebp[0].val, eip.val);
		}

		ebp = (reg_t*) ebp[0].val;
	}

	// infinite loop
	for (;;) { }
	__builtin_unreachable();
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
