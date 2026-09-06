# P2: Setup and Walkthrough

*Part of the P2 manual split. See `P2_1_Start_Here.md` for the full file list and how to use this manual.*

---

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

---

**Next:** `P2_4_Reference.md` for code structure, sample output, debugging, and the glossary.

---
*IEEE Texas State University Student Branch. Connect. Build. Inspire.*
