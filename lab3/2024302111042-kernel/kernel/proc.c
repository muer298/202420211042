#include "lab3.h"

static struct proc processes[NPROC];
struct proc *current;
static int nextpid = 1, last = -1;
static struct proc *deferred_reap;
extern uint64 _uprog_table[];

static void
release(struct proc *p)
{
  if (p->pagetable) free_user_pagetable(p->pagetable);
  if (p->tf) kfree(p->tf);
  if (p->kstack) kfree(p->kstack);
  memset(p, 0, sizeof(*p));
}

static struct proc *
allocate(void)
{
  struct proc *p = 0;
  for (int i = 0; i < NPROC; i++)
    if (processes[i].state == UNUSED) { p = &processes[i]; break; }
  if (!p) return 0;
  memset(p, 0, sizeof(*p));
  p->tf = kalloc();
  p->kstack = kalloc();
  if (!p->tf || !p->kstack) goto fail;
  p->pagetable = proc_pagetable(p);
  if (!p->pagetable) goto fail;
  p->pid = nextpid++;
  p->state = RUNNABLE;
  return p;
fail:
  release(p);
  return 0;
}

void
proc_reap_deferred(void)
{
  if (deferred_reap && deferred_reap != current) {
    struct proc *p = deferred_reap;
    deferred_reap = 0;
    release(p);
  }
}

static int
map_new_pages(pagetable_t pt, uint64 start, uint64 end, uint64 perm)
{
  for (uint64 va = start; va < end; va += PGSIZE) {
    void *page = kalloc();
    if (!page) return -1;
    if (mappages(pt, va, PGSIZE, (uint64)page, perm) < 0) {
      kfree(page);
      return -1;
    }
  }
  return 0;
}

int
proc_exec(struct proc *p, const char *name)
{
  uint64 *entry = _uprog_table;
  while (entry[0]) {
    uint64 bin_start = entry[0], bin_end = entry[1];
    uint64 data_start = entry[2], memsz = entry[3];
    const char *entry_name = (const char *)(entry + 4);
    if (streq(name, entry_name)) {
      uint64 binsz = bin_end - bin_start;
      uint64 image_end = PGROUNDUP(memsz);
      uint64 guard_base = image_end;
      uint64 stack_base = guard_base + (uint64)LAB3_GUARD_PAGES * PGSIZE;
      uint64 stack_top = stack_base + PGSIZE;
      if (!data_start || data_start % PGSIZE || binsz > memsz ||
          data_start > image_end || stack_top >= TRAPFRAME)
        return -1;

      pagetable_t oldpt = p->pagetable;
      pagetable_t newpt = proc_pagetable(p);
      if (!newpt) return -1;
      if (map_new_pages(newpt, 0, data_start, PTE_R | PTE_X | PTE_U) < 0 ||
          map_new_pages(newpt, data_start, image_end, PTE_R | PTE_W | PTE_U) < 0 ||
          map_new_pages(newpt, stack_base, stack_top, PTE_R | PTE_W | PTE_U) < 0) {
        free_user_pagetable(newpt);
        return -1;
      }

      struct proc shadow = *p;
      shadow.pagetable = newpt;
      if (uvm_load_bytes(&shadow, 0, (void *)bin_start, binsz) < 0) {
        free_user_pagetable(newpt);
        return -1;
      }

      struct trapframe ntf;
      memset(&ntf, 0, sizeof(ntf));
      ntf.epc = 0;
      ntf.sp = stack_top;
      p->pagetable = newpt;
      p->data_start = data_start;
      p->image_end = image_end;
      p->guard_base = guard_base;
      p->stack_base = stack_base;
      p->heap_base = stack_top;
      p->sz = stack_top;
      *p->tf = ntf;
      free_user_pagetable(oldpt);
      return 0;
    }
    /* Descriptor/name is followed by its binary, then the next descriptor. */
    entry = (uint64 *)((bin_end + 7) & ~7UL);
  }
  return -1;
}

void
proc_init(void)
{
  struct proc *p = allocate();
  if (!p || proc_exec(p, "sh")) panic("load sh");
  dump_pagetable(p->pagetable);
}

int
proc_fork(void)
{
  struct proc *child = allocate();
  if (!child) return -1;
  child->data_start = current->data_start;
  child->image_end = current->image_end;
  child->guard_base = current->guard_base;
  child->stack_base = current->stack_base;
  child->heap_base = current->heap_base;
  child->sz = current->sz;
  if (uvmcopy(current->pagetable, child->pagetable) < 0) {
    release(child);
    return -1;
  }
  memcpy(child->tf, current->tf, sizeof(*child->tf));
  child->tf->a0 = 0;
  child->parent = current->pid;
  return child->pid;
}

void
proc_exit(int status)
{
  if (current->pid == 1) panic("initial shell exited");
  current->status = status;
  current->state = ZOMBIE;
  current->pending = NONE;
}

void
proc_fault(uint64 cause)
{
  if (current->pid == 1) panic("initial shell fault");
  current->status = 128 + (int)(cause & 0xff);
  current->state = ZOMBIE;
  current->pending = NONE;
}

long
proc_sbrk(long n)
{
  uint64 old = current->sz;
  if (n > 0) {
    uint64 amount = (uint64)n;
    if (amount > TRAPFRAME - old) return -1;
    uint64 next = old + amount;
    if (uvmalloc(current, old, next) < 0) return -1;
    current->sz = next;
  } else if (n < 0) {
    uint64 amount = (uint64)(-(n + 1)) + 1;
    if (amount > old - current->heap_base) return -1;
    uint64 next = old - amount;
    if (uvmdealloc(current, old, next) < 0) return -1;
    current->sz = next;
  }
  return (long)old;
}

int
proc_wait(struct proc *parent, int *status)
{
  int found = 0;
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &processes[i];
    if (p->state == UNUSED || p->parent != parent->pid) continue;
    found = 1;
    if (p->state == ZOMBIE) {
      int pid = p->pid;
      *status = p->status;
      if (parent->tf->a0 && copyout(parent, parent->tf->a0, status, sizeof(*status)))
        return -1;
      if (p == current) {
        if (deferred_reap) panic("multiple deferred reap");
        p->state = REAPING;
        p->parent = 0;
        deferred_reap = p;
      } else {
        release(p);
      }
      return pid;
    }
  }
  return found ? -2 : -1;
}

int
proc_kill(int pid)
{
  if (pid <= 1) return -1;
  for (int i = 0; i < NPROC; i++)
    if (processes[i].state != UNUSED && processes[i].pid == pid) {
      processes[i].status = 130;
      processes[i].state = ZOMBIE;
      processes[i].pending = NONE;
      return 0;
    }
  return -1;
}

void
proc_interrupt(void)
{
  for (int i = 0; i < NPROC; i++)
    if (processes[i].state != UNUSED && processes[i].state != ZOMBIE &&
        processes[i].state != REAPING && processes[i].pid != 1)
      proc_kill(processes[i].pid);
}

static void
ready_blocked(void)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &processes[i];
    if (p->state != BLOCKED) continue;
    int result = -2, status;
    if (p->pending == READING) result = console_read(p, p->tf->a1, (int)p->tf->a2);
    if (p->pending == WAITING) result = proc_wait(p, &status);
    if (p->pending == PAUSING && ticks >= p->deadline) result = 0;
    if (result != -2) {
      p->tf->a0 = result;
      p->pending = NONE;
      p->state = RUNNABLE;
    }
  }
}

void
schedule(void)
{
  intr_off();
  for (;;) {
    proc_reap_deferred();
    ready_blocked();
    for (int step = 1; step <= NPROC; step++) {
      int i = (last + step) % NPROC;
      if (processes[i].state == RUNNABLE) {
        last = i;
        current = &processes[i];
        current->state = RUNNING;
        usertrapret();
      }
    }
    intr_on();
    asm volatile("wfi");
    intr_off();
  }
}
