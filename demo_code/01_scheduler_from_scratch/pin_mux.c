/*
 * P2 demo pin mux: UART0 console (from P0), red and blue LEDs (from P0/P1),
 * and the two TSI touch slider electrodes (from P1). No I2C, ADC, or PWM
 * pins here; this project doesn't use them. Every mux value verified the
 * same way as P0 and P1: against a working SDK example, not guessed.
 */
#include "fsl_common.h"
#include "fsl_port.h"
#include "pin_mux.h"

#define PIN1_IDX 1u
#define PIN2_IDX 2u
#define PIN5_IDX 5u
#define PIN16_IDX 16u
#define PIN17_IDX 17u
#define PIN29_IDX 29u

#define SOPT5_UART0TXSRC_UART_TX 0x00u
#define SOPT5_UART0RXSRC_UART_RX 0x00u

void BOARD_InitPins(void)
{
    CLOCK_EnableClock(kCLOCK_PortA);
    CLOCK_EnableClock(kCLOCK_PortB);
    CLOCK_EnableClock(kCLOCK_PortD);
    CLOCK_EnableClock(kCLOCK_PortE);

    /* UART0 debug console. */
    PORT_SetPinMux(PORTA, PIN1_IDX, kPORT_MuxAlt2); /* PTA1 = UART0_RX */
    PORT_SetPinMux(PORTA, PIN2_IDX, kPORT_MuxAlt2); /* PTA2 = UART0_TX */
    SIM->SOPT5 = ((SIM->SOPT5 & (~(SIM_SOPT5_UART0TXSRC_MASK | SIM_SOPT5_UART0RXSRC_MASK))) |
                  SIM_SOPT5_UART0TXSRC(SOPT5_UART0TXSRC_UART_TX) | SIM_SOPT5_UART0RXSRC(SOPT5_UART0RXSRC_UART_RX));

    /* Red and blue LEDs, plain GPIO. */
    PORT_SetPinMux(PORTE, PIN29_IDX, kPORT_MuxAsGpio); /* PTE29 = red LED */
    PORT_SetPinMux(PORTD, PIN5_IDX, kPORT_MuxAsGpio);  /* PTD5 = blue LED */

    /* TSI0 touch slider electrodes, analog mode. */
    PORT_SetPinMux(PORTB, PIN16_IDX, kPORT_PinDisabledOrAnalog); /* PTB16 = TSI0_CH9 */
    PORT_SetPinMux(PORTB, PIN17_IDX, kPORT_PinDisabledOrAnalog); /* PTB17 = TSI0_CH10 */
}
