/*
 * ps2ctrl.c
 *
 * Implementation for 8042 PS/2 Controller.
 *
 * The 8042 controller is multi-purpose controller responsible to manage:
 * - communication with PS/2 keyboard
 * - communication with PS/2 mouse
 * - A20 gate handling
 * - system reset
 *
 * Documentation:
 * - http://www.diakom.ru/el/elfirms/datashts/Smsc/42w11.pdf (datasheet)
 * - https://wiki.osdev.org/%228042%22_PS/2_Controller
 * - https://wiki.osdev.org/PS/2_Keyboard
 */

#include <stdio.h>

#include <kernel/ps2ctrl.h>
#include <kernel/types.h>

#include "io.h"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

int ps2ctrl_init(void)
{
	printf("[ps2ctrl] starting initialization...\n");

	// FIXME

	printf("[ps2ctrl] initialization complete\n");

	return 0;
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
