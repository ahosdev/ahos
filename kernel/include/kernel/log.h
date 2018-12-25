/*
 * log.h
 *
 * Helpers to print message with various priorities.
 */

#ifndef KERNEL_LOG_H_
#define KERNEL_LOG_H_

#include <stdio.h>

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

#define log_macro_def(level, prefixe, fmt, ...)\
  do {\
    if (g_log_level >= level) \
      printf("%s: "prefixe fmt"\n", __FUNCTION__, ##__VA_ARGS__); \
  } while (0)

// ----------------------------------------------------------------------------

#define dbg(fmt, ...)   log_macro_def(LOG_DEBUG, "DBG: ", fmt, ##__VA_ARGS__)
#define info(fmt, ...)    log_macro_def(LOG_INFO, "", fmt, ##__VA_ARGS__)
#define success(fmt, ...) log_macro_def(LOG_INFO, "", fmt, ##__VA_ARGS__)
#define warn(fmt, ...)    log_macro_def(LOG_WARN, "WARN: ", fmt, ##__VA_ARGS__)
#define error(fmt, ...)   log_macro_def(LOG_ERROR, "ERROR: ", fmt, ##__VA_ARGS__)

// ----------------------------------------------------------------------------

extern void log_set_level(enum log_level level);
extern enum log_level log_get_level(void);

// ----------------------------------------------------------------------------

extern enum log_level g_log_level; // don't use it directly

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_LOG_H_ */
