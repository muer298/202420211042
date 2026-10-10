#include "lab2.h"
#ifndef CONSOLE_SEMANTICS
#define CONSOLE_SEMANTICS LAB2_BUF_SEMANTICS
#endif
_Static_assert(LAB2_BUF_SIZE >= 2, "console capacity");
_Static_assert(CONSOLE_SEMANTICS == 0 || CONSOLE_SEMANTICS == 1, "console mode");
static struct {
  char data[LAB2_BUF_SIZE];
  uint head, tail, used, committed;
} input;
static int banner = 1;
static uint sent;
#define REG(off) (*(volatile uchar *)(UART0 + (off)))
void consoleinit(void) {
  REG(1)=0;
  REG(3)=0x80;
  REG(0)=3; REG(1)=0;
  REG(3)=3;
  REG(2)=7;
  REG(1)=1;  /* Receive-data interrupt, no transmit interrupts. */
  memset(&input,0,sizeof(input));
  sent=0;
}
void console_plain(void) { banner=0; }
void uartputc_sync(int c) {
  while(!(REG(5)&0x20)) ;
  REG(0)=(uchar)c;
  io_fence();
  if(++sent == 16U + COURSE_SID % 16U) {
    sent=0;
    for(uint i=0;i<16U + COURSE_SID % 16U;i++) asm volatile("nop");
  }
}
void consputc(int c) {
  uartputc_sync(c);
  if(banner && LAB1_BANNER_PROTOCOL == 1) uartputc_sync('.');
}
void uartintr(void) {
  while(REG(5)&1) {
    int c=REG(0);
    if(c=='\r') c='\n';
    if(c==3) { /* Ctrl-C only kills a foreground child, never the shell. */
      proc_interrupt();
      memset(&input,0,sizeof(input));
      uartputc_sync('^'); uartputc_sync('C'); uartputc_sync('\n');
      continue;
    }
    if(CONSOLE_SEMANTICS == 0 && (c==8 || c==127)) {
      if(input.used > input.committed) {
        input.tail=(input.tail+LAB2_BUF_SIZE-1)%LAB2_BUF_SIZE;
        input.used--;
        uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
      }
      continue;
    }
    uint limit = (CONSOLE_SEMANTICS == 0 && c!='\n') ? LAB2_BUF_SIZE-1 : LAB2_BUF_SIZE;
    if(input.used >= limit) continue; /* Drop newest; reserve newline in line mode. */
    input.data[input.tail]=c;
    input.tail=(input.tail+1)%LAB2_BUF_SIZE;
    input.used++;
    if(CONSOLE_SEMANTICS == 1 || c=='\n') input.committed=input.used;
    uartputc_sync(c);
  }
}
/* Called with interrupts off; -2 means valid request, not yet ready. */
int console_read(struct proc *p, uint64 dst, int n) {
  if(n<0 || !user_range(dst,(uint64)n)) return -1;
  if(!n) return 0;
  if(!input.committed) return -2;
  int got=0;
  while(got<n && input.committed) {
    char c=input.data[input.head];
    if(copyout(p,dst+got,&c,1)) return -1;
    input.head=(input.head+1)%LAB2_BUF_SIZE;
    input.used--; input.committed--; got++;
    if(CONSOLE_SEMANTICS==0 && c=='\n') break;
  }
  return got;
}
