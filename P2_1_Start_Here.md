# TXST IEEE Student Branch: FRDM-KL26Z Project Series
## Project Manual, P2: Custom Cooperative Task Scheduler

**Document status:** DRAFT v0.1, for project-leader review and bench testing before member use
**Track:** Embedded Systems | **Difficulty:** Advanced | **Sessions:** 2 (WS4, Oct 1 and WS5, Oct 8, 2026), with pre-reading between sessions
**Applies to:** All 8 groups (24 members)
**Companion documents:** P0's manual (`P0_Board_Orientation_and_Toolchain_Setup/`, start at `P0_1_Start_Here.md`), P1's manual (`P1_Sensor_Dashboard/`, start at `P1_1_Start_Here.md`; read both first), *TXST IEEE FRDM-KL26Z Project Specification*
**Demo code:** see `demo_code/` in this project's folder

---

## This Manual Is Split Into 4 Files

Long single files invite procrastination. Read only what you need, when you need it:

1. **`P2_1_Start_Here.md`** (this file) — how to use this manual, why P2 exists, purpose, prerequisites.
2. **`P2_2_Concepts_and_Hardware.md`** — background theory (exceptions, MSP/PSP, EXC_RETURN, the TCB) and the register/priority reference. Read once if any term below is new to you.
3. **`P2_3_Setup_and_Walkthrough.md`** — the actual hands-on steps for both sessions, including the whiteboard exercise. **This is the file you follow during the sessions.**
4. **`P2_4_Reference.md`** — code structure explanation, sample output, session plan, milestones, debugging table, glossary, references, developer notes. Look things up here when stuck.

Section numbers (0-20) are kept consistent across all 4 files, so "see Section 9" always means the same section no matter which file you're in.

---

## How to Use This Manual

Same rule as P0 and P1, with one addition specific to this project: this is a reference, not required reading, but for P2 specifically, do the Session 1 whiteboard exercise (Section 6) with an actual pen and paper before touching the keyboard. This project is genuinely hard, and the single biggest predictor of whether it clicks is whether you can draw the stack frame from memory before you try to write code that manipulates it. Everything after that point in this manual assumes you've done that.

Project leaders: read this end to end and bench-test `demo_code/01_scheduler_from_scratch/` on a real board before WS4 and WS5. This is the hardest project in the series to debug blind; a leader who has already seen it run correctly (or seen exactly how it fails) is worth far more to a stuck group here than in any prior project.

**A note on accuracy:** the trickiest part of this project, the PendSV handler's register save and restore, is adapted from the ARM_CM0 port shipped in the FreeRTOS kernel (`github.com/FreeRTOS/FreeRTOS-Kernel`, `portable/GCC/ARM_CM0/port.c`), which is the standard, widely-deployed reference for solving exactly this problem on exactly this processor family. This project does not link against FreeRTOS, include any of its files, or depend on it in any way; the assembly here was written from scratch, using that proven implementation as the verified pattern to check register-by-register logic against, not copied wholesale. Every NVIC priority value, register name, and instruction in this manual was either pulled from the installed SDK/CMSIS headers or confirmed by compiling and disassembling the code (Section 20 has the details). Nothing here is hand-derived Cortex-M assembly that nobody has checked.

---

## 0. Why This Session Exists

Every project before this one used peripherals that came with a driver: `GPIO_PinInit`, `I2C_MasterTransferBlocking`, `TSI_Calibrate`. Someone at NXP wrote those. P2 is the first project where you write the thing that would normally come from a vendor, or from an RTOS like FreeRTOS. By the end, you will have built, from the bare processor exception model up, the same fundamental mechanism that every real-time operating system uses to run more than one task on one CPU. This is not a toy version of the real thing; the technique in `pendsv_handler.S` is architecturally the same technique FreeRTOS itself uses on this exact processor family, just with the generality (priorities, blocking, semaphores) stripped out. P3 needs you comfortable with interrupts and hardware timing (SysTick here, DMA there) at this same level of detail; P2 is where "I called a function and it configured a register for me" stops being how you think about the chip.

## 2. Purpose

By the end of these two sessions, every member has: a hand-drawn understanding of the Cortex-M0+ exception stack frame; a TCB struct and a SysTick handler firing at 1 ms; a PendSV-based context switcher written in real ARM assembly, not copied without understanding; and three tasks running concurrently on that scheduler, provably interleaving (not just running one after another) via UART output. This is the deepest dive into "what is actually happening on this chip" in the entire series.

## 3. Prerequisites

P1 complete. You need to already be comfortable with the NVIC, interrupt priorities, and the superloop-plus-flag pattern before starting; this manual does not re-explain those (see `P1_2_Concepts_and_Hardware.md` and `P1_3_Setup_and_Walkthrough.md`, Sections 1 and 6 to 8, if they're shaky). Skim the ARM Cortex-M0+ Devices Generic User Guide or the ARMv6-M Architecture Reference Manual (both free from ARM) before WS4 if you want more depth than this manual provides on the exception model; neither is required, but both are the primary source this manual's Section 5 is grounded in.

---

**Next:** `P2_2_Concepts_and_Hardware.md` for the concepts and register reference, or skip straight to `P2_3_Setup_and_Walkthrough.md` if you're already comfortable with the exception model.

---
*IEEE Texas State University Student Branch. Connect. Build. Inspire.*
