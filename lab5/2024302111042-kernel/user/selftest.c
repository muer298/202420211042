#include "kernel/types.h"
#include "user/user.h"
static volatile int private_value=7;
static inline __attribute__((always_inline)) void fail(const char *s) {
  printf("SELFTEST FAIL %s\n",s); exit(1);
}
int main(void) {
  if(getpid()<=1) fail("pid");
  if(write(1,(void *)0x80000000UL,4)!=-1) fail("bad write address");
  if(read(0,(void *)0x80000000UL,4)!=-1) fail("bad read address");
  if(read(0,(void *)~0UL,2)!=-1) fail("overflow address");
  if(write(7,"x",1)!=-1 || read(2,(void *)&private_value,1)!=-1) fail("fd");
  if(write(1,"",0)!=0 || read(0,(void *)&private_value,0)!=0) fail("zero length");
  char *argv[]={"missing",0};
  if(exec("missing",argv)!=-1) fail("exec failure");
  int before=uptime();
  if(pause(3)!=0 || uptime()-before<3) fail("timer");
  int child=fork();
  if(child<0) fail("fork");
  if(child==0) { private_value=99; exit(37); }
  int status=0;
  if(wait(&status)!=child || status!=37 || private_value!=7) fail("wait/copy");
  /* Busy child never calls the kernel: parent can continue only via timer preemption. */
  child=fork();
  if(child<0) fail("fork spin");
  if(child==0) for(;;) asm volatile("nop");
  if(pause(4)!=0 || kill(child)!=0 || wait(&status)!=child || status!=130) fail("preemption");
  if(wait(0)!=-1) fail("wait without child");
  printf("SELFTEST PASS: pointers fork wait exec timer preemption\n");
  exit(0);
}
