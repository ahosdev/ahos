#ifndef _KERNEL_TERMINAL_H
#define _KERNEL_TERMINAL_H

#include <kernel/types.h>

#include <drivers/vga.h> // we include it here to ease the color function usage

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);

void terminal_setcolor(uint8_t color);
void terminal_reset_color(void);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif
