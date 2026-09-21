#include "types.h"
#include "course_sid.h"

extern void printf(const char *fmt, ...);

void __attribute__((noreturn))
main(void)
{
  /* Required personalized banner and self-check sequence. */
  printf("OSLAB1 sid=%lu mod97=0x%x\n",
         (uint64)COURSE_SID, (uint)(COURSE_SID % 97));

  /* Lab-embedded boundary checks for the minimal formatter. */
  printf("printf-test zero=%d negative=%d max=%d empty='%s' hex=0x%x\n",
         0, -123, 2147483647, "", (uint)(COURSE_SID % 97));
  printf("printf-test long=abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789abcdefghijklmnopqrstuvwxyz\n");

  for (;;)
    asm volatile("wfi");
}
