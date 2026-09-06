# TXST IEEE Student Branch: FRDM-KL26Z Project Series
## Project Manual, P2: Custom Cooperative Task Scheduler

**Document status:** DRAFT v0.1, for project-leader review and bench testing before member use
**Track:** Embedded Systems | **Difficulty:** Advanced | **Sessions:** 2 (WS4, Oct 1 and WS5, Oct 8, 2026), with pre-reading between sessions
**Applies to:** All 8 groups (24 members)
**Companion documents:** P0's manual (`P0_Board_Orientation_and_Toolchain_Setup/`, start at `P0_1_Start_Here.md`), `P1_Project_Manual.md` (read both first), *TXST IEEE FRDM-KL26Z Project Specification*
**Demo code:** see `demo_code/` in this project's folder

---

## How to Use This Manual

Same rule as P0 and P1, with one addition specific to this project: this is a reference, not required reading, but for P2 specifically, do the Session 1 whiteboard exercise (Section 6) with an actual pen and paper before touching the keyboard. This project is genuinely hard, and the single biggest predictor of whether it clicks is whether you can draw the stack frame from memory before you try to write code that manipulates it. Everything after that point in this manual assumes you've done that.

Project leaders: read this end to end and bench-test `demo_code/01_scheduler_from_scratch/` on a real board before WS4 and WS5. This is the hardest project in the series to debug blind; a leader who has already seen it run correctly (or seen exactly how it fails) is worth far more to a stuck group here than in any prior project.

**A note on accuracy:** the trickiest part of this project, the PendSV handler's register save and restore, is adapted from the ARM_CM0 port shipped in the FreeRTOS kernel (`github.com/FreeRTOS/FreeRTOS-Kernel`, `portable/GCC/ARM_CM0/port.c`), which is the standard, widely-deployed reference for solving exactly this problem on exactly this processor family. This project does not link against FreeRTOS, include any of its files, or depend on it in any way; the assembly here was written from scratch, using that proven implementation as the verified pattern to check register-by-register logic against, not copied wholesale. Every NVIC priority value, register name, and instruction in this manual was either pulled from the installed SDK/CMSIS headers or confirmed by compiling and disassembling the code (Section 20 has the details). Nothing here is hand-derived Cortex-M assembly that nobody has checked.

---

## 0. Why This Session Exists

Every project before this one used peripherals that came with a driver: `GPIO_PinInit`, `I2C_MasterTransferBlocking`, `TSI_Calibrate`. Someone at NXP wrote those. P2 is the first project where you write the thing that would normally come from a vendor, or from an RTOS like FreeRTOS. By the end, you will have built, from the bare processor exception model up, the same fundamental mechanism that every real-time operating system uses to run more than one task on one CPU. This is not a toy version of the real thing; the technique in `pendsv_handler.S` is architecturally the same technique FreeRTOS itself uses on this exact processor family, just with the generality (priorities, blocking, semaphores) stripped out. P3 needs you comfortable with interrupts and hardware timing (SysTick here, DMA there) at this same level of detail; P2 is where "I called a function and it configured a register for me" stops being how you think about the chip.

## 1. New Concepts You Need Before Starting

This assumes P0 and P1's vocabulary (microcontroller, GPIO, UART, interrupts, the NVIC, the superloop-plus-flag pattern). Everything below is new to P2.

**What actually happens during an exception, at the hardware level.** P1 told you that an interrupt "pauses whatever the CPU is doing and jumps to a handler." Here is the mechanism. When any exception fires (an interrupt, or a software-triggered one like PendSV), the Cortex-M0+ hardware automatically does two things before your handler code runs a single instruction: it pushes 8 specific registers onto the currently active stack (this is called "stacking"), and it loads a special value into the link register (LR) called `EXC_RETURN` that records how to return from this exception later. Both of these happen in hardware, automatically, with no code of yours involved. Section 5 covers exactly what gets stacked and in what order.

**MSP and PSP: two stack pointers, not one.** Every Cortex-M processor actually has two separate stack pointer registers, though only one is active at a time. The Main Stack Pointer (MSP) is what's active from reset and is what all your code has been using through P0 and P1 without you needing to know it existed. The Process Stack Pointer (PSP) is a second, independent stack that software can switch to. P2's entire design rests on this: each task gets its own private stack, and PSP is what gets pointed at whichever task is currently running, while MSP stays reserved for exception handling itself. This is exactly how FreeRTOS and every other Cortex-M RTOS separates "the OS's own stack" from "each task's stack."

**The CONTROL register.** This is what decides whether the processor is currently using MSP or PSP in Thread mode (ordinary, non-exception code). Bit 1 of CONTROL (called SPSEL) is 0 for MSP, 1 for PSP. Section 11 has you write `movs r0, #2` then `msr control, r0` exactly once, to make this switch permanently for the rest of the program's life.

**EXC_RETURN.** When an exception fires, the hardware doesn't just jump to your handler; it loads LR with a special sentinel value (on this chip, one of a small handful of values, all starting with `0xFFFFFFF...`) that isn't a real code address at all. It's a signal: when your handler eventually does `bx lr` (or, as in this project, `bx r3` after moving that value into r3), the processor recognizes the sentinel pattern and knows this is a request to return from an exception, not a normal function return, and it automatically unstacks the same 8 registers it stacked on entry. The specific bit pattern also tells the processor which stack (MSP or PSP) to unstack from and return to. This project relies on this mechanism directly, not through a library function; Section 9 shows exactly where.

**Task Control Block (TCB).** A small struct, one per task, that holds everything the scheduler needs to know about a task it isn't currently running: at minimum, where that task's stack pointer was left off. Section 7 covers this project's TCB and one non-negotiable rule about its layout.

**Context switch.** The act of saving the currently running task's CPU state (all its registers) somewhere safe, then loading a different task's previously saved state back into the CPU, so execution resumes exactly where that other task left off, with no way for either task to tell it ever stopped running. This is the single mechanism this entire project builds.

## 2. Purpose

By the end of these two sessions, every member has: a hand-drawn understanding of the Cortex-M0+ exception stack frame; a TCB struct and a SysTick handler firing at 1 ms; a PendSV-based context switcher written in real ARM assembly, not copied without understanding; and three tasks running concurrently on that scheduler, provably interleaving (not just running one after another) via UART output. This is the deepest dive into "what is actually happening on this chip" in the entire series.

## 3. Prerequisites

P1 complete. You need to already be comfortable with the NVIC, interrupt priorities, and the superloop-plus-flag pattern before starting; this manual does not re-explain those (see `P1_Project_Manual.md`, Sections 1 and 6 to 8, if they're shaky). Skim the ARM Cortex-M0+ Devices Generic User Guide or the ARMv6-M Architecture Reference Manual (both free from ARM) before WS4 if you want more depth than this manual provides on the exception model; neither is required, but both are the primary source this manual's Section 5 is grounded in.

## 4. Register and Priority Reference

| Fact | Value | Verified against |
|---|---|---|
| NVIC priority bits implemented on this chip | 2 bits (4 levels, 0 to 3, 0 = most urgent) | `CMSIS/Include/core_cm0plus.h`, `MKL26Z4.h` (`__NVIC_PRIO_BITS`), also used in P1 |
| PendSV's IRQ number | `PendSV_IRQn = -2` (a negative number: system exceptions, not device interrupts, are numbered this way) | `MKL26Z4.h` |
| SysTick's IRQ number | `SysTick_IRQn = -1` | `MKL26Z4.h` |
| `NVIC_SetPriority()` works on negative IRQn values | Yes, routes to `SCB->SHP[]` instead of `NVIC->IP[]` automatically | `core_cm0plus.h`, confirmed by reading the function body |
| PendSV request bit | `SCB->ICSR`, bit 28 (`SCB_ICSR_PENDSVSET_Msk`) | `core_cm0plus.h` |
| CONTROL register, bit 1 (SPSEL) | 0 = MSP active, 1 = PSP active | ARMv6-M Architecture Reference Manual; used directly in `pendsv_handler.S` |
| Exception stack frame size | 8 words (32 bytes): R0, R1, R2, R3, R12, LR, PC, xPSR, low to high address | ARMv6-M Architecture Reference Manual; confirmed by disassembling `demo_code/01_scheduler_from_scratch` (Section 20) |
| Initial xPSR value for a fake stack frame | `0x01000000` (bit 24, the Thumb bit, must be set; this chip has no ARM instruction mode) | ARMv6-M Architecture Reference Manual |

## 5. The ARM Exception Stack Frame

This is the whiteboard exercise. Before writing any code, draw this by hand.

When any exception fires, the hardware pushes exactly these 8 registers onto the currently active stack (MSP if you're in Thread mode using MSP, PSP if you're using PSP, whichever was active a moment before), in this order, from the lowest address to the highest:

```
Higher address
+----------+  <- stack pointer before the exception (top of stack)
|   xPSR   |
|    PC    |
|    LR    |
|   R12    |
|    R3    |
|    R2    |
|    R1    |
|    R0    |  <- stack pointer after stacking (this is what the handler sees)
+----------+
Lower address
```

**Draw this, then trace through it by hand:** if a task is running normally and a SysTick interrupt fires, what does the stack look like the instant `SysTick_Handler` starts executing? Which registers does the hardware save for you? Which ones did it not touch? (Answer: R4 through R11, plus the two stack pointers themselves, are not part of this automatic frame. Software has to handle those if it needs them, which is exactly what a context switch needs to do, and exactly why Section 9's handler manually saves R4-R11.)

**Why only 8 registers, not all 13 general-purpose ones?** ARM's design assumes a short, simple interrupt handler (written in C, following the normal calling convention) can freely use R0-R3 and R12 as scratch registers without needing to preserve their previous values, the same way any normal function call is allowed to clobber them. So the hardware only needs to protect the registers a C function isn't already responsible for saving (R4-R11, by calling convention, are supposed to be preserved by any function that uses them, meaning ordinary interrupt handlers don't disturb them). A context switch is not an ordinary interrupt handler; it needs to preserve everything, because it's not returning to the same task, so it has to manually handle the R4-R11 half the hardware didn't cover.

**Why the Thumb bit in xPSR matters.** Bit 24 of xPSR records whether the processor is in Thumb state. Cortex-M0+ only ever executes Thumb instructions, there is no ARM (32-bit non-Thumb) mode on this chip at all, but the bit still has to be set correctly in a fabricated frame, or the processor will fault the instant it tries to return into it. This is why `SCHED_INITIAL_XPSR` in the demo code is `0x01000000`, not `0x00000000`.

## Session 1 (WS4, Oct 1): Whiteboard, SysTick, and the TCB

## 6. The Whiteboard Exercise (30 Minutes, Do This First)

Before opening an IDE:

1. Draw the 8-word exception stack frame from Section 5, by hand, unaided.
2. Trace what happens, step by step, when a SysTick interrupt fires while `main()` is running normally on MSP: what does hardware do automatically, in what order, and what is true about the stack and LR the instant your `SysTick_Handler` function's first line executes?
3. Now trace what has to be true for a context switch to work: if you want to resume a *different* task than the one that just got interrupted, what information do you need to have saved from the outgoing task, and where do you get the incoming task's saved information from?

If your group can answer question 3 without looking anything up, you're ready for Sections 7 to 9. If not, re-read Section 5 and try again; do not move on by copying code you can't explain, the whole point of this project is being able to explain it.

## 7. The Task Control Block

`demo_code/01_scheduler_from_scratch/scheduler.h` defines:

```c
typedef struct
{
    uint32_t *sp;      /* saved stack pointer; must be offset 0 */
    const char *name;
} tcb_t;
```

**The one rule that matters more than anything else in this struct: `sp` must be the first member.** The context switcher reaches a task's saved stack pointer with nothing more than `ldr r0, [r1]`, "read the first word of whatever this pointer points at." It does not know about C struct layout, field names, or `offsetof`; it only knows "the thing I need is at offset zero." If you add a field before `sp`, or reorder the struct, the assembly will read the wrong memory, and depending on what garbage it finds there, the board will either hard fault immediately or silently execute from a nonsense address, which is a far worse debugging experience. This is a real, common mistake; it's listed first in Section 19's debugging table for a reason.

## 8. SysTick: The Timer That's Always There

Every Cortex-M processor, regardless of vendor, includes a SysTick timer as part of the core itself, not as a vendor-specific peripheral. That's why it's the standard choice for a scheduler's time base: code written against SysTick is portable across any Cortex-M chip, unlike code written against, say, this chip's specific PIT peripheral.

Configure it with the CMSIS helper `SysTick_Config(SystemCoreClock / 1000U)`, which sets up a 1 ms tick (the session's deliverable) and enables the SysTick interrupt. `demo_code/01_scheduler_from_scratch/scheduler.c`'s `SysTick_Handler()` does two things and nothing else: increments a global tick counter, and requests a context switch by setting the PendSV-pending bit (`SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk`). It does not call the scheduler logic directly; Section 9 explains why that distinction matters.

**Session 1 deliverable check:** SysTick firing at 1 ms (confirm by printing the tick count over UART periodically, the same superloop-plus-flag pattern from P1), and the TCB struct from Section 7 compiling cleanly. No context switch yet; that's Session 2.

## Session 2 (WS5, Oct 8): PendSV and the 3-Task Demo

## 9. Writing the Context Switcher in Assembly

This is `demo_code/01_scheduler_from_scratch/pendsv_handler.S`. Read it next to this section; each step below corresponds to a labeled block in that file.

**Why PendSV, and not just doing the switch inside `SysTick_Handler`?** Two reasons, both real engineering constraints, not arbitrary style choices. First, PendSV is specifically designed to be configured at the lowest possible interrupt priority, meaning it only actually runs once every other, more urgent interrupt has finished; if you did the context switch logic directly inside a higher-priority handler like SysTick, a context switch could begin while some other interrupt was still mid-flight, corrupting state. Second, the actual register-saving trick in this handler only works correctly when it runs as the outermost, lowest-priority exception on the stack; nesting it inside another handler breaks the assumption that "the stack currently holds exactly one exception frame for the interrupted task." Setting `NVIC_SetPriority(PendSV_IRQn, 3)` (the lowest of this chip's 4 levels) in `SchedulerStart()` is not optional tuning, it's required for correctness.

**Step 1: save the outgoing task's context.**

```asm
mrs   r0, psp               /* r0 = the outgoing task's stack pointer */
ldr   r3, =g_currentTask    /* r3 = address of the g_currentTask pointer variable */
ldr   r2, [r3]              /* r2 = g_currentTask (pointer to the outgoing task's TCB) */
subs  r0, r0, #32           /* make room for R4-R11: 8 registers, 32 bytes */
str   r0, [r2]              /* TCB->sp = r0 */
```

The hardware already stacked R0-R3, R12, LR, PC, and xPSR automatically the instant PendSV fired (Section 5). This handler's job is only the other half: R4 through R11. `subs r0, r0, #32` reserves 32 bytes below where PSP currently points (below the hardware's frame) for those 8 registers, and that lower address becomes the task's new saved stack pointer, stored into its TCB.

**Step 2: actually save R4-R11, working around an instruction set limit.**

```asm
stmia r0!, {r4-r7}
mov   r4, r8
mov   r5, r9
mov   r6, r10
mov   r7, r11
stmia r0!, {r4-r7}
```

Cortex-M0+ implements the ARMv6-M instruction set, the same baseline as the original Cortex-M0. Unlike the fancier Cortex-M3/M4 (ARMv7-M), ARMv6-M's `stmia`/`ldmia` (store/load-multiple) instructions can only address the low registers, R0 through R7. There is no single instruction that stores R8-R11 directly. So the code does it in two passes: store R4-R7 with `stmia`, then `mov` each of R8-R11 down into R4-R7 (which is allowed; `mov` between any two registers works regardless of "low" or "high"), then `stmia` again to store what are now copies of the original R8-R11 values. This is not a workaround this project invented; it's the standard technique every Cortex-M0 context switcher uses, including FreeRTOS's, for exactly this reason.

**Step 3: pick the next task, safely.**

```asm
push  {r3, lr}
cpsid i
bl    SchedulerSelectNextTask
cpsie i
pop   {r2, r3}
```

`SchedulerSelectNextTask()` (Section 10) is ordinary C, not assembly; calling it with `bl` is a normal function call. But `bl` overwrites LR with its own return address, and this handler still needs the *original* LR (the EXC_RETURN sentinel from Section 1) to actually complete the exception return later. So both the address of `g_currentTask` (in r3) and the real LR are pushed onto the current stack (MSP; this handler itself is still running in Handler mode, on MSP, throughout) before the call, and popped back immediately after, with `lr` landing back in `r3`. Interrupts are masked (`cpsid i` / `cpsie i`) around the call so nothing can fire mid-switch and see an inconsistent `g_currentTask`.

**Step 4: restore the incoming task's context**, the exact mirror of Step 2, reading instead of writing:

```asm
ldr   r1, [r2]
ldr   r0, [r1]
adds  r0, r0, #16
ldmia r0!, {r4-r7}
mov   r8, r4
mov   r9, r5
mov   r10, r6
mov   r11, r7
msr   psp, r0
subs  r0, r0, #32
ldmia r0!, {r4-r7}
bx    r3
```

`g_currentTask` now points at whatever `SchedulerSelectNextTask()` picked; `ldr r0, [r1]` reads that task's saved stack pointer, exactly as it was left the last time this task was switched out (or its fabricated initial value, the first time it runs; Section 11). `adds r0, r0, #16` skips past where R4-R7 will be to reach the R8-R11 half first (this project's save order and restore order are deliberately symmetric), unshuffles them back into the real R8-R11, sets PSP to the top of the hardware's half of the frame, then backs up and reloads the real R4-R7. Finally, `bx r3` returns from the exception using the EXC_RETURN value saved in Step 3, which is what makes the hardware automatically unstack the remaining R0-R3, R12, LR, PC, and xPSR, exactly as Section 1 described, resuming the incoming task exactly where it left off, or starting it for the first time if this is the fabricated frame from Section 7.

## 10. Round-Robin Scheduling, and Why This Project Calls It "Cooperative"

`SchedulerSelectNextTask()` in `scheduler.c` is the entire scheduling policy:

```c
void SchedulerSelectNextTask(void)
{
    s_currentIndex = (s_currentIndex + 1U) % SCHED_NUM_TASKS;
    g_currentTask = &s_tasks[s_currentIndex];
}
```

That's it. No priorities, no ready/blocked lists, just "whoever's next in the fixed rotation." This is deliberately the simplest fair scheduling policy that exists: every task gets an equal, guaranteed turn, in the same order, forever.

**A terminology note worth understanding precisely, because it's a common point of confusion.** This project is titled a "cooperative" scheduler, but the mechanism described, a fixed-rate SysTick tick forcing a switch every single tick, is technically a form of *preemptive* time-sliced round-robin: no task ever explicitly agrees to give up the CPU, it's simply cut off every 1 ms whether it's ready to stop or not. A strictly "cooperative" scheduler, in the classic operating-systems-textbook sense, would instead have each task voluntarily call something like `Yield()` when it's done with its turn, and a badly behaved task that never yields could starve every other task forever. This project's scheduler cannot be starved that way, because the switch is forced by hardware on a timer, independent of what any task is doing. What *is* true, and what "cooperative" is doing real work to signal here, is that there's no priority-based preemption (a "more important" task cannot jump the queue), and there are no blocking primitives like semaphores or mutexes protecting shared resources between tasks, both of which are hallmarks of a fuller RTOS and genuinely out of scope for this project. If you're asked to explain the scheduling policy during the WS5 demo, say precisely what it does (fixed-rate, forced, round-robin, no priorities) rather than leaning on the word "cooperative" as if it settles the question.

## 11. Launching the First Task

Ordinary context switches (Section 9) always have a "previous task" to save. The very first task the scheduler ever runs doesn't; there's nothing to switch away from, only something to switch into. `SchedulerStartFirstTask` in `pendsv_handler.S` handles this one-time case:

```asm
ldr   r2, =g_currentTask
ldr   r3, [r2]
ldr   r0, [r3]
adds  r0, r0, #32             /* skip the R4-R11 block: task 0 has no real register values yet */
msr   psp, r0

movs  r0, #2                  /* CONTROL = 0b10: use PSP, not MSP, once back in Thread mode */
msr   control, r0
isb

pop   {r0-r5}
mov   lr, r5
pop   {r3}
pop   {r2}
cpsie i
bx    r3
```

This is called once, directly, from `SchedulerStart()` in `scheduler.c`, not through an exception. It reads task 0's fabricated stack pointer (Section 7's `InitTaskStack`, which built a fake frame exactly matching what a real context switch would have produced), skips past the R4-R11 slots since a brand-new task has no real prior register values to restore there, and points PSP at the fabricated hardware-half frame. The `movs r0, #2` / `msr control, r0` / `isb` sequence is the one-time, permanent switch from MSP to PSP described in Section 1; every subsequent Thread-mode instruction in the program, meaning every instruction any task ever runs, now executes on PSP. Then it manually pops what would normally be auto-unstacked by hardware (because there was no real exception here to trigger automatic unstacking), and `bx r3` jumps straight into task 0's entry point. This function never returns; `main()`'s own stack (still sitting on MSP, abandoned but harmless) is never used again.

## 12. Building the 3-Task Demo

`demo_code/01_scheduler_from_scratch/main.c` registers three tasks and starts the scheduler:

```c
SchedulerAddTask(0U, "LED", LedBlinkTask);
SchedulerAddTask(1U, "UART-Counter", UartCounterTask);
SchedulerAddTask(2U, "TouchSlider", TouchSliderTask);
SchedulerStart(); /* never returns */
```

Each task is an infinite loop that never returns and never blocks for long, timing its own periodic work against the shared, read-only `g_tickCount` (safe to read from any task; nothing ever writes it except `SysTick_Handler`):

- **LED task:** toggles the red LED every 250 ticks and the blue LED every 80 ticks, from within one task, so two visibly different blink rates prove the scheduler is giving this task CPU time regularly, on schedule.
- **UART-Counter task:** prints its name, the current tick count, and a local counter every 300 ticks.
- **TouchSlider task:** samples one TSI electrode every 400 ticks (same technique as P1, Section 13 there) and prints how far above baseline it reads.

**One honest limitation, worth understanding rather than being surprised by:** all three tasks call `PRINTF`, and nothing in this project protects the shared UART from being written by two tasks whose print-due ticks happen to land close together. Since a task can be preempted mid-`PRINTF` (the switch is forced every 1 ms regardless of what a task is doing, including mid-write), it is possible, if rare with these staggered periods, to see two tasks' output interleaved character-by-character in the terminal. This is not a bug to chase down; it's a real, expected consequence of having no synchronization primitives yet, and it's exactly the kind of problem mutexes and semaphores exist to solve in a fuller RTOS, a preview of a concept this project doesn't build.

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
- P0's manual (`P0_Board_Orientation_and_Toolchain_Setup/`, this repository), `P1_Project_Manual.md`: foundational concepts this manual builds on without re-explaining.

## 20. Developer Notes

- **`demo_code/01_scheduler_from_scratch/` was compiled and disassembled, not just written.** It builds cleanly (14 KB of 128 KB flash) against the real toolchain, and both hand-written assembly functions (`PendSV_Handler`, `SchedulerStartFirstTask`) were disassembled after linking and checked instruction-by-instruction against the intended design; every instruction matched. It has not been flashed to physical hardware. A project leader must bench-test it on a real board before WS4/WS5; this is the highest-stakes project in the series to get subtly wrong (a bad context switch can hard-fault silently or corrupt memory in ways that are hard to trace), so the bench test matters more here than for any prior project.
- The "cooperative" terminology note in Section 10 is worth reading in full and repeating accurately during the WS5 demo; it's a common source of confusion even in professional embedded contexts, and getting it right is a good sign a group actually understands the mechanism rather than having memorized the project title.
- If a future capstone or advanced project wants priority-based scheduling instead of plain round-robin, the natural extension point is `SchedulerSelectNextTask()` (Section 10); the context-switch mechanics in `pendsv_handler.S` would not need to change at all, only the policy that decides which TCB `g_currentTask` points at next. Worth mentioning to members as a preview of how a real RTOS scheduler is layered.

---
*IEEE Texas State University Student Branch. Connect. Build. Inspire.*
