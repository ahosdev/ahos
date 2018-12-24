#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

#include "vga.h"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint8_t terminal_default_color;
static uint16_t* terminal_buffer;

static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;
static const size_t VGA_ELT_SIZE = sizeof(terminal_buffer[0]);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

static void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}

// ----------------------------------------------------------------------------

static void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

// ----------------------------------------------------------------------------

/*
 * Moves the terminal buffer up by one line. The top line is lost.
 */

static void scroll_up(void)
{
	terminal_column = 0;
	if (++terminal_row == VGA_HEIGHT) {
		memmove(terminal_buffer, &terminal_buffer[VGA_WIDTH + 0],
			VGA_ELT_SIZE * (VGA_WIDTH * (VGA_HEIGHT - 1)));
		for (size_t x = 0; x < VGA_WIDTH; ++x) {
			const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_default_color);
		}
		terminal_row = VGA_HEIGHT - 1;
	}
}

// ----------------------------------------------------------------------------

/*
 * This function has been moved out of terminal_putchar() in order to only update
 * the cursor once writing is complete instead of every char.
 */

static void __terminal_putchar(char c)
{
	unsigned char uc = c;

	if (c == '\n') {
		scroll_up();
	} else if (c == '\r') {
		terminal_column = 0;
	} else if (c == '\t') {
		__terminal_putchar(' ');
		__terminal_putchar(' ');
		__terminal_putchar(' ');
	} else {
		terminal_putentryat(uc, terminal_color, terminal_column,
							terminal_row);
		if (++terminal_column == VGA_WIDTH) {
			scroll_up();
		}
	}
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void terminal_initialize(void)
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_default_color =
		vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	terminal_setcolor(terminal_default_color);
	terminal_buffer = VGA_MEMORY;

	// clear the screen
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}

	vga_enable_cursor(VGA_CURSOR_BOX);
	vga_update_cursor(terminal_column, terminal_row);
}

// ----------------------------------------------------------------------------

void terminal_putchar(char c)
{
	__terminal_putchar(c);
	vga_update_cursor(terminal_column, terminal_row);
}

// ----------------------------------------------------------------------------

void terminal_write(const char* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		__terminal_putchar(data[i]);
	vga_update_cursor(terminal_column, terminal_row);
}

// ----------------------------------------------------------------------------

void terminal_writestring(const char* data)
{
	terminal_write(data, strlen(data));
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
