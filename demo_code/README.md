# P2 Demo Code

Reference material only, and more than usually worth trying yourself first: this project's
whole point is that you understand every line of the context switch, not that you have a
working binary. Open `01_scheduler_from_scratch/` only after you've drawn the stack frame
yourself (Session 1's whiteboard exercise) and attempted the PendSV handler yourself.

`01_scheduler_from_scratch/` is a complete, working 3-task round-robin scheduler:

- `scheduler.h` / `scheduler.c`: the TCB struct, the stack-frame builder, the round-robin
  picker, `SysTick_Handler`, and `SchedulerStart()`.
- `pendsv_handler.S`: the context switcher itself, written in real ARM assembly (a `.S` file,
  not inline C), because that's what the project asks you to produce. Adapted from the
  ARM_CM0 port shipped in the FreeRTOS kernel
  (`github.com/FreeRTOS/FreeRTOS-Kernel`, `portable/GCC/ARM_CM0/port.c`), the standard,
  widely-deployed reference for exactly this problem on exactly this processor family. This
  project does not link against FreeRTOS or reuse its code; it reimplements the same proven
  register save/restore technique as a small, original scheduler. See the manual, Section 9,
  for why this specific reference was used and a full line-by-line walkthrough.
- `main.c`: three demo tasks (LED blink at two different rates, a UART counter, and a touch
  slider read) that exercise the scheduler.

**This project was compiled and disassembled to verify it, not just written.** It builds
cleanly against `SDK_2_2_0_FRDM-KL26Z` (14 KB of a 128 KB flash budget), and the two hand-
written assembly functions were disassembled after linking and checked instruction-by-
instruction against the intended design; both matched exactly. It has not been flashed to
physical hardware. A project leader must bench-test it on a real board before WS4/WS5,
the same way every prior project's combined reference has needed a real-hardware check.

To build it, see the P0 manual, Section 8, for the general `cmake` / `make` / `objcopy` flow;
`armgcc/` here already has its build scripts patched for this repository's folder layout, the
same fixes P1's combined dashboard needed (paths pointed at the sibling `SDK_2_2_0_FRDM-KL26Z/`
folder, linker script path quoted for the space in "IEEE Projects").
