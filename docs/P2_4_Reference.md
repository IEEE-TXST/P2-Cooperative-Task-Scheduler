# P2: Reference

*Part of the P2 manual split. See `P2_1_Start_Here.md` for the full file list and how to use this manual.*

---

## 13. Code Structure Explanation

| File | What It Does |
|---|---|
| `scheduler.h` | The `tcb_t` struct (Section 7) and the public API: `SchedulerAddTask()`, `SchedulerStart()`, and the two globals every task can read (`g_currentTask`, `g_tickCount`). |
| `scheduler.c` | `InitTaskStack()` (Section 7's fabricated frame), `SchedulerAddTask()`, `SchedulerSelectNextTask()` (Section 10), `SysTick_Handler()` (Section 8), and `SchedulerStart()` (sets NVIC priorities, configures SysTick, calls into the assembly to launch task 0). |
| `pendsv_handler.S` | `PendSV_Handler` (Section 9) and `SchedulerStartFirstTask` (Section 11). The only hand-written assembly in this project. |
| `main.c` | The three demo tasks (Section 12) and `main()`, which does the usual `BOARD_Init*()` calls, registers the tasks, and calls `SchedulerStart()`. |
| `pin_mux.c` | UART0, the red and blue LEDs, and the two TSI electrodes; no new pins beyond what P0 and P1 already established. |

## 14. Sample Output

```
=== P2 Cooperative Scheduler reference ===
Starting 3 tasks. Occasional interleaved lines are expected,
nothing protects the shared UART yet. See manual Section 11.

[UART-Counter] tick=300 count=1
[TouchSlider] tick=400 electrode1_delta=3
[UART-Counter] tick=600 count=2
[TouchSlider] tick=800 electrode1_delta=2
[UART-Counter] tick=900 count=3
```

The red and blue LEDs blinking at visibly different rates (roughly 2 Hz for blue, roughly 0.67 Hz for red, given the 250-tick and 80-tick periods against a 1 ms tick) is the part a camera can capture; the interleaved UART lines above are the part that proves two other tasks are also genuinely running concurrently, not just the LED task looping alone.

## 15. Session Plan (maps to Guideline Section 4.6)

| Meeting | Phase | What Members Do | Deliverable | Slide Focus |
|---|---|---|---|---|
| 1 of 2 | Whiteboard, SysTick, TCB | 30-minute whiteboard exercise (Section 6): draw the stack frame, trace an exception by hand. Implement SysTick ISR and the TCB struct in C (Sections 7 to 8). No context switch yet. | SysTick firing at 1 ms, UART printing tick count. TCB struct compiling clean. | Photo of the whiteboard drawing; terminal showing the tick count; what was confusing and how it got resolved. |
| 2 of 2 | PendSV handler and 3-task demo | Write the PendSV handler in ARM assembly (Section 9), using `pendsv_handler.S` only as a check after attempting it. Wire SysTick to trigger PendSV. Build the 3-task demo (Section 12). | Three tasks running concurrently; UART output shows task switching; LED blink rates visibly different. | Live demo is the whole slide; be ready to trace through the PendSV handler on request, this is the most architecturally significant code in the series and the demo should prove you understand it, not just that it runs. |

## 16. Milestones and Success Criteria

| Milestone | Success Criteria | Evidence |
|---|---|---|
| Whiteboard exercise | Group can trace an exception by hand without notes | Photo of the drawing, verbal check by leader |
| SysTick at 1 ms (Session 1 exit criteria) | Tick count printed over UART, increasing by roughly 1000 per second | Terminal screenshot |
| TCB struct | Compiles clean; `sp` confirmed as the first member | Code review by leader |
| PendSV context switch | Two tasks visibly alternate without either one appearing to "own" the CPU | Live demo |
| Full 3-task demo (Session 2 exit criteria) | All three tasks running concurrently; UART shows interleaved output from at least two of them; LED blink rates visibly different | Live demo plus terminal screenshot |

## 17. Project-Specific Debugging Reference

| # | Common Problem | Suggested Debugging Steps | Difficulty |
|---|---|---|---|
| 1 | Board hard-faults immediately after `SchedulerStart()` | Almost always a TCB layout bug: confirm `sp` is genuinely the first member of `tcb_t` (Section 7), and that `InitTaskStack()` returns a pointer to the *bottom* of the fabricated frame (past the R4-R11 slots), not the top. | Hard |
| 2 | Only one task ever seems to run; the others never print or blink | `SchedulerSelectNextTask()` isn't being called, or `g_currentTask` isn't actually changing. Confirm PendSV is configured at the lowest priority (Section 9) and that `SCB->ICSR \|= SCB_ICSR_PENDSVSET_Msk` is actually being set every SysTick tick, not just once. | Medium |
| 3 | Tasks run, but in the wrong order, or seem to skip | Check `s_currentIndex`'s modulo arithmetic matches `SCHED_NUM_TASKS`, and that every task slot was actually filled by `SchedulerAddTask()` before `SchedulerStart()` was called; an uninitialized TCB in the rotation will switch to garbage. | Medium |
| 4 | Everything runs for a while, then hard-faults or behaves erratically | Likely a task stack overflow: each task's stack is a fixed size (`SCHED_STACK_WORDS` in `scheduler.c`), and a task with deep local variables or heavy `PRINTF` use can overrun it, silently corrupting the next task's stack region. Increase the per-task stack size and see if the problem goes away; if it does, that was it. | Hard |
| 5 | UART output has visibly garbled or interleaved characters | Expected, not necessarily a bug; see Section 12's honest limitation. Only chase this if it happens so often it's unreadable, in which case stagger the tasks' print periods further apart. | Easy |
| 6 | `SchedulerStartFirstTask` runs, but the very first task's registers look wrong | Confirm `InitTaskStack()`'s xPSR value is exactly `0x01000000` (the Thumb bit), and that the PC value written is the task function's actual address; a missing Thumb bit or wrong PC will fault the instant `bx r3` executes at the end of `SchedulerStartFirstTask`. | Hard |
| 7 | Assembler errors when building `pendsv_handler.S` | Confirm `.syntax unified` and `.thumb` are the first two directives in the file (this project's Cortex-M0+ target requires Thumb-only assembly), and that every register name is lowercase and matches ARMv6-M's available mnemonics; `stmdb`, and any instruction addressing R8-R11 directly in a multi-register transfer, are not available on this core (Section 9 explains why). | Medium |
| 8 | Priorities set with `NVIC_SetPriority` don't seem to have any effect | Confirm you're using `PendSV_IRQn` and `SysTick_IRQn` (both negative values) directly; `NVIC_SetPriority` handles negative IRQn correctly on this chip (Section 4), but a typo'd positive number will silently configure the wrong thing. | Medium |

## 18. Glossary

- **Exception:** the general ARM term covering both interrupts (from peripherals) and other events like PendSV and SysTick that aren't tied to a specific external device.
- **Stacking / unstacking:** the hardware's automatic push (on exception entry) and pop (on exception return) of the 8-register frame described in Section 5.
- **EXC_RETURN:** the special sentinel value the hardware loads into LR on exception entry, later used to trigger and correctly configure the automatic unstacking on return.
- **MSP / PSP:** Main Stack Pointer and Process Stack Pointer, the two independent stack pointer registers every Cortex-M core has; this project uses MSP for exception handling and PSP for task code.
- **CONTROL register:** the register whose bit 1 (SPSEL) selects which of MSP or PSP is active in Thread mode.
- **Context switch:** saving the running task's full register state and loading a different task's previously saved state, so it resumes exactly where it left off.
- **TCB (Task Control Block):** the small per-task struct holding what the scheduler needs to know about a task that isn't currently running; here, just its saved stack pointer and a name.
- **PendSV:** a software-triggered exception, specifically designed to run at the lowest priority, used as the standard ARM mechanism for performing context switches safely.
- **ARMv6-M:** the instruction set architecture implemented by both Cortex-M0 and Cortex-M0+ (this chip); notably lacks some of the more powerful multi-register instructions available on the ARMv7-M cores (M3/M4), which is why R8-R11 need the shuffle-through-R4-R7 trick in Section 9.
- **Round-robin scheduling:** a policy that gives every task an equal, fixed-order turn, with no priority levels.
- **Cooperative scheduling (and how this project uses the term loosely):** classically, a policy where tasks voluntarily yield the CPU; this project's forced, fixed-tick round-robin is technically preemptive time-slicing, but "cooperative" here is signaling the absence of priorities and blocking primitives, not voluntary yielding. See Section 10.

## 19. References

- FreeRTOS Kernel, `portable/GCC/ARM_CM0/port.c` (`github.com/FreeRTOS/FreeRTOS-Kernel`): the verified reference this project's context-switch technique is adapted from. Not linked against; consulted as the standard, proven pattern for this exact problem.
- ARMv6-M Architecture Reference Manual (ARM, free): the authoritative source for the exception model, stack frame layout, and EXC_RETURN behavior described in Sections 1 and 5.
- Cortex-M0+ Devices Generic User Guide (ARM, free): a gentler introduction to the same material, worth reading before the Architecture Reference Manual if the latter feels too dense.
- `MKL26Z4.h`, `core_cm0plus.h` (in `SDK_2_2_0_FRDM-KL26Z`): source of every register name and priority value in Section 4.
- P0's manual (`P0_Board_Orientation_and_Toolchain_Setup/`, this repository), P1's manual (`P1_Sensor_Dashboard/`, this repository): foundational concepts this manual builds on without re-explaining.

## 20. Developer Notes

- **`demo_code/01_scheduler_from_scratch/` was compiled and disassembled, not just written.** It builds cleanly (14 KB of 128 KB flash) against the real toolchain, and both hand-written assembly functions (`PendSV_Handler`, `SchedulerStartFirstTask`) were disassembled after linking and checked instruction-by-instruction against the intended design; every instruction matched. It has not been flashed to physical hardware. A project leader must bench-test it on a real board before WS4/WS5; this is the highest-stakes project in the series to get subtly wrong (a bad context switch can hard-fault silently or corrupt memory in ways that are hard to trace), so the bench test matters more here than for any prior project.
- The "cooperative" terminology note in Section 10 is worth reading in full and repeating accurately during the WS5 demo; it's a common source of confusion even in professional embedded contexts, and getting it right is a good sign a group actually understands the mechanism rather than having memorized the project title.
- If a future capstone or advanced project wants priority-based scheduling instead of plain round-robin, the natural extension point is `SchedulerSelectNextTask()` (Section 10); the context-switch mechanics in `pendsv_handler.S` would not need to change at all, only the policy that decides which TCB `g_currentTask` points at next. Worth mentioning to members as a preview of how a real RTOS scheduler is layered.

---
*IEEE Texas State University Student Branch. Connect. Build. Inspire.*
