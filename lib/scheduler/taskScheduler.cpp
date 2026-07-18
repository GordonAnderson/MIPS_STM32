#include "taskScheduler.h"
#include <string.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #define SCHED_TICK_MS()  millis()
#else
  #include "stm32h7xx_hal.h"
  #define SCHED_TICK_MS()  HAL_GetTick()
#endif

// =============================================================================
//  Module-level state
//
//  Command callbacks are plain C functions stored as CMDfunction pointers, so
//  they reach the processor and the scheduler through file-scope statics.
//  Matches the pattern used by GAACE_Core's `debug` module.
//
//  LIMITATION: only one taskScheduler instance may exist at a time.
// =============================================================================

static commandProcessor *cp    = NULL;
static taskScheduler    *sched = NULL;

// =============================================================================
//  Microsecond timebase (DWT cycle counter)
//
//  HAL_GetTick() is millisecond resolution, which would report 0 for nearly
//  every task body. The DWT cycle counter gives sub-microsecond resolution at
//  no runtime cost beyond a register read.
// =============================================================================

static uint32_t cyclesPerUs = 1;

#if !defined(ARDUINO)
static void usClockInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(DWT_LAR_KEY) || defined(CoreDebug_DEMCR_TRCENA_Msk)
    // Some Cortex-M7 parts require unlocking the DWT before CYCCNT will run.
    DWT->LAR = 0xC5ACCE55;
#endif
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    cyclesPerUs = SystemCoreClock / 1000000UL;
    if (cyclesPerUs == 0) cyclesPerUs = 1;   // guard against a bad clock setup
}

/** @brief Free-running microsecond counter. Wraps every ~9.7 s at 440 MHz. */
static inline uint32_t usTicks(void)
{
    return DWT->CYCCNT / cyclesPerUs;
}
#else
static void usClockInit(void) { cyclesPerUs = 1; }
static inline uint32_t usTicks(void) { return micros(); }
#endif

// =============================================================================
//  Internal helpers
// =============================================================================

/**
 * @brief Wrap-safe "is this tick value due?" test.
 *
 * Comparing tick values with `>=` breaks when HAL_GetTick() wraps (every ~49
 * days). Subtracting and testing the sign of the signed difference is correct
 * across the wrap, provided the interval is far below 2^31 ms — which
 * TASK_MAX_INTERVAL guarantees.
 */
static inline bool timeReached(uint32_t now, uint32_t target)
{
    return ((int32_t)(now - target) >= 0);
}

/** @brief Case-insensitive name match. */
static bool nameMatches(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    return (strcasecmp(a, b) == 0);
}

// =============================================================================
//  taskScheduler implementation
// =============================================================================

taskScheduler::taskScheduler(commandProcessor *cmdP)
{
    cp        = cmdP;
    sched     = this;
    numTasks  = 0;
    suspended = false;

    memset(tasks, 0, sizeof(tasks));
    usClockInit();
}

int taskScheduler::addTask(const char *name, void (*callback)(void), uint32_t intervalMs)
{
    if (name == NULL || callback == NULL)   return TASK_INVALID_ID;
    if (numTasks >= MAX_TASKS)              return TASK_INVALID_ID;
    if (findTask(name) != NULL)             return TASK_INVALID_ID;  // duplicate name

    if (intervalMs < TASK_MIN_INTERVAL) intervalMs = TASK_MIN_INTERVAL;
    if (intervalMs > TASK_MAX_INTERVAL) intervalMs = TASK_MAX_INTERVAL;

    Task *t = &tasks[numTasks];

    t->name        = name;
    t->id          = numTasks;
    t->interval    = intervalMs;
    t->nextRun     = SCHED_TICK_MS() + intervalMs;
    t->enabled     = true;
    t->callback    = callback;
    t->lastRuntime = 0;
    t->maxRuntime  = 0;
    t->runCount    = 0;
    t->overrun     = false;

    return numTasks++;
}

int taskScheduler::run(void)
{
    if (suspended) return 0;

    int ran = 0;
    uint32_t now = SCHED_TICK_MS();

    for (int i = 0; i < numTasks; i++)
    {
        Task *t = &tasks[i];

        if (!t->enabled || t->callback == NULL) continue;
        if (!timeReached(now, t->nextRun))      continue;

        uint32_t start = usTicks();
        t->callback();
        uint32_t elapsed = usTicks() - start;   // wrap-safe: unsigned subtraction

        t->lastRuntime = elapsed;
        if (elapsed > t->maxRuntime) t->maxRuntime = elapsed;
        t->runCount++;

        // Overrun: this run took longer than the task's own period.
        t->overrun = (elapsed > (t->interval * 1000UL));

        // Reschedule from NOW, not from the previous deadline. Drift tolerant:
        // a stalled loop never produces a burst of catch-up runs.
        t->nextRun = SCHED_TICK_MS() + t->interval;

        ran++;
    }
    return ran;
}

Task *taskScheduler::findTask(const char *name)
{
    for (int i = 0; i < numTasks; i++)
        if (nameMatches(tasks[i].name, name)) return &tasks[i];
    return NULL;
}

Task *taskScheduler::taskByIndex(int index)
{
    if (index < 0 || index >= numTasks) return NULL;
    return &tasks[index];
}

bool taskScheduler::setEnabled(const char *name, bool state)
{
    Task *t = findTask(name);
    if (t == NULL) return false;

    // Re-arm on enable so a long-disabled task does not fire immediately
    // (its nextRun would be far in the past).
    if (state && !t->enabled) t->nextRun = SCHED_TICK_MS() + t->interval;

    t->enabled = state;
    return true;
}

bool taskScheduler::setInterval(const char *name, uint32_t intervalMs)
{
    if (intervalMs < TASK_MIN_INTERVAL || intervalMs > TASK_MAX_INTERVAL) return false;

    Task *t = findTask(name);
    if (t == NULL) return false;

    t->interval = intervalMs;
    t->nextRun  = SCHED_TICK_MS() + intervalMs;   // apply from now
    return true;
}

bool taskScheduler::runNow(const char *name)
{
    Task *t = findTask(name);
    if (t == NULL) return false;

    t->nextRun = SCHED_TICK_MS();   // due immediately
    return true;
}

void taskScheduler::restart(void)
{
    uint32_t now = SCHED_TICK_MS();
    for (int i = 0; i < numTasks; i++)
        tasks[i].nextRun = now + (uint32_t)(TASK_RESTART_STAGGER * i);
}

void taskScheduler::suspendAll(bool state)
{
    suspended = state;

    // On resume, re-arm every task. Without this, every task would be overdue
    // by the entire suspend duration and all would fire on the same run() call.
    if (!state) restart();
}

void taskScheduler::clearStatistics(void)
{
    for (int i = 0; i < numTasks; i++)
    {
        tasks[i].maxRuntime = 0;
        tasks[i].runCount   = 0;
        tasks[i].overrun    = false;
    }
}

// =============================================================================
//  Command callbacks
// =============================================================================

/** @brief TASKS — list all tasks with their state and diagnostics. */
static void listTasks(void)
{
    if (sched == NULL || cp == NULL) return;

    cp->sendACK();
    cp->println("ID  Name        Int(ms)  Ena  Last(us)  Max(us)   Count     Ovr");

    for (int i = 0; i < sched->taskCount(); i++)
    {
        Task *t = sched->taskByIndex(i);
        if (t == NULL) continue;

        cp->print(t->id);            cp->print("   ");
        cp->print(t->name);          cp->print("        ");
        cp->print(t->interval);      cp->print("     ");
        cp->print(t->enabled);       cp->print("  ");
        cp->print(t->lastRuntime);   cp->print("      ");
        cp->print(t->maxRuntime);    cp->print("     ");
        cp->print(t->runCount);      cp->print("    ");
        cp->println(t->overrun);
    }
}

/** @brief TASK,<name> — detailed report for one task. */
static void reportTask(void)
{
    char *name;

    if (sched == NULL || cp == NULL) return;
    if (cp->getNumArgs() != 1)            { cp->sendNAK(); return; }
    if (!cp->getValue(&name))             { cp->sendNAK(); return; }

    Task *t = sched->findTask(name);
    if (t == NULL)                        { cp->sendNAK(); return; }

    cp->sendACK();
    cp->print("Name:         "); cp->println(t->name);
    cp->print("ID:           "); cp->println(t->id);
    cp->print("Interval(ms): "); cp->println(t->interval);
    cp->print("Enabled:      "); cp->println(t->enabled);
    cp->print("Last run(us): "); cp->println(t->lastRuntime);
    cp->print("Max run(us):  "); cp->println(t->maxRuntime);
    cp->print("Run count:    "); cp->println(t->runCount);
    cp->print("Overrun:      "); cp->println(t->overrun);
}

/** @brief TASKENA,<name>,<TRUE|FALSE> — enable or disable a task. */
static void enableTask(void)
{
    char *name;
    char *state;

    if (sched == NULL || cp == NULL) return;
    if (cp->getNumArgs() != 2)                          { cp->sendNAK(); return; }
    if (!cp->getValue(&name))                           { cp->sendNAK(); return; }
    if (!cp->getValue(&state, "TRUE,FALSE"))            { cp->sendNAK(); return; }

    bool on = (strcasecmp(state, "TRUE") == 0);

    if (sched->setEnabled(name, on)) cp->sendACK();
    else                             cp->sendNAK();
}

/** @brief TASKINT,<name>,<ms> — change a task's run interval. */
static void intervalTask(void)
{
    char *name;
    int   ms;

    if (sched == NULL || cp == NULL) return;
    if (cp->getNumArgs() != 2)                                      { cp->sendNAK(); return; }
    if (!cp->getValue(&name))                                       { cp->sendNAK(); return; }
    if (!cp->getValue(&ms, TASK_MIN_INTERVAL, TASK_MAX_INTERVAL))   { cp->sendNAK(); return; }

    if (sched->setInterval(name, (uint32_t)ms)) cp->sendACK();
    else                                        cp->sendNAK();
}

/** @brief RUNNOW,<name> — make a task due immediately. */
static void runTaskNow(void)
{
    char *name;

    if (sched == NULL || cp == NULL) return;
    if (cp->getNumArgs() != 1) { cp->sendNAK(); return; }
    if (!cp->getValue(&name))  { cp->sendNAK(); return; }

    if (sched->runNow(name)) cp->sendACK();
    else                     cp->sendNAK();
}

/** @brief RESTART — restart all task schedules, staggered. */
static void restartTasks(void)
{
    if (sched == NULL || cp == NULL) return;
    sched->restart();
    cp->sendACK();
}

/** @brief SUSPEND,<TRUE|FALSE> — suspend or resume all task execution. */
static void suspendTasks(void)
{
    char *state;

    if (sched == NULL || cp == NULL) return;
    if (cp->getNumArgs() != 1)                  { cp->sendNAK(); return; }
    if (!cp->getValue(&state, "TRUE,FALSE"))    { cp->sendNAK(); return; }

    sched->suspendAll(strcasecmp(state, "TRUE") == 0);
    cp->sendACK();
}

/** @brief TASKCLR — zero all task statistics. */
static void clearTaskStats(void)
{
    if (sched == NULL || cp == NULL) return;
    sched->clearStatistics();
    cp->sendACK();
}

// =============================================================================
//  Command table
// =============================================================================

static Command schedulerCmds[] =
{
    {"TASKS",   CMDfunction, -1, (void *)listTasks,     NULL, "List all tasks with state and runtime statistics"},
    {"TASK",    CMDfunction, -1, (void *)reportTask,    NULL, "Detailed report for one task: name"},
    {"TASKENA", CMDfunction, -1, (void *)enableTask,    NULL, "Enable/disable a task: name, TRUE|FALSE"},
    {"TASKINT", CMDfunction, -1, (void *)intervalTask,  NULL, "Set task interval in ms (1-10000): name, ms"},
    {"RUNNOW",  CMDfunction, -1, (void *)runTaskNow,    NULL, "Run a task immediately: name"},
    {"RESTART", CMDfunction, -1, (void *)restartTasks,  NULL, "Restart all task schedules, staggered 25ms per task"},
    {"SUSPEND", CMDfunction, -1, (void *)suspendTasks,  NULL, "Suspend/resume all task execution: TRUE|FALSE"},
    {"TASKCLR", CMDfunction, -1, (void *)clearTaskStats,NULL, "Clear max runtime, run count and overrun flags"},

    // -------------------------------------------------------------------------
    // Rev 5.4 host-compatibility aliases.
    //
    // MIPS host software at 47+ institutions uses the original spellings.
    // These map the old names onto the same handlers so existing scripts keep
    // working. DELETE THIS BLOCK if host compatibility is not required.
    //
    // Note THRDRESTART/SSPND map to RESTART/SUSPEND, and the argument order is
    // unchanged from Rev 5.4.
    // -------------------------------------------------------------------------
    {"THREADS",     CMDfunction, -1, (void *)listTasks,    NULL, "Rev 5.4 alias for TASKS"},
    {"THREAD",      CMDfunction, -1, (void *)reportTask,   NULL, "Rev 5.4 alias for TASK"},
    {"STHRDENA",    CMDfunction, -1, (void *)enableTask,   NULL, "Rev 5.4 alias for TASKENA"},
    {"STHRDINT",    CMDfunction, -1, (void *)intervalTask, NULL, "Rev 5.4 alias for TASKINT"},
    {"THRDRESTART", CMDfunction, -1, (void *)restartTasks, NULL, "Rev 5.4 alias for RESTART"},
    {"SSPND",       CMDfunction, -1, (void *)suspendTasks, NULL, "Rev 5.4 alias for SUSPEND"},

    {NULL} // sentinel
};

static CommandList schedulerList = {schedulerCmds, NULL};

CommandList *taskScheduler::schedulerCommands(void)
{
    return &schedulerList;
}
