#include "types.h"
#include "riscv.h"
#include "course_sid.h"

extern void main(void);
extern void trap_vector(void);

#define MSTATUS_MIE (1L << 3)
#define MSTATUS_MPIE (1L << 7)

/* Only hart 0 uses this stack; secondary harts park before entering C. */
char boot_stack[LAB1_STACK_KB * 1024] __attribute__((aligned(16)));

/* Complete the machine-mode obligations, then enter main() in S mode. */
void __attribute__((noreturn))
start(void)
{
  uint64 status = r_mstatus();

  /* mret must select S mode; interrupts remain disabled throughout lab 1. */
  status &= ~MSTATUS_MPP_MASK;
  status |= MSTATUS_MPP_S;
  status &= ~(MSTATUS_MIE | MSTATUS_MPIE | SSTATUS_SIE);
  w_mstatus(status);
  w_mepc((uint64)main);

  /* Prepare timer/external source masks; global SIE stays off in lab 1. */
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_mie(MIE_STIE);
  w_sie(SIE_STIE | SIE_SEIE);
  w_stvec((uint64)trap_vector);

  /* Lab 1 uses physical addresses directly, so satp remains in Bare mode. */
  w_satp(0);
  sfence_vma();

  /* A=TOR (0x08), R/W/X=1: entry 0 covers memory from zero to its top. */
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  asm volatile("mret");
  __builtin_unreachable();
}
