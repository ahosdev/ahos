/*
 * paging.c
 *
 * Paging Memory management with a single level.
 *
 * Documentation:
 * - Intel (chapter 3)
 * - https://wiki.osdev.org/Paging
 * - https://wiki.osdev.org/Setting_Up_Paging
 */

#include <mem/memory.h>

#include <kernel/log.h>

#define LOG_MODULE "paging"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void paging_setup(void)
{
	info("paging setup...");

	// TODO

	success("paging setup succeed");
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
