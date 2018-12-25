/*
 * vga.c
 *
 * Documentation:
 * - https://wiki.osdev.org/Text_Mode_Cursor
 */

#include <kernel/types.h>
#include <kernel/vga.h>

#include "io.h"

#undef LOG_MODULE
#define LOG_MODULE "vga"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void vga_enable_cursor(enum vga_cursor_style style)
{
	const uint8_t cursor_start = 0;
	const uint8_t cursor_end = (style == VGA_CURSOR_BOX) ? 15 : 1;

	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

// ----------------------------------------------------------------------------

void vga_disable_cursor(void)
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}

// ----------------------------------------------------------------------------

void vga_update_cursor(int x, int y)
{
	uint16_t pos = y * VGA_WIDTH + x;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
