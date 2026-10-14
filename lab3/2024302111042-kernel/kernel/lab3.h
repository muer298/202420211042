#ifndef LAB3_H
#define LAB3_H

#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "console.h"
#include "param.h"

#define TIMER_INTERVAL 100000UL
#define PTE_A (1L << 6)
#define PTE_D (1L << 7)

/* Exact ABI of the immutable gifted trampoline.S, in bytes. */
struct trapframe {
  uint64 kernel_satp, kernel_sp, kernel_trap, epc, kernel_hartid;
  uint64 ra, sp, gp, tp, t0, t1, t2, s0, s1;
  uint64 a0, a1, a2, a3, a4, a5, a6, a7;
  uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6;
};
#define OFFSET_CHECK(field, offset) _Static_assert(__builtin_offsetof(struct trapframe, field) == offset, "trampoline ABI: " #field)
OFFSET_CHECK(kernel_satp, 0); OFFSET_CHECK(kernel_sp, 8);
OFFSET_CHECK(kernel_trap, 16); OFFSET_CHECK(epc, 24); OFFSET_CHECK(kernel_hartid, 32);
OFFSET_CHECK(ra, 40); OFFSET_CHECK(sp, 48); OFFSET_CHECK(gp, 56); OFFSET_CHECK(tp, 64);
OFFSET_CHECK(t0, 72); OFFSET_CHECK(t1, 80); OFFSET_CHECK(t2, 88);
OFFSET_CHECK(s0, 96); OFFSET_CHECK(s1, 104); OFFSET_CHECK(a0, 112);
OFFSET_CHECK(a1, 120); OFFSET_CHECK(a2, 128); OFFSET_CHECK(a3, 136);
OFFSET_CHECK(a4, 144); OFFSET_CHECK(a5, 152); OFFSET_CHECK(a6, 160); OFFSET_CHECK(a7, 168);
OFFSET_CHECK(s2, 176); OFFSET_CHECK(s3, 184); OFFSET_CHECK(s4, 192); OFFSET_CHECK(s5, 200);
OFFSET_CHECK(s6, 208); OFFSET_CHECK(s7, 216); OFFSET_CHECK(s8, 224); OFFSET_CHECK(s9, 232);
OFFSET_CHECK(s10, 240); OFFSET_CHECK(s11, 248); OFFSET_CHECK(t3, 256);
OFFSET_CHECK(t4, 264); OFFSET_CHECK(t5, 272); OFFSET_CHECK(t6, 280);
_Static_assert(sizeof(struct trapframe) == 288, "trapframe size");

enum state { UNUSED, RUNNABLE, RUNNING, BLOCKED, ZOMBIE, REAPING };
enum pending { NONE, READING, WAITING, PAUSING };

struct proc {
  enum state state;
  enum pending pending;
  int pid, parent, status;
  uint64 deadline;
  pagetable_t pagetable;
  struct trapframe *tf;
  void *kstack;
  uint64 data_start, image_end, guard_base, stack_base, heap_base, sz;
};

extern struct proc *current;
extern pagetable_t kernel_pagetable;
extern volatile uint64 ticks;
extern char trampoline[], uservec[], userret[], kernel_end[];

void *memset(void *, int, uint64);
void *memcpy(void *, const void *, uint64);
int streq(const char *, const char *);
void panic(const char *) __attribute__((noreturn));

void kinit(void);
void *kalloc(void);
void kfree(void *);
uint64 kfree_pages(void);
void vm_selftest(void);
void vm_init(void);
pte_t *walk(pagetable_t, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, uint64);
void uvmunmap(pagetable_t, uint64, uint64, int);
pagetable_t proc_pagetable(struct proc *);
void free_user_pagetable(pagetable_t);
int uvmcopy(pagetable_t, pagetable_t);
int uvmalloc(struct proc *, uint64, uint64);
int uvmdealloc(struct proc *, uint64, uint64);
int uvm_load_bytes(struct proc *, uint64, const void *, uint64);
int copyin(struct proc *, void *, uint64, uint64);
int copyout(struct proc *, uint64, const void *, uint64);
int copyinstr(struct proc *, char *, uint64, uint64);
int user_access(struct proc *, uint64, uint64, int);
int user_range(uint64, uint64);
void dump_pagetable(pagetable_t);

void proc_init(void);
int proc_exec(struct proc *, const char *);
int proc_fork(void);
void proc_exit(int);
int proc_wait(struct proc *, int *);
int proc_kill(int);
void proc_interrupt(void);
void proc_fault(uint64);
void proc_reap_deferred(void);
long proc_sbrk(long);
void schedule(void) __attribute__((noreturn));

void syscall(void);
void usertrap(void) __attribute__((noreturn));
void usertrapret(void) __attribute__((noreturn));
void kernelvec(void);
void kerneltrap(void);
void timer_init(void);
void plic_init(void);
int device_intr(uint64);
void uartintr(void);
int console_read(struct proc *, uint64, int);
void console_plain(void);

#endif
