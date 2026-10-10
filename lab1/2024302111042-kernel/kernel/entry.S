#include "course_sid.h"

/* QEMU's reset stub enters here in machine mode at 0x80000000. */
    .section .text.entry
    .globl _entry
_entry:
    /* Keep startup deterministic until supervisor mode is fully configured. */
    csrci mstatus, 8
    csrw    mie, zero

    /* mtvec BASE must be four-byte aligned in direct mode. */
    la      t0, trap_vector
    csrw    mtvec, t0

    /* Only hart 0 may use the single boot stack. */
    csrr    t0, mhartid
    bnez    t0, park
    mv      tp, t0

    /* The stack grows down from this 16-byte-aligned address. */
    la      sp, boot_stack
    li      t1, LAB1_STACK_KB * 1024
    add     sp, sp, t1
    call    start

/* start() and main() are not expected to return. */
park:
    wfi
    j       park

    .balign 4
    .globl trap_vector
trap_vector:
    /* Lab 1 expects no traps. Stop so a debugger can inspect the CSRs. */
    wfi
    j       trap_vector
