/* Standalone QEMU test entry: never linked into the submission kernel. */
#include "types.h"
#include "console.h"

void __attribute__((noreturn))
main(void)
{
  consoleinit();
  printf("d:%d,%d,%d,%d\n", 0, -123, 2147483647, (-2147483647 - 1));
  printf("x:%x,%x,%x\n", 0U, 0x48U, 0xffffffffU);
  printf("s:[%s],[%s]\n", "", "Hello RISC-V OS!");
  printf("percent:%%\n");
  printf("long:%ld,%ld,%lu,%lx\n", 9223372036854775807L,
         (-9223372036854775807L - 1L), 18446744073709551615UL,
         0xffffffffffffffffUL);
  /* Empty output, literal dot, consecutive newlines and terminal newline. */
  printf("%s", "");
  printf(".\n\n");
  for (uint i = 0; i < 128; i++)
    printf("%s", "abcdefghijklmnopqrstuvwxyz0123456789");
  printf("\n");
  for (;;)
    asm volatile("wfi");
}
