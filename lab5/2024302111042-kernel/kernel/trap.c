#include "lab3.h"
volatile uint64 ticks;
#define PLIC_CLAIM (*(volatile uint *)(PLIC + 0x201004))
void plic_init(void) {
  *(volatile uint *)(PLIC + UART0_IRQ*4)=1;
  *(volatile uint *)(PLIC + 0x2080)=1U<<UART0_IRQ;
  *(volatile uint *)(PLIC + 0x201000)=0;
}

void timer_init(void) {
  w_stimecmp(r_time()+TIMER_INTERVAL);//定时器多久触发一次
  w_sie(SIE_STIE|SIE_SEIE);//开启定时器中断
}

//时钟中断增加 tick 并重新设定时钟
int device_intr(uint64 cause) {
  if(cause==((1UL<<63)|9)) {
    uint irq=PLIC_CLAIM;
    if(irq==UART0_IRQ) uartintr();
    if(irq) PLIC_CLAIM=irq;
    return 1;
  }
  if(cause==((1UL<<63)|5)) {
    ticks++;//时钟中断每次增加 ticks
    proc_wakeup((uint64)&ticks);//唤醒正在等待 tick 的进程，例如 pause()
    //因此，等待 tick 的进程会被时钟中断唤醒，再检查 until
    w_stimecmp(r_time()+TIMER_INTERVAL);//用新的 stimecmp 安排下一次时钟中断
    return 2;
  }
  return 0;
}
void kerneltrap(void) {
  uint64 epc=r_sepc(), status=r_sstatus(), cause=r_scause();
  if(!(status&SSTATUS_SPP) || !device_intr(cause)) {
    printf("kerneltrap scause=%lx sepc=%lx stval=%lx\n",cause,epc,r_stval());
    panic("kernel trap");
  }
  w_sepc(epc); w_sstatus(status);
}
//用户态时钟中断触发统计
void usertrap(void) {
  /* The gifted uservec intentionally leaves stvec unchanged. */
  w_stvec((uint64)kernelvec);
  if(r_sstatus()&SSTATUS_SPP) panic("usertrap from supervisor");
  proc_reap_deferred();
  current->tf->epc=r_sepc();
  uint64 cause=r_scause();
  if(cause==8) {
    current->tf->epc+=4;
    syscall();
  } else {
    int dev=device_intr(cause);//判断中断类型
    if(!dev) {
      printf("usertrap pid=%d scause=%lx sepc=%lx stval=%lx\n",
             current->pid,cause,current->tf->epc,r_stval());
      proc_fault(cause);//对非设备异常调用
    }
    if(dev==2 && current->state==RUNNING) proc_timer_tick();//如果是时钟中断（dev == 2），且当前进程仍为 RUNNING，调用 proc_timer_tick()
  }
  if(current->state!=RUNNING) proc_yield();//如果处理后进程状态已经不是 RUNNING，调用 proc_yield() 切回调度器
  usertrapret();
}
void usertrapret(void) {
  intr_off();
  struct trapframe *tf=current->tf;
  tf->kernel_satp=MAKE_SATP(kernel_pagetable);
  tf->kernel_sp=(uint64)current->kstack+PGSIZE;
  tf->kernel_trap=(uint64)usertrap;
  tf->kernel_hartid=0;
  w_stvec(TRAMPOLINE+((uint64)uservec-(uint64)trampoline));
  uint64 status=r_sstatus();
  status &= ~(SSTATUS_SPP|SSTATUS_SIE);
  status |= SSTATUS_SPIE;
  w_sstatus(status);
  w_sepc(tf->epc);
  ((void (*)(uint64))(TRAMPOLINE+((uint64)userret-(uint64)trampoline)))
    (MAKE_SATP(current->pagetable));
  __builtin_unreachable();
}
