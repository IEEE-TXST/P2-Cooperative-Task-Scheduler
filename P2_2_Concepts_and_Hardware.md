# P2: Concepts and Hardware

*Part of the P2 manual split. See `P2_1_Start_Here.md` for the full file list and how to use this manual.*

---

## 1. New Concepts You Need Before Starting

This assumes P0 and P1's vocabulary (microcontroller, GPIO, UART, interrupts, the NVIC, the superloop-plus-flag pattern). Everything below is new to P2.

**What actually happens during an exception, at the hardware level.** P1 told you that an interrupt "pauses whatever the CPU is doing and jumps to a handler." Here is the mechanism. When any exception fires (an interrupt, or a software-triggered one like PendSV), the Cortex-M0+ hardware automatically does two things before your handler code runs a single instruction: it pushes 8 specific registers onto the currently active stack (this is called "stacking"), and it loads a special value into the link register (LR) called `EXC_RETURN` that records how to return from this exception later. Both of these happen in hardware, automatically, with no code of yours involved. Section 5 covers exactly what gets stacked and in what order.

**MSP and PSP: two stack pointers, not one.** Every Cortex-M processor actually has two separate stack pointer registers, though only one is active at a time. The Main Stack Pointer (MSP) is what's active from reset and is what all your code has been using through P0 and P1 without you needing to know it existed. The Process Stack Pointer (PSP) is a second, independent stack that software can switch to. P2's entire design rests on this: each task gets its own private stack, and PSP is what gets pointed at whichever task is currently running, while MSP stays reserved for exception handling itself. This is exactly how FreeRTOS and every other Cortex-M RTOS separates "the OS's own stack" from "each task's stack."

**The CONTROL register.** This is what decides whether the processor is currently using MSP or PSP in Thread mode (ordinary, non-exception code). Bit 1 of CONTROL (called SPSEL) is 0 for MSP, 1 for PSP. Section 11 has you write `movs r0, #2` then `msr control, r0` exactly once, to make this switch permanently for the rest of the program's life.

**EXC_RETURN.** When an exception fires, the hardware doesn't just jump to your handler; it loads LR with a special sentinel value (on this chip, one of a small handful of values, all starting with `0xFFFFFFF...`) that isn't a real code address at all. It's a signal: when your handler eventually does `bx lr` (or, as in this project, `bx r3` after moving that value into r3), the processor recognizes the sentinel pattern and knows this is a request to return from an exception, not a normal function return, and it automatically unstacks the same 8 registers it stacked on entry. The specific bit pattern also tells the processor which stack (MSP or PSP) to unstack from and return to. This project relies on this mechanism directly, not through a library function; Section 9 shows exactly where.

**Task Control Block (TCB).** A small struct, one per task, that holds everything the scheduler needs to know about a task it isn't currently running: at minimum, where that task's stack pointer was left off. Section 7 covers this project's TCB and one non-negotiable rule about its layout.

**Context switch.** The act of saving the currently running task's CPU state (all its registers) somewhere safe, then loading a different task's previously saved state back into the CPU, so execution resumes exactly where that other task left off, with no way for either task to tell it ever stopped running. This is the single mechanism this entire project builds.

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

---

**Next:** `P2_3_Setup_and_Walkthrough.md` for the hands-on session steps, starting with the whiteboard exercise.

---
*IEEE Texas State University Student Branch. Connect. Build. Inspire.*
