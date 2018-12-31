/*
 * scheduler.c
 *
 * Basic Cooperative Scheduler.
 */

#include <kernel/scheduler.h>

#include <drivers/clock.h>

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

void sched_run_task(int32_t quantum, char *name, void (*task)(void))
{
	const int32_t end_tick = clock_gettick() + quantum;

	dbg("running task <%s>", name);

	while (clock_gettick() < end_tick) {
		(*task)();
	}

	dbg("stopping task <%s>", name);
}

// ============================================================================
// ----------------------------------------------------------------------------
// ============================================================================
