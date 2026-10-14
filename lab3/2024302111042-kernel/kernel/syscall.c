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
    case SYS_read:
      if(argint(0)!=0) break;
      result=console_read(current,argaddr(1),argint(2));
      if(result==-2) { current->pending=READING; current->state=BLOCKED; }
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
    case SYS_wait: {
      int status;
      if(argaddr(0) && !user_access(current,argaddr(0),sizeof(status),1)) break;
      result=proc_wait(current,&status);
      if(result==-2) { current->pending=WAITING; current->state=BLOCKED; }
      break;
    }
    case SYS_kill: result=proc_kill(argint(0)); break;
    case SYS_sbrk: result=proc_sbrk((long)argraw(0)); break;
    case SYS_uptime: result=ticks; break;
    case SYS_pause:
      if(argint(0)<0) break;
      result=0;
      if(argint(0)>0) {
        current->deadline=ticks+(uint)argint(0);
        current->pending=PAUSING; current->state=BLOCKED;
      }
      break;
    case SYS_sync:
      dump_pagetable(current->pagetable);
      result=0;
      break;
    default: break;
  }
  if(current->state==RUNNING) current->tf->a0=result;
}
