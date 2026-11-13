#include "lab3.h"
#include "syscall.h"
static uint64 argraw(int n) {
  switch(n) {
    case 0: return current->tf->a0;
    case 1: return current->tf->a1;
    case 2: return current->tf->a2;
    case 3: return current->tf->a3;
    case 4: return current->tf->a4;
    case 5: return current->tf->a5;
    default: return 0;
  }
}
static int argint(int n) { return (int)argraw(n); }
static uint64 argaddr(int n) { return argraw(n); }
void syscall(void) {
  long result=-1;
  switch(current->tf->a7) {
    case SYS_getpid: result=current->pid; break;
    case SYS_write: {
      int fd=argint(0), n=argint(2);
      uint64 src=argaddr(1);
      if((fd!=1 && fd!=2) || n<0 || !user_access(current,src,(uint64)n,0)) break;
      int ok=1;
      for(int i=0;i<n;i++) {
        char c;
        if(copyin(current,&c,src+i,1)) { ok=0; break; }
        uartputc_sync(c);
      }
      if(ok) result=n;
      break;
    }
    case SYS_read://控制台 read
      if(argint(0)!=0) break;
      do {
        result=console_read(current,argaddr(1),argint(2));
        if(result==-2) proc_sleep((uint64)0x434f4e53UL, 0);
      } while(result==-2);
      //`console_read()` 返回 -2 表示“请求有效，但现在还没有可读数据”，此时进程睡在控制台 channel 0x434f4e53 上
      break;
    case SYS_fork: result=proc_fork(); break;
    case SYS_exec: {
      char name[64];
      if(copyinstr(current,name,argaddr(0),sizeof(name))) break;
      int n=0; while(name[n]) n++;
      while(n && (name[n-1]=='\n' || name[n-1]=='\r')) name[--n]=0;
      result=proc_exec(current,name);
      break;
    }
    case SYS_exit: proc_exit(argint(0)); return;
    case SYS_wait: {//父进程等子进程退出
      int status;
      if(argaddr(0) && !user_access(current,argaddr(0),sizeof(status),1)) break;
      do {
        result=proc_wait(current,&status);//先检查用户提供的状态地址
        if(result==-2) proc_sleep((uint64)current->pid, 0);
        //如果 proc_wait() 返回 -2，表示子进程还没退出；父进程就睡在自己的 PID channel 上。
      } while(result==-2);
      break;
    }
    case SYS_kill: result=proc_kill(argint(0)); break;
    case SYS_sbrk: result=proc_sbrk((long)argraw(0)); break;
    case SYS_uptime: result=ticks; break;
    case SYS_pause://等待指定数量的 tick
    //argint(0)：取出用户传入的第一个参数，也就是你想要等待多少个 tick
      if(argint(0)<0) break;
      result=0;//设置系统调用返回值为 0，0 代表调用成功。
      if(argint(0)>0) {
        //它先算出目标 tick
        uint64 until=ticks+(uint)argint(0);
        while(ticks<until) proc_sleep((uint64)&ticks, 0);
        //用while不用if：要有人唤醒这个通道，通道上所有睡觉的进程，全部一起被叫醒
        //等待通道channel，这里用全局变量 ticks 的地址当作等待标记
        //然后睡在 &ticks 这个 channel 上。使用 while 是因为被唤醒后还要重新检查时间是否真的到了；若未到，就继续睡眠。
      }
      break;
    case SYS_sync:
      dump_pagetable(current->pagetable);
      result=0;
      break;
    case SYS_schedstat: {
      uint64 cpu=0,last=0; int pid=argint(0);
      int cpu32, last32;
      if(proc_schedstat(pid,&cpu,&last)==0) {
        cpu32=(int)cpu; last32=(int)last;
        if(copyout(current,argaddr(1),&cpu32,sizeof(cpu32))==0 &&
           copyout(current,argaddr(2),&last32,sizeof(last32))==0) result=0;
      }
      break;
    }
    case SYS_time_rdtime: result=(long)r_time(); break;
    case SYS_setickets: result=proc_settickets(argint(0)); break;
    default: break;
  }
  if(current->state==RUNNING) current->tf->a0=result;
}
