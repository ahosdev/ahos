/*
 * scheduler.h
 *
 * A "cheap fake scheduler" which delegate the "fairness" to the task
 * responsability (i.e. not fair at all). It only provide a stubs to run a
 * task given a certain number of ticks (i.e. QUANTUM) if the task itself
 * accept to give cpu back to the scheduler...
 */

#ifndef KERNEL_SCHEDULER_H_
#define KERNEL_SCHEDULER_H_

#include <kernel/types.h>

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

void sched_run_task(int32_t quantum, char *name, void (*task)(void));

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_SCHEDULER_H_ */
