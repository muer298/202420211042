#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  if (sync() < 0) {
    printf("PGTBL FAIL\n");
    exit(1);
  }
  printf("PGTBL PASS\n");
  exit(0);
}
