/*
 * P2 scheduler, from scratch: task setup, the round-robin picker, and the
 * SysTick tick source. The actual register save/restore lives in
 * pendsv_handler.S; see that file, and Section 9 of the manual, for the
 * part that does the real work.
 */
#include "scheduler.h"
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"

#define SCHED_STACK_WORDS 256U /* 1 KB per task; generous headroom for PRINTF's internal buffer */
#define SCHED_INITIAL_XPSR 0x01000000UL /* Thumb bit (bit 24) set; required, this chip has no ARM mode */

static uint32_t s_taskStacks[SCHED_NUM_TASKS][SCHED_STACK_WORDS];
static tcb_t s_tasks[SCHED_NUM_TASKS];
static uint32_t s_currentIndex = 0U;

tcb_t *g_currentTask;
volatile uint32_t g_tickCount = 0U;

/* Declared in pendsv_handler.S. */
extern void PendSV_Handler(void);
extern void SchedulerStartFirstTask(void);

static void TaskExitError(void)
{
    /* A task function must never return; there is nothing to return to,
       since this scheduler never allocated a caller for it. If you land
       here, a task's while(1) is missing. */
    for (;;)
    {
    }
}

/*
 * Builds the fake exception stack frame a brand-new task needs so that
 * the PendSV handler can "restore" it exactly as if it had been running
 * and gotten interrupted. Layout matches what the Cortex-M0+ hardware
 * pushes automatically on real exception entry (R0-R3, R12, LR, PC, xPSR,
 * low to high address), plus R4-R11 below that, which software must
 * manage itself (Section 5 explains why the split falls where it does).
 */
static uint32_t *InitTaskStack(uint32_t *topOfStack, task_func_t taskFunc)
{
    topOfStack--;
    *topOfStack = SCHED_INITIAL_XPSR; /* xPSR */
    topOfStack--;
    *topOfStack = (uint32_t)taskFunc; /* PC: where this task starts executing */
    topOfStack--;
    *topOfStack = (uint32_t)TaskExitError; /* LR: where it would "return" to, never used correctly */
    topOfStack -= 4; /* R12, R3, R2, R1: never initialized, tasks take no arguments here */
    topOfStack--;
    *topOfStack = 0U; /* R0 */
    topOfStack -= 8;  /* R11..R4: never initialized, first swap-in doesn't care what they hold */
    return topOfStack;
}

void SchedulerAddTask(uint32_t index, const char *name, task_func_t taskFunc)
{
    uint32_t *topOfStack = &s_taskStacks[index][SCHED_STACK_WORDS];
    s_tasks[index].sp = InitTaskStack(topOfStack, taskFunc);
    s_tasks[index].name = name;
}

/*
 * Called from PendSV, after the outgoing task's context is already saved.
 * This is the entire scheduling policy: move to the next slot, wrapping
 * around. No priorities, no blocked/ready lists, on purpose, this is the
 * simplest fair policy there is (Section 10).
 */
void SchedulerSelectNextTask(void)
{
    s_currentIndex = (s_currentIndex + 1U) % SCHED_NUM_TASKS;
    g_currentTask = &s_tasks[s_currentIndex];
}

void SysTick_Handler(void)
{
    g_tickCount++;
    /* Request a context switch. Never call the switch logic directly from
       here; always go through PendSV. Section 6 explains why. */
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void SchedulerStart(void)
{
    /* PendSV and SysTick both run at the lowest possible priority on this
       chip (3 of 0-3). See Section 8 for why PendSV specifically must
       never preempt a real interrupt. */
    NVIC_SetPriority(PendSV_IRQn, 3U);
    NVIC_SetPriority(SysTick_IRQn, 3U);

    g_currentTask = &s_tasks[0];
    s_currentIndex = 0U;

    /* 1 ms tick: core clock / 1000, minus 1 because SysTick counts down
       from LOAD to 0 inclusive. */
    SysTick_Config(SystemCoreClock / 1000U);

    SchedulerStartFirstTask(); /* never returns */
}
