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
#include <kernel/clock.h>

#undef LOG_MODULE
#define LOG_MODULE "sched"

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

/*
 * Wrapper to invoke @task in a loop until @quantum ticks has been consumed.
 *
 * All parameters are expected to be non-NULL. In addition, @task is supposed
 * to act as a "good citizen" and give the scheduler's opportunity to check if
 * if the task must yield out.
 */

static inline void run_task(int32_t quantum, char *name, void (*task)(void))
{
	const int32_t end_tick = clock_gettick() + quantum;

	info("running task <%s>", name);

	while (clock_gettick() < end_tick) {
		(*task)();
	}

	info("stopping task <%s>", name);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================

#endif /* !KERNEL_SCHEDULER_H_ */
