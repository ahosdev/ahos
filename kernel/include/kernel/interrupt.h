/*
 * interrupt.h
 *
 * Interruption management and handlers.
 */

#ifndef KERNEL_INTERRUPT_H_
#define KERNEL_INTERRUPT_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#define IRQ0_CLOCK 		0 // Programmable Interrupt Timer (PIT)
#define IRQ1_KEYBOARD 		1
#define IRQ2_SLAVE_INT		2
#define IRQ3_COM2 		3
#define IRQ4_COM1 		4
#define IRQ5_LPT2 		5
#define IRQ6_FLOPPY 		6
#define IRQ7_LPT1 		7 // spurious ?

#define IRQ8_CMOS_RTC 		8
#define IRQ9_FREE 		9 // Free for peripherals / legacy SCSI / NIC
#define IRQ10_FREE 		10 // Free for peripherals / legacy SCSI / NIC
#define IRQ11_FREE 		11 // Free for peripherals / legacy SCSI / NIC
#define IRQ12_PS2_MOUSE 	12
#define IRQ13_FPU_COPROC 	13
#define IRQ14_PRIMARY_ATA 	14
#define IRQ15_SECONDARY_ATA 	15

#define IRQ_MAX_VALUE 15

// ----------------------------------------------------------------------------

#define IRQ0_INT 0x20 // irq0 interrupts is mapped to interrupt 32 (0x20)
#define IRQ7_INT 0x28 // irq8 interrupts is mapped to interrupt 40 (0x28)

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void setup_idt();

void enable_interrupts(void);
void disable_interrupts(void);

// NMI: Non Maskable Interrupts
void enable_nmi(void);
void disable_nmi(void);

void irq_init(uint8_t master_offset, uint8_t slave_offset);
void irq_set_mask(uint8_t irq);
void irq_clear_mask(uint8_t irq);
void irq_send_eoi(uint8_t irq);

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* KERNEL_INTERRUPT_H_ */
