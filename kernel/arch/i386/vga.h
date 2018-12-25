/*
 * vga.h
 */

#ifndef ARCH_I386_VGA_H
#define ARCH_I386_VGA_H

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum vga_color {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

enum vga_cursor_style {
	VGA_CURSOR_UNDERSCORE = 0,
	VGA_CURSOR_BOX = 1,
};

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}

// ----------------------------------------------------------------------------

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
	return (uint16_t) uc | (uint16_t) color << 8;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void vga_enable_cursor(enum vga_cursor_style style);
void vga_disable_cursor(void);
void vga_update_cursor(int x, int y);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif
