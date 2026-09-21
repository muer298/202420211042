#include "types.h"
#include "riscv.h"

extern void main(void);
extern void trap_vector(void);

#define MSTATUS_MIE (1L << 3)

/* Complete the machine-mode obligations, then enter main() in S mode. */
void __attribute__((noreturn))
start(void)
{
  uint64 status = r_mstatus();

  /* mret must select S mode; interrupts remain disabled throughout lab 1. */
  status &= ~MSTATUS_MPP_MASK;
  status |= MSTATUS_MPP_S;
  status &= ~MSTATUS_MIE;
  w_mstatus(status);
  w_mepc((uint64)main);

  /* Delegate normal traps to S mode, but enable no interrupt source. */
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(0);
  w_stvec((uint64)trap_vector);

  /* Lab 1 uses physical addresses directly, so satp remains in Bare mode. */
  w_satp(0);
  sfence_vma();

  /* NAPOT entry 0 grants S/U mode read, write and execute access everywhere. */
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  asm volatile("mret");
  __builtin_unreachable();
}
