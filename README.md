# P2: Custom Cooperative Task Scheduler

*Build a real-time kernel from scratch: SysTick, PendSV, and ARM context switching.*

IEEE TXST Student Branch, Fall 2026 FRDM-KL26Z project series.

**Platform:** NXP FRDM-KL26Z (ARM Cortex-M0+, 48 MHz, 128 KB Flash). No extra hardware. Prerequisite: P1 complete, comfortable with NVIC, interrupts, and UART.

## What this project is

Members build a cooperative round-robin task scheduler entirely from scratch, using SysTick as the time base and PendSV as the context switcher, with the switcher written in ARM assembly. No FreeRTOS, no OS underneath it, just the processor's own exception model and direct stack manipulation. The goal is understanding what an RTOS actually does under the hood: how tasks share one CPU, how registers get saved and restored, and why priority levels matter. The demo runs three concurrent tasks on the same CPU with visible proof of scheduling.

## Exit criteria

Three tasks running on the scheduler simultaneously. UART output shows each task's name and tick count interleaved, proving the scheduler is actually switching between them.

## Repo layout

- `P2_1_Start_Here.md` through `P2_4_Reference.md`: the Project Manual, split into four files.
- `demo_code/01_scheduler_from_scratch/`:
  - `scheduler.c` / `scheduler.h`: task setup, the round-robin picker, and the SysTick tick source.
  - `pendsv_handler.S`: the context switcher itself, written in ARM assembly, adapted from the technique in FreeRTOS's own verified ARM_CM0 port (this project doesn't link against FreeRTOS or use any of its code).
  - `main.c`: three demo tasks (LED blink, UART counter, touch slider read) registered with the scheduler.
- `demo_code/_reference_drivers/`: vendored NXP SDK driver source, reference only.

## Getting started

Read `P2_1_Start_Here.md` first. Section 9 of the manual walks through the context switch line by line; work through the whiteboard exercise (Session 1) before writing any assembly.

## Resume line

> Implemented cooperative round-robin task scheduler on ARM Cortex-M0+ from scratch using SysTick and PendSV exception handlers; wrote context switcher in ARM assembly with no RTOS dependency.
