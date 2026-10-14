#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"
int main(void) {
  char buf[LAB2_BUF_SIZE];
  printf("READPROBE READY capacity=%d\n", LAB2_BUF_SIZE);
  int n=read(0,buf,sizeof(buf));
  printf("READPROBE n=%d\n",n);
  if(n>0) write(1,buf,n);
  exit(n>0?0:1);
}
