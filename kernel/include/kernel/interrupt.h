/*
 * interrupt.h
 *
 * Interruption management and handlers.
 */

#ifndef KERNEL_INTERRUPT_H_
#define KERNEL_INTERRUPT_H_

void enable_irq(void);
void disable_irq(void);

// NMI: Non Maskable Interrupts
void enable_nmi(void);
void disable_nmi(void);

#endif /* KERNEL_INTERRUPT_H_ */
