/*
 * ps2ctrl.h
 *
 * Header for 8042 PS/2 Controller.
 */

#ifndef KERNEL_PS2CTRL_H_
#define KERNEL_PS2CTRL_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

int ps2ctrl_init(void);
bool ps2ctrl_identify_devices(void);
void ps2ctrl_irq1_handler(void); // first port
void ps2ctrl_irq12_handler(void); // second port
bool ps2ctrl_cpu_reset(void);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_PS2CTRL_H_ */
