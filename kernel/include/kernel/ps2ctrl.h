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

struct ps2_device {
	char name[64];
	bool (*init)(void *param);
	void (*release)(void);
	void (*recv)(uint8_t data);
	void (*enable)(void);
	void (*disable)(void);
};

// ----------------------------------------------------------------------------

int ps2ctrl_init(void);
bool ps2ctrl_identify_devices(void);
void ps2ctrl_irq1_handler(void); // first port
void ps2ctrl_irq12_handler(void); // second port
bool ps2ctrl_cpu_reset(void);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_PS2CTRL_H_ */
