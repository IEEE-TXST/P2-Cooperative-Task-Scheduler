/* ==========================================================================
 *  P2: RUNNING THREE TASKS AT ONCE, A STEP-BY-STEP LADDER
 * ==========================================================================
 *
 *  Goal: run three separate jobs "at the same time" on a chip that has only
 *  one CPU core. The scheduler (scheduler.c and pendsv_handler.S) rapidly
 *  switches between them, so fast that they all appear to run at once.
 *
 *    Task 1 blinks the red LED and the blue LED at different speeds.
 *    Task 2 prints a counter to the terminal.
 *    Task 3 reads a touch pad and prints how hard you are touching it.
 *
 *  BEFORE YOU START: SET UP YOUR PROJECT (4 quick steps)
 *
 *    1. Create a new project with the SDK wizard. The P0 manual, Section 9,
 *       Step 1, shows exactly how.
 *    2. Paste this whole file over the contents of your project's main
 *       source file.
 *    3. Replace your project's pin_mux.c with the pin_mux.c that sits next
 *       to this file. It tells the chip which pins are the LED, the touch
 *       pads and so on. Forget it and the program still builds and runs,
 *       but the LED does nothing, with no error message at all.
 *    4. Copy these three files into your project's source folder next to
 *       the main file, and do NOT edit them:
 *
 *           scheduler.c      scheduler.h      pendsv_handler.S
 *
 *  Those three are the engine. This file is the part you play with. How the
 *  engine works inside is the lesson in the manual (Sections 5 to 10). This
 *  file is about how to USE it.
 *
 *  This file starts almost empty on purpose. The code for each step is
 *  already here, but turned OFF (commented out). You switch it on one step
 *  at a time, run it on the board, and see what changed.
 *
 *  HOW TO USE THIS FILE
 *
 *    1. Read the STEP box. It explains the idea in plain words.
 *    2. Select ALL the grey "//" code lines under it (the whole block).
 *    3. Press  Cmd + /  (Mac)  or  Ctrl + /  (Windows)  to turn them ON.
 *    4. Build, flash, and watch the board and the terminal.
 *    5. Do the "TRY IT" exercise, then move on to the next STEP.
 *
 *  Each box has a "NEEDS" line telling you which earlier steps must be on.
 *
 *  THE ONE RULE
 *
 *    Text inside  / * ... * /  boxes is explanation. Leave it alone.
 *    Lines starting with  //  are code that is switched off. Those are the
 *    only lines you ever turn on.
 *
 *  HOW THIS FILE IS LAID OUT
 *
 *    PART A (STEPS 1 to 4, above main): writes the four task functions.
 *    Nothing runs yet. Each step just needs to build with no errors.
 *
 *    PART B (STEPS 5 to 10, inside main): starts the scheduler and adds
 *    the tasks one at a time.
 *
 *  !! IMPORTANT: THE ORDER YOU TURN PART B ON IS DIFFERENT FROM THE ORDER
 *  !! IT APPEARS IN THE FILE. Follow the step NUMBERS, not the position:
 *  !!
 *  !!      turn on:  5, 6, 7, 8, 9, 10      (in that order)
 *  !!
 *  !! STEP 7 (starting the scheduler) must sit LAST in the file, because it
 *  !! never returns. Anything after it would never run. So in the file you
 *  !! will see STEP 7 at the bottom of main(), below STEPS 8 to 10. That is
 *  !! correct. Skip over it and come back when it is its turn.
 *
 *  THE BIG DANGER: AN EMPTY SLOT CRASHES THE BOARD
 *    The scheduler has exactly three task slots. It rotates through all
 *    three, forever. If a slot was never filled, it switches into nothing
 *    and the chip crashes (a "hard fault"). So we fill ALL THREE slots with
 *    a do-nothing IdleTask first (STEP 6), then replace them with real
 *    tasks one at a time (STEPS 8 to 10). That way the board never crashes
 *    no matter which step you stop at.
 * ========================================================================== */


/* --------------------------------------------------------------------------
 *  INCLUDES (already on, nothing to do)
 *
 *  scheduler.h gives us SchedulerAddTask(), SchedulerStart() and
 *  g_tickCount (a counter that goes up by one every millisecond, kept by
 *  the scheduler). fsl_tsi_v4.h is for the touch pad in Task 3.
 * -------------------------------------------------------------------------- */

#include <string.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_tsi_v4.h"
#include "scheduler.h"





/* ==========================================================================
 *  PART A, STEP 1: WHAT IS A TASK? THE IDLE TASK
 *
 *  CONCEPT
 *    A task is just a normal C function with one special rule: it NEVER
 *    returns. It has an endless loop inside. There is nobody to return to,
 *    because the scheduler launched it directly.
 *
 *    The scheduler interrupts whichever task is running about every
 *    millisecond, saves where it was, and jumps to the next task. The
 *    task has no idea this happens. From inside a task, it feels like it
 *    owns the whole CPU. That is the whole trick: a good scheduler is
 *    invisible to the tasks.
 *
 *    IdleTask is the simplest possible task: loop forever, do nothing. We
 *    use it as a harmless placeholder for slots that have no real job yet.
 *
 *  NEEDS: nothing. Safe to flash right away.
 *
 *  TRY IT
 *    Turn this on and build. Notice the shape  for (;;) { }  which is
 *    another way of writing an endless loop, the same as  while (1).
 *
 *  NEXT: go to STEP 2.
 * ========================================================================== */

//void IdleTask(void)
//{
//    for (;;)
//    {
//    }
//}





/* ==========================================================================
 *  PART A, STEP 2: TASK 1, THE TWO-SPEED LED BLINKER
 *
 *  CONCEPT
 *    This one task blinks BOTH the red LED (every 250 ms) and the blue LED
 *    (every 80 ms). Two different speeds from one function. How?
 *
 *    The scheduler keeps a clock, g_tickCount, that goes up by 1 every
 *    millisecond. The task remembers when it last toggled each LED
 *    (lastRedToggle, lastBlueToggle) and asks again and again:
 *
 *        has enough time passed since I last toggled?
 *
 *    The test is  (now - lastRedToggle) >= 250U  and NOT  now == 250.
 *    Why? If the task is paused a moment too long, the exact tick would
 *    slip by and the toggle would be missed forever. With ">=", a late
 *    check just sees a bigger difference and toggles a little late. It
 *    never misses.
 *
 *    The task never "sleeps" or hands control back. It just keeps checking.
 *    The scheduler takes care of letting other tasks have their turn.
 *
 *    One-time setup runs once at the top, before the endless loop: both
 *    LED pins become outputs and start OFF (1 = off on this board).
 *
 *  NEEDS: nothing. Safe to flash right away. It is only defined, not
 *  started.
 *
 *  TRY IT
 *    Turn this on and build. Find the two numbers 250U and 80U. Those
 *    control the blink speeds.
 *
 *  NEXT: go to STEP 3.
 * ========================================================================== */

//void LedBlinkTask(void)
//{
//    gpio_pin_config_t ledConfig;
//    uint32_t lastRedToggle = 0U;
//    uint32_t lastBlueToggle = 0U;
//
//    ledConfig.pinDirection = kGPIO_DigitalOutput;
//    ledConfig.outputLogic = 1U;
//
//    GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &ledConfig);
//    GPIO_PinInit(BOARD_LED_BLUE_GPIO, BOARD_LED_BLUE_GPIO_PIN, &ledConfig);
//
//    for (;;)
//    {
//        uint32_t now = g_tickCount;
//
//        if ((now - lastRedToggle) >= 250U)
//        {
//            GPIO_TogglePinsOutput(BOARD_LED_RED_GPIO, 1U << BOARD_LED_RED_GPIO_PIN);
//            lastRedToggle = now;
//        }
//
//        if ((now - lastBlueToggle) >= 80U)
//        {
//            GPIO_TogglePinsOutput(BOARD_LED_BLUE_GPIO, 1U << BOARD_LED_BLUE_GPIO_PIN);
//            lastBlueToggle = now;
//        }
//    }
//}





/* ==========================================================================
 *  PART A, STEP 3: TASK 2, THE UART COUNTER
 *
 *  CONCEPT
 *    Every 300 ms this task prints its name, the current tick count, and
 *    its own private counter. It uses the exact same "has enough time
 *    passed?" pattern as Task 1.
 *
 *    This task exists mostly to PROVE the scheduler works. If you watch
 *    the printed lines and the LEDs at the same time, and both keep going,
 *    then the scheduler really is switching between tasks. If they were
 *    running one after the other, only one would ever move.
 *
 *    Notice "count" is a local variable that this task alone owns. Each
 *    task has its own private memory (its own stack), so tasks cannot
 *    accidentally overwrite each other's variables.
 *
 *  NEEDS: nothing. Safe to flash right away. It is only defined, not
 *  started.
 *
 *  TRY IT
 *    Turn this on and build.
 *
 *  NEXT: go to STEP 4.
 * ========================================================================== */

//void UartCounterTask(void)
//{
//    uint32_t lastPrint = 0U;
//    uint32_t count = 0U;
//
//    for (;;)
//    {
//        uint32_t now = g_tickCount;
//
//        if ((now - lastPrint) >= 300U)
//        {
//            count++;
//
//            PRINTF("[UART-Counter] tick=%u count=%u\r\n", now, count);
//
//            lastPrint = now;
//        }
//    }
//}





/* ==========================================================================
 *  PART A, STEP 4: TASK 3, THE TOUCH PAD READER
 *
 *  CONCEPT
 *    Every 400 ms this task reads touch pad E1 and prints how far above its
 *    "no touch" baseline the reading is. You met touch sensing in P1
 *    Demo 5. Read that demo's boxes if it is unfamiliar.
 *
 *    Setup at the top runs once: start the touch hardware, then measure the
 *    baseline. So do not touch the pad when the board starts.
 *
 *    A realistic detail worth noticing: the touch read has an empty
 *    "while" loop that WAITS for the scan to finish. While this task
 *    waits, it is not doing anything else. The scheduler can still switch
 *    away from it at the next tick, but this task does hold up its own
 *    turn. A task that waits a long time delays how quickly it gets
 *    back to its other work.
 *
 *  NEEDS: nothing. Safe to flash right away. It is only defined, not
 *  started.
 *
 *  TRY IT
 *    Turn this on and build. That finishes Part A: all four tasks are
 *    written. Now Part B puts them to work.
 *
 *  NEXT: go to STEP 5 inside main().
 * ========================================================================== */

//void TouchSliderTask(void)
//{
//    tsi_config_t tsiConfig;
//    tsi_calibration_data_t baseline;
//    uint32_t lastSample = 0U;
//
//    TSI_GetNormalModeDefaultConfig(&tsiConfig);
//    TSI_Init(TSI0, &tsiConfig);
//    TSI_EnableModule(TSI0, true);
//
//    memset((void *)&baseline, 0, sizeof(baseline));
//    TSI_Calibrate(TSI0, &baseline);
//
//    for (;;)
//    {
//        uint32_t now = g_tickCount;
//
//        if ((now - lastSample) >= 400U)
//        {
//            int32_t delta;
//            uint16_t counter;
//
//            TSI_SetMeasuredChannelNumber(TSI0, BOARD_TSI_ELECTRODE_1);
//            TSI_StartSoftwareTrigger(TSI0);
//
//            while (0U == (TSI_GetStatusFlags(TSI0) & kTSI_EndOfScanFlag))
//            {
//            }
//
//            counter = TSI_GetCounter(TSI0);
//            TSI_ClearStatusFlags(TSI0, kTSI_EndOfScanFlag);
//
//            delta = (int32_t)counter - (int32_t)baseline.calibratedData[BOARD_TSI_ELECTRODE_1];
//
//            PRINTF("[TouchSlider] tick=%u electrode1_delta=%d\r\n", now, delta);
//
//            lastSample = now;
//        }
//    }
//}





int main(void)
{

    /* ======================================================================
     *  PART B, STEP 5: WAKE UP THE BOARD
     *
     *  CONCEPT
     *    Three setup calls run once, in this order, before anything else:
     *
     *      BOARD_InitPins()          decide what each pin does
     *      BOARD_BootClockRUN()      start the clock (the chip's heartbeat)
     *      BOARD_InitDebugConsole()  open the serial line to your computer
     *
     *    Then we print a title and a warning. The warning is honest: two
     *    tasks will print to the same serial line, and nothing stops them
     *    from talking at the same moment. You will sometimes see two lines
     *    mixed together mid-sentence. That is a real problem in real
     *    systems, and you will think about how to fix it in the exercises.
     *
     *  NEEDS: nothing from Part A. Safe to flash right away.
     *
     *  TRY IT
     *    Turn the whole block on, flash, and open the terminal. You should
     *    see the title and the warning. Change the title to your own words.
     *
     *  NEXT: go to STEP 6.
     * ==================================================================== */

    //BOARD_InitPins();
    //BOARD_BootClockRUN();
    //BOARD_InitDebugConsole();
    //
    //PRINTF("\r\n=== P2 Cooperative Scheduler ===\r\n");
    //PRINTF("Starting 3 tasks. Occasional interleaved lines are expected,\r\n");
    //PRINTF("nothing protects the shared UART yet.\r\n\r\n");





    /* ======================================================================
     *  PART B, STEP 6: FILL ALL THREE SLOTS WITH THE IDLE TASK
     *
     *  CONCEPT
     *    SchedulerAddTask(slot, name, function) puts a task into one of the
     *    scheduler's three fixed slots (0, 1, 2). Behind the scenes it
     *    builds a pretend "saved state" on the task's private stack, so the
     *    very first time the scheduler switches to the task, it looks
     *    exactly like a task that was already running and got paused.
     *
     *    All three slots must be filled before the scheduler starts, or the
     *    board crashes (see the warning at the top of this file). So here
     *    we put the harmless IdleTask in all three. In STEPS 8 to 10 we
     *    will overwrite them with real tasks, one by one. Adding to a slot
     *    that is already filled simply replaces it.
     *
     *  NEEDS: STEPS 1 and 5 already on. Safe to flash, but nothing runs
     *  until STEP 7.
     *
     *  TRY IT
     *    Turn this on and build. Now go and turn on STEP 7 (at the bottom
     *    of main()), then come back here for STEP 8.
     *
     *  NEXT: go to STEP 7 (the bottom of main), then STEP 8.
     * ==================================================================== */

    //SchedulerAddTask(0U, "Idle0", IdleTask);
    //SchedulerAddTask(1U, "Idle1", IdleTask);
    //SchedulerAddTask(2U, "Idle2", IdleTask);





    /* ======================================================================
     *  PART B, STEP 8: ADD TASK 1, THE LED BLINKER
     *
     *  CONCEPT
     *    This replaces the idle task in slot 0 with the LED blinker. Slot 0
     *    is where the scheduler launches first.
     *
     *    This block sits ABOVE STEP 7 in the file on purpose, because
     *    every task must be registered BEFORE the scheduler starts.
     *
     *  NEEDS: STEPS 1, 2, 5, 6 and 7 all on.
     *
     *  TRY IT
     *    Turn this on and flash. The red LED should blink slowly and the
     *    blue LED quickly, roughly 3 blue blinks for every 1 red. The other
     *    two slots are still doing nothing.
     *
     *    Then try: in STEP 2, change 250U to 1000U and flash. Red is now
     *    much slower. Put it back afterward.
     *
     *  NEXT: go to STEP 9.
     * ==================================================================== */

    //SchedulerAddTask(0U, "LED", LedBlinkTask);





    /* ======================================================================
     *  PART B, STEP 9: ADD TASK 2, THE UART COUNTER
     *
     *  CONCEPT
     *    Replaces the idle task in slot 1.
     *
     *  NEEDS: STEPS 1 to 3, 5, 6, 7 and 8 all on.
     *
     *  TRY IT
     *    Turn this on and flash. Open the terminal. You should see a new
     *    line about three times a second, while the LEDs keep blinking at
     *    the same time. Two things running together, on one core. That is
     *    the scheduler at work.
     *
     *    Watch the "tick=" value. It goes up by roughly 300 each line,
     *    because the task prints every 300 ms and the tick counter counts
     *    milliseconds.
     *
     *  NEXT: go to STEP 10.
     * ==================================================================== */

    //SchedulerAddTask(1U, "UART-Counter", UartCounterTask);





    /* ======================================================================
     *  PART B, STEP 10: ADD TASK 3, THE TOUCH READER
     *
     *  CONCEPT
     *    Replaces the idle task in slot 2. Now all three real tasks are in.
     *
     *  NEEDS: STEPS 1 to 9 all on. Do not touch the pad as the board starts.
     *
     *  TRY IT
     *    Turn this on and flash. Now you should see TWO kinds of printed
     *    lines, UART-Counter and TouchSlider, plus both LEDs blinking.
     *    Touch pad E1 and watch the electrode1_delta number jump. Take
     *    your finger away and it falls back near zero.
     *
     *    Look closely for a line that is garbled, with two tasks' text
     *    mixed together. That is the shared-UART problem from STEP 5.
     *
     *  NEXT: go to the exercises below.
     * ==================================================================== */

    //SchedulerAddTask(2U, "TouchSlider", TouchSliderTask);





    /* ======================================================================
     *  PART B, STEP 7: START THE SCHEDULER (TURN THIS ON AFTER STEP 6)
     *
     *  CONCEPT
     *    SchedulerStart() sets up the 1 millisecond tick, launches the
     *    first task, and NEVER RETURNS. From here on, main() is finished.
     *    All the real work happens inside the tasks, switched by the
     *    scheduler's timer. That is why this block must be the very last
     *    thing in main(): anything placed after it would never run.
     *
     *    The empty  for (;;)  after it can never actually be reached. C
     *    just wants a statement after the call, even one the compiler can
     *    tell will not be reached.
     *
     *  NEEDS: STEPS 1, 5 and 6 on. If STEP 6 is off, this will crash the
     *  board, because the empty slots have nothing to run.
     *
     *  TRY IT
     *    Turn this on right after STEP 6, build, and flash. You will see
     *    the title in the terminal and nothing else. Three do-nothing
     *    tasks are taking turns. That is correct. The scheduler is running.
     *    Leave this ON for the rest of the ladder.
     *
     *  NEXT: go back up to STEP 8.
     * ==================================================================== */

    //SchedulerStart();
    //
    //for (;;)
    //{
    //}

}





/* ==========================================================================
 *  EXERCISES
 *
 *  1. Change the blink rates. In STEP 2, change 80U to 20U, and 250U to
 *     500U. How does it look now? Is there a speed at which the LED just
 *     looks dim and steady?
 *
 *  2. Prove the tasks really overlap. In UartCounterTask (STEP 3), change
 *     300U to 100U. The prints triple, the LEDs keep their rhythm. Does
 *     printing more often disturb the LEDs?
 *
 *  3. Make the garbling worse on purpose. In UartCounterTask (STEP 3) and
 *     TouchSliderTask (STEP 4), set both print intervals to the SAME
 *     number, like 300U. Two tasks now try to print at the same instant.
 *     How often does a line get mixed up? Think about how you could fix
 *     it. (Hint: only one task at a time should be allowed to use the
 *     serial line. This is called a "mutex" or "lock".)
 *
 *  4. Hog the CPU. Inside LedBlinkTask (STEP 2), after the two "if"
 *     blocks, add a long busy-wait:
 *
 *         volatile uint32_t w;
 *         for (w = 0; w < 2000000U; w++) { }
 *
 *     Do the UART lines keep coming? Do the LEDs still blink on time?
 *     What does this tell you about what the scheduler can and cannot
 *     promise a task?
 *
 *  5. Make a fourth task. Open scheduler.h and change SCHED_NUM_TASKS from
 *     3U to 4U. Write a new task function that prints "hello" every
 *     second. Register it in slot 3 with SchedulerAddTask(3U, ...). Fill
 *     the new slot with IdleTask first, as you learned in STEP 6. What
 *     happens to each task's share of the CPU time with four tasks instead
 *     of three?
 *
 *  6. Cause the crash on purpose. Turn STEP 10 off, and in STEP 6 delete
 *     the line that fills slot 2. Run it. The board stops doing useful
 *     work. Why? (Slot 2 was never filled, so when the scheduler rotates
 *     to it, there is nothing to run.) Put it back afterward.
 *
 *  WHERE TO GO NEXT
 *    P3: DMA and ADC. You will have the hardware itself move data around
 *    without the CPU, and apply a digital filter to clean up a signal.
 * ========================================================================== */
