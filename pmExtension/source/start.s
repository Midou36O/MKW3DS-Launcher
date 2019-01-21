.arm

.section .text.start
.align 4

.global _start
_start:
    stmfd sp!, {r0-r12, lr}
    bl start
    ldmfd sp!, {r0-r12, lr}
_svcRun:
    stmfd   sp!, {r4, r5}
    ldr     r5, [r1, #0x10]
    ldr     r4, [r1, #0xC]
    ldr     r3, [r1, #8]
    ldr     r2, [r1, #4]
    ldr     r1, [r1]
    svc     0x12
    ldmfd   sp!, {r4, r5}
    bx      lr
