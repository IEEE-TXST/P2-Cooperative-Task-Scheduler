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
#include <string.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_tsi_v4.h"
#include "scheduler.h"

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

    SchedulerAddTask(0U, "LED", LedBlinkTask);
    SchedulerAddTask(1U, "UART-Counter", UartCounterTask);
    SchedulerAddTask(2U, "TouchSlider", TouchSliderTask);

    SchedulerStart(); /* never returns */

    for (;;)
    {
        /* unreachable */
    }
}
