#include "lab3.h"

void __attribute__((noreturn))
main(void)
{
  consoleinit();
  /* Submission output contains exactly one personalized banner. */
  printf("OSLAB1 sid=%lu mod97=%x\n",
         (uint64)COURSE_SID, (uint)(COURSE_SID % 97));
  console_plain();
  kinit();
  vm_selftest();
  vm_init();
  w_stvec((uint64)kernelvec);
  plic_init();
  timer_init();
  proc_init();
  schedule();
}
