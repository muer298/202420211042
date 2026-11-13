// Lab5综合自测：引用计数、COW隔离、并发子进程、非法写、配额与exec参数。
#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

#define NCHILD 3
#define NPAGES LAB5_COW_BUDGET

#define fail(what) do { printf("COWTEST FAIL: %s\n", (what)); exit(1); } while (0)

int main(void) {
  if ((long long)pmc(2) != -1) fail("pmc invalid selector");

  // 配额测试先执行，确保当前进程的 COW 拷贝计数从零开始。
  volatile char *bulk=sbrk(NPAGES * 4096);
  if ((char *)bulk == SBRK_ERROR) fail("budget sbrk");
  for (int i=0;i<NPAGES;i++) bulk[i*4096]=(char)i;
  int sleeper=fork();
  if (sleeper < 0) fail("budget fork");
  if (sleeper == 0) for (;;) pause(1);
  unsigned long long before=pmc(0);
  for (int i=0;i<NPAGES;i++) bulk[i*4096]=(char)(i+1);
  unsigned long long copied=pmc(0)-before;
  if (copied < NPAGES) fail("budget COW copies missing");
  if (kill(sleeper) < 0) fail("budget child kill");
  int status=-1;
  if (wait(&status) != sleeper || status != 130) fail("budget child reap");
  printf("COWTEST PASS budget exercised copies=%llu\n", copied);

  // 单页多轮 fork/双边写，检查每一轮父子读回互不污染。
  char *page=sbrk(4096);
  if (page == SBRK_ERROR) fail("single page sbrk");
  for (int round=0;round<3;round++) {
    page[0]=(char)('a'+round);
    int pid=fork();
    if (pid < 0) fail("single page fork");
    if (pid == 0) {
      page[0]=(char)('x'+round);
      if (page[0] != (char)('x'+round)) fail("child single-page readback");
      exit(0);
    }
    page[0]=(char)('A'+round);
    if (page[0] != (char)('A'+round)) fail("parent single-page isolation");
    status=-1;
    if (wait(&status) != pid || status != 0) fail("single page wait");
    if (page[0] != (char)('A'+round)) fail("parent value changed");
  }

  // 多个子进程共享后分别写同一堆页，退出后共享页计数应归零。
  char *shared=sbrk(4096);
  if (shared == SBRK_ERROR) fail("multi-child sbrk");
  shared[0]='0';
  int pids[NCHILD];
  for (int i=0;i<NCHILD;i++) {
    pids[i]=fork();
    if (pids[i] < 0) fail("multi-child fork");
    if (pids[i] == 0) {
      shared[0]=(char)('1'+i);
      if (shared[0] != (char)('1'+i)) fail("child isolation");
      exit(i+1);
    }
  }
  shared[0]='P';
  for (int i=0;i<NCHILD;i++) {
    status=-1;
    if (wait(&status) <= 0 || status < 1 || status > NCHILD)
      fail("multi-child exit status");
  }
  if (shared[0]!='P' || pmc(1)!=0) fail("multi-child isolation/refcount cleanup");

  // 写只读代码页必须杀掉故障子进程，但不能杀死内核/父进程。
  int bad=fork();
  if (bad < 0) fail("bad-write fork");
  if (bad == 0) { *(volatile int *)0=7; exit(0); }
  status=0;
  if (wait(&status)!=bad || status==0) fail("readonly code write survived");
  printf("COWTEST PASS readonly write isolated status=%d\n", status);

  // exec 参数与坏用户指针：失败必须返回 -1，旧进程仍可继续。
  char *args[]={(char *)"echo",(char *)"cowtest-argv",0};
  if (exec((char *)~0UL,args)!=-1) fail("invalid path accepted");
  if (exec("hi",(char **)0x80000000UL)!=-1) fail("invalid argv accepted");
  int ep=fork();
  if (ep < 0) fail("echo fork");
  if (ep == 0) { exec("echo",args); exit(1); }
  status=-1;
  if (wait(&status)!=ep || status!=0) fail("exec argc/argv stack");
  printf("COWTEST PASS invalid pointers and exec argv\n");
  printf("COWTEST PASS all requested cases\n");
  exit(0);
}
