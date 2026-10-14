#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char *base = sbrk(0);
  char *p = sbrk(8192);
  if (p == SBRK_ERROR || p != base) {
    printf("VMTEST FAIL sbrk grow\n");
    exit(1);
  }
  char *cross = p + 4090;
  for (int i = 0; i < 20; i++) cross[i] = 'A' + i;
  if (write(1, cross, 20) != 20) {
    printf("VMTEST FAIL cross-page copyin\n");
    exit(1);
  }
  printf("\n");
  if (sbrk(-4096) != p + 8192 || sbrk(0) != p + 4096) {
    printf("VMTEST FAIL sbrk shrink\n");
    exit(1);
  }
  printf("VMTEST PASS cross-page copy and sbrk\n");
  exit(0);
}
