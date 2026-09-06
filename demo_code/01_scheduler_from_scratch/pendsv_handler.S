/*
 * P2 context switcher, written in ARM assembly, as the project requires.
 *
 * This is adapted from the FreeRTOS ARM_CM0 port's xPortPendSVHandler and
 * vPortStartFirstTask (github.com/FreeRTOS/FreeRTOS-Kernel,
 * portable/GCC/ARM_CM0/port.c), the standard reference for this exact
 * problem on this exact processor family. The register save/restore
 * sequence, the R4-R11 high-register shuffle, and the first-task launch
 * are the same proven technique; the variable and function names are this
 * project's own. See the manual, Section 9, for a line-by-line walkthrough
 * of why each instruction is here.
 *
 * Cortex-M0+ implements the ARMv6-M instruction set, the same baseline as
 * Cortex-M0. Every instruction below is valid on this chip; you will not
 * find LDM/STM-with-high-registers or single-instruction multi-register
 * ARMv7-M tricks here, because ARMv6-M (unlike M3/M4) doesn't have them.
 * That restriction is exactly why R8-R11 have to be shuffled through
 * R4-R7 in pairs of "mov" instructions instead of being stored directly.
 */

    .syntax unified
    .thumb

    .section .text.PendSV_Handler, "ax", %progbits
    .thumb_func
    .global PendSV_Handler
PendSV_Handler:
    mrs   r0, psp               /* r0 = the outgoing task's stack pointer */

    ldr   r3, =g_currentTask    /* r3 = address of the g_currentTask pointer variable */
    ldr   r2, [r3]              /* r2 = g_currentTask (pointer to the outgoing task's TCB) */

    subs  r0, r0, #32           /* make room for R4-R11: 8 registers, 32 bytes */
    str   r0, [r2]              /* TCB->sp = r0 (sp is the TCB's first member, offset 0) */

    stmia r0!, {r4-r7}          /* save the low half of R4-R11 (r0! auto-advances) */
    mov   r4, r8                /* ARMv6-M can't STM high registers directly, */
    mov   r5, r9                /* so move R8-R11 down into R4-R7 first, */
    mov   r6, r10                /* then store those. */
    mov   r7, r11
    stmia r0!, {r4-r7}          /* save the (shuffled) high half of R4-R11 */

    push  {r3, lr}              /* keep &g_currentTask and this exception's LR safe across the call below */
    cpsid i                     /* no interrupts while we pick the next task */
    bl    SchedulerSelectNextTask
    cpsie i
    pop   {r2, r3}               /* r2 = &g_currentTask again, r3 = this exception's LR (the EXC_RETURN value) */

    ldr   r1, [r2]               /* r1 = g_currentTask (now the INCOMING task, SchedulerSelectNextTask updated it) */
    ldr   r0, [r1]               /* r0 = incoming task's saved stack pointer */

    adds  r0, r0, #16            /* skip past R4-R7 to reach the shuffled R8-R11 half */
    ldmia r0!, {r4-r7}           /* load the shuffled R8-R11 values back into R4-R7... */
    mov   r8, r4                 /* ...then move them into the real R8-R11 */
    mov   r9, r5
    mov   r10, r6
    mov   r11, r7

    msr   psp, r0                /* the incoming task's new top of stack, past the R4-R11 block */

    subs  r0, r0, #32            /* back up to the start of the R4-R11 block */
    ldmia r0!, {r4-r7}           /* load the real R4-R7 values */

    bx    r3                     /* return via the saved EXC_RETURN: pops R0-R3,R12,LR,PC,xPSR from PSP automatically */

    .ltorg
    .size PendSV_Handler, . - PendSV_Handler

/*
 * Launches task 0 for the very first time. Ordinary tasks are resumed by
 * PendSV_Handler above; this function exists only because the very first
 * task has no "previous task" context to switch away from, so it needs
 * its own, simpler unstacking sequence. Called once, from SchedulerStart()
 * in scheduler.c; never returns.
 */
    .section .text.SchedulerStartFirstTask, "ax", %progbits
    .thumb_func
    .global SchedulerStartFirstTask
SchedulerStartFirstTask:
    ldr   r2, =g_currentTask
    ldr   r3, [r2]                /* r3 = g_currentTask (task 0's TCB) */
    ldr   r0, [r3]                /* r0 = task 0's saved stack pointer */
    adds  r0, r0, #32             /* skip the R4-R11 block: task 0 has no real register values yet */
    msr   psp, r0                 /* PSP now points at the fabricated R0-R3,R12,LR,PC,xPSR frame */

    movs  r0, #2                  /* CONTROL = 0b10: use PSP (not MSP) once back in Thread mode */
    msr   control, r0
    isb                           /* required after writing CONTROL, before the next instruction relies on it */

    pop   {r0-r5}                 /* unstack the fabricated R0,R1,R2,R3,R12,LR (LR lands in r5) */
    mov   lr, r5
    pop   {r3}                    /* r3 = the fabricated PC: task 0's entry point */
    pop   {r2}                    /* discard the fabricated xPSR, already correct in hardware */

    cpsie i                       /* task 0 has a full context now; interrupts can resume */
    bx    r3                      /* jump straight into task 0's function; this never returns here */

    .ltorg
    .size SchedulerStartFirstTask, . - SchedulerStartFirstTask
