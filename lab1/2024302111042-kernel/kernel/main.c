#include "types.h"
#include "course_sid.h"

#include "console.h"

void __attribute__((noreturn))
main(void)
{
  consoleinit();
  /* Submission output contains exactly one personalized banner. */
  printf("OSLAB1 sid=%lu mod97=%x\n",
         (uint64)COURSE_SID, (uint)(COURSE_SID % 97));

  for (;;)
    asm volatile("wfi");
}
