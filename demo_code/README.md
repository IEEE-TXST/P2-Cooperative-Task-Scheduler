# P2 Demo Code

`01_scheduler_from_scratch/` is a complete, working 3-task round-robin scheduler. It is split
into the **engine** (which you read and study) and the **demo** (which you step through):

- `scheduler.h` / `scheduler.c`: the task control block, the stack-frame builder, the
  round-robin picker, `SysTick_Handler`, and `SchedulerStart()`.
- `pendsv_handler.S`: the context switcher itself, written in real ARM assembly. Adapted from
  the ARM_CM0 port in the FreeRTOS kernel (`github.com/FreeRTOS/FreeRTOS-Kernel`,
  `portable/GCC/ARM_CM0/port.c`), the standard reference for exactly this problem on this
  processor family. This project does not link against FreeRTOS or reuse its code. See the
  manual, Section 9, for the full line-by-line walkthrough.
- `main.c`: the **step-by-step file**. It starts almost empty (the code is there but switched
  off) and you turn on one step at a time: first a do-nothing idle task, then the scheduler,
  then an LED task, a UART counter task, and a touch task. All the explanation is written
  inside the file.

The engine files are the lesson of this project, so read them and draw the stack frame yourself
(Session 1's whiteboard exercise). But you do **not** edit them to use the demo.

## How to use the demo

1. Create a new project with the SDK wizard. The P0 manual, Section 9, Step 1, shows exactly how.
2. Copy everything in `main.c` over your project's main source file.
3. Replace your project's `pin_mux.c` with the `pin_mux.c` in this folder. It sets up the LED and
   touch pins. If you forget this step the program still builds and runs, but the LED does
   nothing, with no error message.
4. Copy `scheduler.c`, `scheduler.h` and `pendsv_handler.S` into your project's source folder.
   Do not edit them.
5. Follow the steps inside `main.c`. **The order you turn the steps on is not the order they
   appear in the file**: the file header explains why (the call that starts the scheduler must
   sit last in `main()`).

## For project leaders

- The engine was compiled and its two hand-written assembly functions were disassembled and
  checked instruction by instruction against the intended design. The new step-by-step `main.c`
  has not been built and flashed on every step. Please build each step in order on a real board
  before the session. In particular, check that turning on the scheduler start step without
  filling all three task slots crashes the board as the file warns, and that following the file's
  order never does.
