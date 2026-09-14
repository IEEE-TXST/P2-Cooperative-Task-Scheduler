/*
 * P2 demo: three tasks running on the from-scratch scheduler.
 *
 * - LedBlinkTask toggles the red LED every 250 ticks and the blue LED
 *   every 80 ticks, so two visibly different blink rates come out of one
 *   task, proving the scheduler is giving it CPU time regularly.
 * - UartCounterTask prints its own name, the global tick count, and a
 *   local counter every 300 ticks.
 * - TouchSliderTask samples one TSI electrode every 400 ticks and prints
 *   how far above baseline it reads.
 *
 * All three read the shared g_tickCount (read-only, safe) but nothing
 * protects the shared UART from being written by two tasks around the
 * same tick. See the manual, Section 11, for why that's left as-is on
 * purpose for this project.
 *
 * Reference only. Build your own three tasks first; open this only if
 * stuck on how the scheduler API is meant to be used.
 */

/*
 * WHAT: Registers three independent, endlessly-looping tasks with the P2
 * scheduler (scheduler.c/pendsv_handler.S) and starts it.
 *
 * HOW: Each task is a plain C function with an infinite for(;;) loop, never
 * returning. None of them call each other or explicitly hand off control;
 * the scheduler's SysTick + PendSV mechanism (see scheduler.c) interrupts
 * whichever task is running roughly every millisecond and switches to the
 * next one, completely transparently to the task's own code.
 *
 * WHY: This file deliberately contains no scheduler internals at all, no
 * stack manipulation, no PendSV, nothing: that's the entire point of
 * building a real scheduler underneath. From a task's point of view, it
 * just runs in an infinite loop as if it had the whole CPU to itself; the
 * scheduler is what makes three such "selfish" loops actually share one
 * processor. Comparing this file (simple, ordinary C) against scheduler.c
 * and pendsv_handler.S (dense, low-level) is itself the lesson: a good
 * scheduler is invisible from the task's side.
 */
#include <string.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_tsi_v4.h"
#include "scheduler.h"

/*
 * WHAT: Blinks red every 250 ticks (~250ms) and blue every 80 ticks
 * (~80ms), from inside one task.
 * HOW: Reads g_tickCount (incremented once per millisecond by
 * SysTick_Handler in scheduler.c) and compares it against two independent
 * "last toggled" timestamps, one per LED, toggling whichever LED's
 * interval has elapsed.
 * WHY: This is a "cooperative" scheduler: it interrupts a task on a timer,
 * but a task itself never explicitly yields or sleeps, it just keeps
 * checking whether enough time has passed. Comparing elapsed ticks
 * (`now - lastToggle >= interval`) instead of an exact equality check is
 * deliberate: it still works correctly even if this task doesn't get CPU
 * time on the exact tick the interval elapsed, since being a few ticks
 * late just means the subtraction is a little larger than the interval,
 * not that the toggle gets missed entirely.
 */
static void LedBlinkTask(void)
{
    gpio_pin_config_t ledConfig;
    uint32_t lastRedToggle = 0U;
    uint32_t lastBlueToggle = 0U;

    ledConfig.pinDirection = kGPIO_DigitalOutput;
    ledConfig.outputLogic = 1U; /* start off: active-low LEDs */
    GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &ledConfig);
    GPIO_PinInit(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN, &ledConfig);

    for (;;)
    {
        uint32_t now = g_tickCount;
        if ((now - lastRedToggle) >= 250U)
        {
            GPIO_TogglePinsOutput(BOARD_LED_RED_GPIO, 1U << BOARD_LED_RED_GPIO_PIN);
            lastRedToggle = now;
        }
        if ((now - lastBlueToggle) >= 80U)
        {
            GPIO_TogglePinsOutput(BOARD_LED_BLUE_GPIO, 1U << BOARD_LED_BLUE_GPIO_PIN);
            lastBlueToggle = now;
        }
    }
}

/*
 * WHAT: Prints a line with the current tick count and a local counter,
 * once every 300 ticks.
 * HOW: Same elapsed-ticks pattern as LedBlinkTask, just with one interval
 * and a PRINTF instead of a GPIO toggle.
 * WHY: This task exists mainly to prove multiple tasks are genuinely
 * running "at once": watching this task's UART output interleave with
 * LedBlinkTask's visible blinking (and occasionally garble mid-line with
 * TouchSliderTask's own PRINTF calls, per the file's top comment) is the
 * most direct evidence the scheduler is actually switching between three
 * independent tasks, not just running them one after another.
 */
static void UartCounterTask(void)
{
    uint32_t lastPrint = 0U;
    uint32_t count = 0U;

    for (;;)
    {
        uint32_t now = g_tickCount;
        if ((now - lastPrint) >= 300U)
        {
            count++;
            PRINTF("[UART-Counter] tick=%u count=%u\r\n", now, count);
            lastPrint = now;
        }
    }
}

/*
 * WHAT: Samples one TSI touch electrode every 400 ticks and prints how far
 * its reading is above the calibrated baseline.
 * HOW: One-time TSI setup and calibration at the top (identical in shape to
 * P1's touch slider demo), then the same elapsed-ticks pattern as the other
 * two tasks, doing a software-triggered polling TSI read each time the
 * interval elapses.
 * WHY: This task's setup code runs once, before its for(;;) loop, exactly
 * like the other tasks' one-time init; but note the TSI read itself
 * (`while` polling for kTSI_EndOfScanFlag) blocks this task for its
 * duration. That's a deliberate, realistic wrinkle Section 11 of the
 * manual discusses: a cooperative scheduler like this one has no way to
 * interrupt a task mid-instruction to run another task sooner, it can only
 * switch tasks on the next SysTick tick, so a task that blocks for a while
 * (like this TSI poll) delays how promptly other tasks get to run, even
 * though they're otherwise ready.
 */
static void TouchSliderTask(void)
{
    tsi_config_t tsiConfig;
    tsi_calibration_data_t baseline;
    uint32_t lastSample = 0U;

    TSI_GetNormalModeDefaultConfig(&tsiConfig);
    TSI_Init(TSI0, &tsiConfig);
    TSI_EnableModule(TSI0, true);
    memset((void *)&baseline, 0, sizeof(baseline));
    TSI_Calibrate(TSI0, &baseline);

    for (;;)
    {
        uint32_t now = g_tickCount;
        if ((now - lastSample) >= 400U)
        {
            int32_t delta;
            uint16_t counter;

            TSI_SetMeasuredChannelNumber(TSI0, BOARD_TSI_ELECTRODE_1);
            TSI_StartSoftwareTrigger(TSI0);
            while (0U == (TSI_GetStatusFlags(TSI0) & kTSI_EndOfScanFlag))
            {
            }
            counter = TSI_GetCounter(TSI0);
            TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);

            delta = (int32_t)counter - (int32_t)baseline.calibratedData[BOARD_TSI_ELECTRODE_1];
            PRINTF("[TouchSlider] tick=%u electrode1_delta=%d\r\n", now, delta);
            lastSample = now;
        }
    }
}

int main(void)
{
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    PRINTF("\r\n=== P2 Cooperative Scheduler reference ===\r\n");
    PRINTF("Starting 3 tasks. Occasional interleaved lines are expected,\r\n");
    PRINTF("nothing protects the shared UART yet. See manual Section 11.\r\n\r\n");

    /*
     * WHAT: Registers each task function into one of the scheduler's fixed
     * task slots (0, 1, 2), by name, before starting the scheduler.
     * HOW: SchedulerAddTask (scheduler.c) builds a fake initial stack frame
     * for each task so the PendSV handler can "resume" it into existence
     * the first time it's switched to, exactly as if it had already been
     * running and gotten interrupted.
     * WHY: All three tasks must be registered before SchedulerStart() is
     * called, since SchedulerStart() never returns; there is no
     * opportunity to register a fourth task later, or to register these
     * after the scheduler is already running.
     */
    SchedulerAddTask(0U, "LED", LedBlinkTask);
    SchedulerAddTask(1U, "UART-Counter", UartCounterTask);
    SchedulerAddTask(2U, "TouchSlider", TouchSliderTask);

    /*
     * WHAT: Hands control to the scheduler permanently.
     * WHY: main() effectively ends here; everything that happens after this
     * point happens inside one of the three tasks above, switched between
     * by SysTick/PendSV. The unreachable for(;;) below exists only because
     * C requires main() to have some statement after a void function call,
     * even one the compiler can prove never returns.
     */
    SchedulerStart(); /* never returns */

    for (;;)
    {
        /* unreachable */
    }
}
