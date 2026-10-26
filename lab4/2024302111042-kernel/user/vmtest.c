//验证跨页拷贝和堆扩展/收缩。
#include "kernel/types.h"
#include "user/user.h"
//SYS_sbrk → proc_sbrk() → uvmalloc() / map_fresh_range()
//SYS_write → user_access() → copyin()
int
main(void)
{
  char *base = sbrk(0);//读取当前堆边界
  char *p = sbrk(8192);//申请两页，检查返回地址就是原来的堆边界
  if (p == SBRK_ERROR || p != base) {
    printf("VMTEST FAIL sbrk grow\n");
    exit(1);
  }
  char *cross = p + 4090;//从 p+4090 写 20 个字节。第一页只剩 6 字节，所以这 20 字节跨两页。
  for (int i = 0; i < 20; i++) cross[i] = 'A' + i;
  if (write(1, cross, 20) != 20) {
    printf("VMTEST FAIL cross-page copyin\n");
    exit(1);
  }//调用 write(1, cross, 20)，内核检查用户缓冲区并通过 copyin() 分页复制
  printf("\n");//sbrk(-4096) 收缩一页，再用 sbrk(0) 确认新边界
  if (sbrk(-4096) != p + 8192 || sbrk(0) != p + 4096) {
    printf("VMTEST FAIL sbrk shrink\n");
    exit(1);
  }
  printf("VMTEST PASS cross-page copy and sbrk\n");
  exit(0);
}
