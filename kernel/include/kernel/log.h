/*
 * log.h
 *
 * Helpers to print message with various priorities.
 */

#ifndef KERNEL_LOG_H_
#define KERNEL_LOG_H_

#include <stdio.h>

#include <drivers/tty.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

enum log_level
{
	LOG_ERROR = 0,
	LOG_WARN = 1,
	LOG_INFO = 2,
	LOG_DEBUG = 3,
	LOG_MAX_LEVEL = LOG_DEBUG + 1,
};

// ----------------------------------------------------------------------------

#if 0 // XXX: use this version if you want something more verbose
#define log_macro_def(level, prefixe, fmt, ...)\
  do {\
    if (g_log_level >= level) \
      printf("[%s] %s: "prefixe fmt"\n", LOG_MODULE, __FUNCTION__, ##__VA_ARGS__); \
  } while (0)
#else
#define log_macro_def(level, prefixe, fmt, ...)\
  do {\
    if (g_log_level >= level) \
      printf("[%s] "prefixe fmt"\n", LOG_MODULE, ##__VA_ARGS__); \
  } while (0)
#endif

// ----------------------------------------------------------------------------

#define dbg(fmt, ...) \
{\
	terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));\
	log_macro_def(LOG_DEBUG, "DBG: ", fmt, ##__VA_ARGS__); \
	terminal_reset_color(); \
}

#define info(fmt, ...) \
{\
	terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));\
	log_macro_def(LOG_INFO, "", fmt, ##__VA_ARGS__); \
	terminal_reset_color(); \
}

#define success(fmt, ...) \
{\
	terminal_setcolor(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));\
	log_macro_def(LOG_INFO, "", fmt, ##__VA_ARGS__); \
	terminal_reset_color(); \
}

#define warn(fmt, ...) \
{\
	terminal_setcolor(vga_entry_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK));\
	log_macro_def(LOG_WARN, "WARN: ", fmt, ##__VA_ARGS__); \
	terminal_reset_color(); \
}

#define error(fmt, ...) \
{\
	terminal_setcolor(vga_entry_color(VGA_COLOR_RED, VGA_COLOR_BLACK));\
	log_macro_def(LOG_ERROR, "ERROR: ", fmt, ##__VA_ARGS__); \
	terminal_reset_color(); \
}

// ----------------------------------------------------------------------------

extern void log_set_level(enum log_level level);
extern enum log_level log_get_level(void);

// ----------------------------------------------------------------------------

extern enum log_level g_log_level; // don't use it directly

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_LOG_H_ */
