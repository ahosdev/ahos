/*
 * ps2ctrl.h
 *
 * Header for 8042 PS/2 Controller.
 */

#ifndef KERNEL_PS2CTRL_H_
#define KERNEL_PS2CTRL_H_

#include <kernel/types.h>
#include <kernel/ps2driver.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

int  ps2ctrl_init(void);
void ps2ctrl_irq1_handler(void); // first port
void ps2ctrl_irq12_handler(void); // second port
bool ps2ctrl_cpu_reset(void);

bool ps2ctrl_send(uint8_t port, uint8_t cmd);
bool ps2ctrl_recv(uint8_t port, uint8_t *result);

bool ps2ctrl_identify_devices(void);
bool ps2ctrl_register_driver(struct ps2driver *driver);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_PS2CTRL_H_ */
