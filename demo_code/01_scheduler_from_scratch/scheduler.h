/*
 * P2 scheduler, from scratch. No FreeRTOS, no OS underneath this.
 *
 * The context-switch mechanics (stack frame layout, PendSV handler,
 * first-task launch) are adapted from the ARM_CM0 port shipped in the
 * FreeRTOS kernel (github.com/FreeRTOS/FreeRTOS-Kernel,
 * portable/GCC/ARM_CM0/port.c), which is the standard, widely-deployed
 * reference for exactly this problem on exactly this processor family
 * (Cortex-M0 / M0+ share the same ARMv6-M instruction set). This project
 * does not link against FreeRTOS or use any of its code; it reimplements
 * the same proven technique as a small, original, 3-task round-robin
 * scheduler, which is what the manual (Section 9) asks you to understand
 * well enough to have written yourself.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define SCHED_NUM_TASKS 3U

typedef void (*task_func_t)(void);

/*
 * Task Control Block.
 *
 * "sp" MUST be the first member. The PendSV handler (pendsv_handler.S)
 * reaches it with a plain "ldr r0, [r1]", meaning "read the first word of
 * whatever g_currentTask points at." If you add fields above sp, or
 * reorder this struct, the context switch will read the wrong memory and
 * the board will hard fault or run garbage.
 */
typedef struct
{
    uint32_t *sp;      /* saved stack pointer; must be offset 0 */
    const char *name;  /* for the UART dashboard, not used by the switcher */
} tcb_t;

/* The currently running task. Read and written by pendsv_handler.S;
   do not rename without updating the assembly file's "=g_currentTask". */
extern tcb_t *g_currentTask;

/* Ticks since the scheduler started, incremented in SysTick_Handler. */
extern volatile uint32_t g_tickCount;

/*
 * Registers one task in slot "index" (0 to SCHED_NUM_TASKS - 1). Call this
 * for every task before SchedulerStart(); SchedulerStart() does not return.
 */
void SchedulerAddTask(uint32_t index, const char *name, task_func_t taskFunc);

/*
 * Configures SysTick for a 1 ms tick, sets PendSV and SysTick to the
 * lowest NVIC priority (Section 8 explains why), and launches task 0.
 * Never returns.
 */
void SchedulerStart(void);

#endif /* SCHEDULER_H */
