#include "lab3.h"
volatile uint64 ticks;
#define PLIC_CLAIM (*(volatile uint *)(PLIC + 0x201004))
void plic_init(void) {
  *(volatile uint *)(PLIC + UART0_IRQ*4)=1;
  *(volatile uint *)(PLIC + 0x2080)=1U<<UART0_IRQ;
  *(volatile uint *)(PLIC + 0x201000)=0;
}
void timer_init(void) {
  w_stimecmp(r_time()+TIMER_INTERVAL);
  w_sie(SIE_STIE|SIE_SEIE);
}
int device_intr(uint64 cause) {
  if(cause==((1UL<<63)|9)) {
    uint irq=PLIC_CLAIM;
    if(irq==UART0_IRQ) uartintr();
    if(irq) PLIC_CLAIM=irq;
    return 1;
  }
  if(cause==((1UL<<63)|5)) {
    ticks++;
    w_stimecmp(r_time()+TIMER_INTERVAL);
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
    int dev=device_intr(cause);
    if(!dev) {
      printf("usertrap pid=%d scause=%lx sepc=%lx stval=%lx\n",
             current->pid,cause,current->tf->epc,r_stval());
      proc_fault(cause);
    }
    if(dev==2 && ticks%LAB2_TICK==0 && current->state==RUNNING)
      current->state=RUNNABLE;
  }
  if(current->state==RUNNING) usertrapret();
  schedule();
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
