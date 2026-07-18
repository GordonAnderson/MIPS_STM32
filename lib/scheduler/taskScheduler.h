#ifndef TASKSCHEDULER_H_
#define TASKSCHEDULER_H_

/**
 * @file taskScheduler.h
 * @brief Cooperative millisecond task scheduler for MIPS Rev 6.0.
 *
 * Replaces the Arduino ArduinoThread / ThreadController pair used by the
 * Rev 5.4 firmware. Written fresh rather than ported, because MIPS's task
 * management commands are a genuinely useful feature and belong WITH the
 * scheduler rather than scattered through Serial.cpp as free functions.
 *
 * Design
 * ------
 *  - **Millisecond granularity only.** Real-time critical work belongs in
 *    hardware timers (STM32PulseTimer, TIM8 burst), not here.
 *  - **Cooperative.** run() is called from the main loop; each due task runs
 *    to completion. There is no preemption, therefore no priorities.
 *  - **Drift tolerant.** A task that misses its deadline is run once and
 *    rescheduled from *now*, never "caught up". This avoids a spiral of death
 *    if the loop stalls.
 *  - **The scheduler owns SUSPEND.** It is a first-class scheduler feature, not
 *    an external main-loop flag.
 *  - **Owns its command table.** This is the first example of a module
 *    registering its own GAACE CommandList — the seam that decomposes the
 *    Serial.cpp monolith.
 *
 * Per-task diagnostics
 * --------------------
 * Each task records last runtime, max runtime, run count and an overrun flag
 * (set when a task's runtime exceeds its own interval). Overrun is the single
 * most useful health signal in a cooperative system: a task running longer
 * than its period is starving the loop.
 *
 * Runtime is measured in **microseconds** via the DWT cycle counter. Intervals
 * are milliseconds; most task bodies run well under 1 ms, so millisecond
 * runtime resolution would report zero for nearly everything.
 *
 * Deliberately out of scope
 * -------------------------
 *  - Priorities (cooperative scheduling has none without preemption)
 *  - Dynamic task deletion (tasks live for the program; enable/disable suffices)
 *  - Sub-millisecond intervals
 *
 * @warning SUSPEND also gates table-mode real-time control in MIPS. The
 *          pulse/table engine is NOT a cooperative task, so it must query
 *          isSuspended() itself — see the note on suspendAll().
 *
 * Usage
 * -----
 * @code
 *   commandProcessor cp;
 *   taskScheduler    sched(&cp);
 *   cp.registerCommands(sched.schedulerCommands());
 *
 *   sched.addTask("DIO", DIO_loop, 100);
 *
 *   while (1) { cp.processStreams(); cp.processCommands(); sched.run(); }
 * @endcode
 */

#include "gaace_compat.h"
#include "commandProcessor.h"

/** Maximum number of registered tasks. MIPS Rev 5.4 used ~20. */
#define MAX_TASKS      32

/** Interval limits accepted by setInterval() / TASKINT, in milliseconds. */
#define TASK_MIN_INTERVAL   1
#define TASK_MAX_INTERVAL   10000

/** Stagger applied between tasks by restart(), in milliseconds. */
#define TASK_RESTART_STAGGER  25

/** Returned by addTask() on failure. */
#define TASK_INVALID_ID  (-1)

/**
 * @brief One scheduled task.
 *
 * Populated by taskScheduler::addTask(); do not construct directly.
 */
typedef struct
{
    const char *name;         ///< Task name, used by all TASK* commands
    int         id;           ///< Assigned at add time (index into the table)
    uint32_t    interval;     ///< Run interval in milliseconds
    uint32_t    nextRun;      ///< Tick value at which this task is next due
    bool        enabled;      ///< False = skipped by run(), state retained
    void      (*callback)(void);  ///< Task body

    // Diagnostics
    uint32_t    lastRuntime;  ///< Duration of the most recent run, microseconds
    uint32_t    maxRuntime;   ///< Longest run since boot / clear, microseconds
    uint32_t    runCount;     ///< Number of times this task has run
    bool        overrun;      ///< Last run exceeded this task's own interval
} Task;

class taskScheduler
{
public:
    /**
     * @brief Construct the scheduler and bind it to a commandProcessor.
     *
     * Also initialises the DWT cycle counter used for runtime measurement.
     * Call schedulerCommands() and pass the result to
     * commandProcessor::registerCommands() to expose the TASK* commands.
     *
     * @param cmdP  Processor that will own the scheduler's commands. Must
     *              remain valid for the lifetime of this object.
     *
     * @warning Only one taskScheduler instance may exist at a time — the
     *          command callbacks reach it through a module-level static
     *          pointer, matching the pattern used by `debug`.
     */
    explicit taskScheduler(commandProcessor *cmdP);

    // -----------------------------------------------------------------------
    // Task registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register a task.
     *
     * Replaces the Rev 5.4 three-line dance:
     *   `thread.onRun(cb); thread.setInterval(100); control.add(&thread);`
     *
     * The first run is scheduled one interval from now. Tasks are added once at
     * boot and never removed; use setEnabled() to stop one.
     *
     * @param name      Task name. Must remain valid for the program's lifetime
     *                  (string literal or static buffer) — it is not copied.
     * @param callback  Task body, called when due.
     * @param intervalMs  Run interval, clamped to TASK_MIN/MAX_INTERVAL.
     * @return Assigned task ID, or TASK_INVALID_ID if the table is full or the
     *         arguments are invalid.
     */
    int  addTask(const char *name, void (*callback)(void), uint32_t intervalMs);

    // -----------------------------------------------------------------------
    // Execution
    // -----------------------------------------------------------------------

    /**
     * @brief Run all due tasks. Call from the main loop, as often as possible.
     *
     * Measures each task's runtime, updates diagnostics, and reschedules from
     * the moment the task finished (drift tolerant — see file header).
     * Returns immediately and runs nothing while suspended.
     *
     * @return Number of tasks that ran on this call.
     */
    int  run(void);

    // -----------------------------------------------------------------------
    // Task control
    // -----------------------------------------------------------------------

    bool setEnabled(const char *name, bool state);
    bool setInterval(const char *name, uint32_t intervalMs);

    /** @brief Make a task due immediately (next run() call executes it). */
    bool runNow(const char *name);

    /**
     * @brief Restart all task schedules, staggered by TASK_RESTART_STAGGER ms
     *        per task index.
     *
     * Staggering prevents every task coming due on the same tick, which would
     * produce a long blocking burst in the cooperative loop. Matches Rev 5.4
     * THRDRESTART behaviour.
     */
    void restart(void);

    // -----------------------------------------------------------------------
    // Suspend — owned by the scheduler
    // -----------------------------------------------------------------------

    /**
     * @brief Suspend or resume all cooperative task execution.
     *
     * @warning This does NOT stop the pulse/table engine. That runs from
     *          hardware timers, not from run(), so it must query isSuspended()
     *          itself and halt real-time control accordingly. In Rev 5.4 the
     *          global `Suspend` flag gated both; keep that behaviour.
     */
    void suspendAll(bool state);

    /** @brief True while suspended. Queried by the pulse/table engine. */
    bool isSuspended(void) const { return suspended; }

    // -----------------------------------------------------------------------
    // Introspection
    // -----------------------------------------------------------------------

    Task *findTask(const char *name);     ///< NULL if not found (case-insensitive)
    Task *taskByIndex(int index);         ///< NULL if out of range
    int   taskCount(void) const { return numTasks; }

    /** @brief Zero maxRuntime, runCount and overrun for every task. */
    void  clearStatistics(void);

    // -----------------------------------------------------------------------
    // Command table
    // -----------------------------------------------------------------------

    /**
     * @brief CommandList holding TASKS, TASK, TASKENA, TASKINT, RUNNOW,
     *        RESTART and SUSPEND.
     *
     * Pass to commandProcessor::registerCommands().
     */
    CommandList *schedulerCommands(void);

private:
    Task     tasks[MAX_TASKS];
    int      numTasks;
    bool     suspended;
};

#endif // TASKSCHEDULER_H_
