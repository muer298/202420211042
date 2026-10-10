#include "lab2.h"
static struct proc processes[NPROC];
struct proc *current;
static int nextpid=1, last=-1;
extern uint64 _uprog_table[];

static void release(struct proc *p) {
  for(int i=0;i<USER_PAGES;i++) kfree(p->pages[i]);
  free_pagetable(p->pagetable);
  kfree(p->tf); kfree(p->kstack);
  memset(p,0,sizeof(*p));
}
static struct proc *allocate(void) {
  struct proc *p=0;
  for(int i=0;i<NPROC;i++) if(processes[i].state==UNUSED) { p=&processes[i]; break; }
  if(!p) return 0;
  p->pagetable=kalloc(); p->tf=kalloc(); p->kstack=kalloc();
  if(!p->pagetable || !p->tf || !p->kstack) goto fail;
  for(int i=0;i<USER_PAGES;i++) {
    p->pages[i]=kalloc();
    if(!p->pages[i] || map_page(p->pagetable,i*PGSIZE,(uint64)p->pages[i],PTE_R|PTE_W|PTE_X|PTE_U)) goto fail;
  }
  if(map_page(p->pagetable,TRAMPOLINE,(uint64)trampoline,PTE_R|PTE_X) ||
     map_page(p->pagetable,TRAPFRAME,(uint64)p->tf,PTE_R|PTE_W)) goto fail;
  p->pid=nextpid++;
  p->state=RUNNABLE;
  return p;
fail:
  release(p);
  return 0;
}
int proc_exec(struct proc *p, const char *name) {
  uint64 *entry=_uprog_table;
  while(entry[0]) {
    const char *entry_name=(const char *)(entry+2);
    if(streq(name,entry_name)) {
      uint64 size=entry[1]-entry[0];
      if(size>USER_SIZE-PGSIZE) return -1;
      for(int i=0;i<USER_PAGES;i++) memset(p->pages[i],0,PGSIZE);
      if(copyout(p,0,(void *)entry[0],size)) return -1;
      memset(p->tf,0,sizeof(*p->tf));
      p->tf->epc=0; p->tf->sp=USER_SIZE;
      return 0;
    }
    /* Gift format interleaves descriptor/name and binary, then aligns to 8. */
    entry=(uint64 *)((entry[1]+7)&~7UL);
  }
  return -1;
}
void proc_init(void) {
  struct proc *p=allocate();
  if(!p || proc_exec(p,"sh")) panic("load sh");
}
int proc_fork(void) {
  struct proc *child=allocate();
  if(!child) return -1;
  for(int i=0;i<USER_PAGES;i++) memcpy(child->pages[i],current->pages[i],PGSIZE);
  memcpy(child->tf,current->tf,sizeof(*child->tf));
  child->tf->a0=0;
  child->parent=current->pid;
  return child->pid;
}
void proc_exit(int status) {
  if(current->pid==1) panic("initial shell exited");
  current->status=status;
  current->state=ZOMBIE;
  current->pending=NONE;
}
int proc_wait(struct proc *parent, int *status) {
  int found=0;
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i];
    if(p->state==UNUSED || p->parent!=parent->pid) continue;
    found=1;
    if(p->state==ZOMBIE) {
      int pid=p->pid;
      *status=p->status;
      if(parent->tf->a0 && copyout(parent,parent->tf->a0,status,sizeof(*status))) return -1;
      release(p);
      return pid;
    }
  }
  return found ? -2 : -1;
}
int proc_kill(int pid) {
  if(pid<=1) return -1;
  for(int i=0;i<NPROC;i++) if(processes[i].state!=UNUSED && processes[i].pid==pid) {
    processes[i].status=130;
    processes[i].state=ZOMBIE;
    processes[i].pending=NONE;
    return 0;
  }
  return -1;
}
void proc_interrupt(void) {
  for(int i=0;i<NPROC;i++) if(processes[i].state!=UNUSED && processes[i].pid!=1)
    proc_kill(processes[i].pid);
}
static void ready_blocked(void) {
  for(int i=0;i<NPROC;i++) {
    struct proc *p=&processes[i];
    if(p->state!=BLOCKED) continue;
    int result=-2, status;
    if(p->pending==READING) result=console_read(p,p->tf->a1,(int)p->tf->a2);
    if(p->pending==WAITING) result=proc_wait(p,&status);
    if(p->pending==PAUSING && ticks>=p->deadline) result=0;
    if(result!=-2) { p->tf->a0=result; p->pending=NONE; p->state=RUNNABLE; }
  }
}
void schedule(void) {
  intr_off();
  for(;;) {
    ready_blocked();
    for(int step=1;step<=NPROC;step++) {
      int i=(last+step)%NPROC;
      if(processes[i].state==RUNNABLE) {
        last=i; current=&processes[i]; current->state=RUNNING;
        usertrapret();
      }
    }
    /* No runnable task: hardware interrupts remain live, no read busy-loop. */
    intr_on();
    asm volatile("wfi");
    intr_off();
  }
}
