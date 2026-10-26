#include "lab3.h"

static struct proc processes[NPROC];
struct proc *current;
static int nextpid = 1;
static struct proc *deferred_reap;
static struct cpu cpu0;
static volatile uint ticks_lock;
static int scan_cursor = -1;
static uint64 slice_start;
static void
push_off(void)
{
  int enabled = intr_get();
  intr_off();
  if (cpu0.noff == 0) cpu0.intena = enabled;
  cpu0.noff++;
}

static void
pop_off(void)
{
  if (intr_get() || cpu0.noff < 1) panic("pop_off");
  cpu0.noff--;
  if (cpu0.noff == 0 && cpu0.intena) intr_on();
}

static void
acquire(struct proc *p)
{
  push_off();
  while (__sync_lock_test_and_set(&p->locked, 1)) ;
  __sync_synchronize();
}

static void
release_lock(struct proc *p)
{
  if (!p->locked) panic("release unlocked proc");
  __sync_synchronize();
  __sync_lock_release(&p->locked);
  pop_off();
}

static void lock(volatile uint *l) {
  push_off(); while (__sync_lock_test_and_set(l, 1)) ; __sync_synchronize();
}
static void unlock(volatile uint *l) {
  __sync_synchronize(); __sync_lock_release(l); pop_off();
}
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
  p->tickets = 7 + COURSE_SID % 13;
  p->level = 0;
  p->context.ra = (uint64)forkret;
  p->context.sp = (uint64)p->kstack + PGSIZE;
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

//加载一个用户程序，为进程 p 新建一套独立用户页表、划分用户地址空间布局、拷贝程序镜像、设置 trapframe 入口与栈指针
//proc_exec不是原地修改旧页表，而是从头到尾新建一张完整用户页表，全部准备妥当之后，再用新页表替换掉进程原来的旧页表
int
proc_exec(struct proc *p, const char *name)
{
  uint64 *entry = _uprog_table;
  //预定义的程序表，存放每个用户程序的元信息：`bin_start / bin_end / data_start / memsz / 程序名字符串`
  //它从 _uprog_table 取得程序二进制大小、data_start 和 end
  // 遍历程序表，按名字匹配要加载的程序
  while (entry[0]) {
    uint64 bin_start = entry[0], bin_end = entry[1];
    uint64 data_start = entry[2], memsz = entry[3];
    //data_start 代码段结束、数据段开始的虚拟地址分界点
    const char *entry_name = (const char *)(entry + 4);
    if (streq(name, entry_name)) {//找到目标程序
      uint64 binsz = bin_end - bin_start;//bin镜像文件本身的字节长度
      uint64 image_end = PGROUNDUP(memsz);//把程序内存终点向上对齐到页边界
      //memsz程序运行时总共需要的内存大小（bin + BSS）
      uint64 guard_base = image_end;//紧接在程序映像之后
      //`LAB3_GUARD_PAGES`：警戒页数量；guard 区域不调用任何映射函数，没有 PTE，访问直接缺页异常
      uint64 stack_base = guard_base + (uint64)LAB3_GUARD_PAGES * PGSIZE;//越过 guard 区后的一页用户栈起点
      uint64 stack_top = stack_base + PGSIZE;//用户栈顶，也作为初始堆起点
      if (!data_start || data_start % PGSIZE || binsz > memsz ||
          data_start > image_end || stack_top >= TRAPFRAME)
        return -1;

      pagetable_t oldpt = p->pagetable;//保存进程原来的旧页表
      pagetable_t newpt = proc_pagetable(p);//创建全新空白用户根页表
      if (!newpt) return -1;
      //map_new_pages内部会调用 `kalloc` 分配数据页 + 调用 `mappages` 建立映射；
      //它就是前面说的上层调用者，负责分配数据页所有权，`mappages` 只负责页表 / PTE
      if (map_new_pages(newpt, 0, data_start, PTE_R | PTE_X | PTE_U) < 0 ||//代码 / 只读段
          map_new_pages(newpt, data_start, image_end, PTE_R | PTE_W | PTE_U) < 0 ||//全局数据 + BSS
          map_new_pages(newpt, stack_base, stack_top, PTE_R | PTE_W | PTE_U) < 0) {//用户栈
        free_user_pagetable(newpt);
        // 任意一段映射失败：释放整张新用户页表（free_user_pagetable内部uvmunmap+do_free=1）
        return -1;
      }

      //shadow拷贝一份进程信息，临时使用，避免中途修改真实p
      struct proc shadow = *p;
      shadow.pagetable = newpt;
      //把bin二进制镜像拷贝到新用户地址空间；BSS超出bin的部分，map_new_pages已经分配清零页
      if (uvm_load_bytes(&shadow, 0, (void *)bin_start, binsz) < 0) {//只到`binsz`；`binsz ~ memsz` 是 BSS 区域，这部分页在 map_new_pages 分配时已经 kalloc + 清零，不需要从 bin 拷贝
        free_user_pagetable(newpt);
        return -1;
      }

      //构造新的陷阱帧trapframe
      //这里只是在栈上临时搭一个陷阱帧副本 ntf，还没有写进进程真正的 trapframe 页。
      struct trapframe ntf;
      memset(&ntf, 0, sizeof(ntf));
      ntf.epc = 0;// 程序入口地址设为0（用户程序从虚拟地址0开始执行）
      ntf.sp = stack_top;// sp初始值 = stack_top（栈向下增长）
      //把新页表、地址区间边界写进进程控制块 struct proc
      p->pagetable = newpt;//进程正式挂上刚建好的新用户页表；以后这个进程运行、trap、缺页 fault，都用这张页表翻译地址；
      p->data_start = data_start;
      p->image_end = image_end;
      p->guard_base = guard_base;
      p->stack_base = stack_base;
      p->heap_base = stack_top;
      p->sz = stack_top;//进程当前用户地址空间总大小
      // 写入进程的trapframe，保存epc/sp等寄存器上下文
      //边界成员全部存进 `struct proc`（lab3.h），后面堆扩展、fault 处理会读取它们
      //刚才在栈上准备好的 ntf 完整拷贝到进程真正的陷阱帧页面里
      *p->tf = ntf;
      //等到这个进程被调度、从内核态返回用户态时，内核就从 `p->tf` 取出寄存器，其中 `epc=0`、`sp=stack_top`，让程序开始跑
      // 释放原来旧的页表，free_user_pagetable 内部 uvmunmap(do_free=1)，连带释放旧数据页
      free_user_pagetable(oldpt);
      //带 PTE_U 的高地址映射：只清除 PTE，不会释放 trampoline 和 trapframe 物理页
      //是 exec 之前进程正在使用的旧页表（比如 exec 之前跑别的程序）
      return 0;
    }
    /* Descriptor/name is followed by its binary, then the next descriptor. */
    // 跳到下一个程序描述符
    entry = (uint64 *)((bin_end + 7) & ~7UL);//8 字节对齐（把地址向上对齐到 8 的倍数）
    //`_uprog_table`的布局：一个程序描述符 + 它的二进制镜像，紧跟着下一个描述符
  }
  return -1;
}
 
void
proc_init(void)
{
  proc_scheduler_init();
  struct proc *p = allocate();
  if (!p || proc_exec(p, "sh")) panic("load sh");
  dump_pagetable(p->pagetable);
}

int
proc_fork(void)
{
  struct proc *child = allocate();//分配一个新的 `struct proc` 子进程控制块，`allocate` 内部会调用 `proc_pagetable(child)`
  //提前给子建好根页表，自带 TRAMPOLINE + TRAPFRAME 两条高地址映射（和 exec 里的 proc_pagetable 一模一样）
  if (!child) return -1;
  //把父进程的地址区间边界原样拷贝给子进程：`data_start / image_end / guard_base / stack_base / heap_base / sz`
  //→ 父子的虚拟地址布局完全一致：代码段、数据段、guard 警戒区、栈、堆起点全部相同
  child->data_start = current->data_start;
  child->image_end = current->image_end;
  child->guard_base = current->guard_base;
  child->stack_base = current->stack_base;
  child->heap_base = current->heap_base;
  child->sz = current->sz;
  if (uvmcopy(current->pagetable, child->pagetable) < 0) {//递归拷贝用户侧映射
    release(child);//失败：`release(child)` 回收这个半成品子进程，返回 - 1
    return -1;
  }
  memcpy(child->tf, current->tf, sizeof(*child->tf));
  //子进程的 trapframe 初始寄存器和父完全一样：PC、sp、通用寄存器全部复刻，所以 fork 之后父子从同一条指令继续跑
  child->tf->a0 = 0;//RISC-V a0 用来存系统调用返回值
  child->parent = current->pid;
  child->tickets = current->tickets;
  child->level = current->level;
  return child->pid;
}

void
proc_exit(int status)
{
  if (current->pid == 1) panic("initial shell exited");
  acquire(current);
  current->status = status;
  current->state = ZOMBIE;
  current->pending = NONE;
  proc_wakeup((uint64)current->parent);
  swtch(&current->context, &cpu0.scheduler);
  panic("zombie resumed");
}

void
proc_fault(uint64 cause)
{
  if (current->pid == 1) panic("initial shell fault");
  acquire(current);
  current->status = 128 + (int)(cause & 0xff);
  current->state = ZOMBIE;//父进程随后 wait() 回收它，所以不是整个内核崩溃
  current->pending = NONE;
  proc_wakeup((uint64)current->parent);
  swtch(&current->context, &cpu0.scheduler);
  panic("faulted process resumed");
}

//扩展 / 收缩进程堆（heap）
long
proc_sbrk(long n)
{
  //current当前进程
  uint64 old = current->sz;//`current->sz` 就是堆断点（program break）
  if (n > 0) {//堆扩容，向高地址新增内存
    uint64 amount = (uint64)n;
    if (amount > TRAPFRAME - old) return -1;// 边界检查：不能一直长，不能冲到TRAPFRAME
    uint64 next = old + amount;
    if (uvmalloc(current, old, next) < 0) return -1;// 分配、建立PTE
    current->sz = next;//更新进程断点
  } else if (n < 0) {//堆缩容，回收高地址侧内存
    uint64 amount = (uint64)(-(n + 1)) + 1;//n绝对值
    if (amount > old - current->heap_base) return -1;//边界检查：不能缩到heap_base以下（堆不能缩过堆起始位置）
    uint64 next = old - amount;
    if (uvmdealloc(current, old, next) < 0) return -1;// 释放、清除PTE、kfree物理页
    current->sz = next;
  }
  // n == 0：什么都不做，直接返回当前断点old
  return (long)old;
}

int
proc_wait(struct proc *parent, int *status)
{
  int found = 0;
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &processes[i];
    acquire(p);
    if (p->state == UNUSED || p->parent != parent->pid) { release_lock(p); continue; }
    found = 1;
    if (p->state == ZOMBIE) {
      int pid = p->pid;
      *status = p->status;
      if (parent->tf->a0 && copyout(parent, parent->tf->a0, status, sizeof(*status)))
        { release_lock(p); return -1; }
      p->state = REAPING;
      release_lock(p);
      release(p);
      return pid;
    }
    release_lock(p);
  }
  return found ? -2 : -1;
}

int
proc_kill(int pid)
{
  if (pid <= 1) return -1;
  for (int i = 0; i < NPROC; i++)
    if (processes[i].pid == pid) {
      struct proc *p=&processes[i]; acquire(p);
      if (p->state == UNUSED) { release_lock(p); continue; }
      p->status = 130; p->state = ZOMBIE; p->pending = NONE;
      int parent=p->parent;
      release_lock(p);
      proc_wakeup((uint64)parent);
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

void
schedule(void)
{
  for (;;) {
    int chosen_level = 99;
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &processes[i]; acquire(p);
      if (p->state == RUNNABLE && p->level < chosen_level) chosen_level = p->level;
      release_lock(p);
    }
    intr_on();
    intr_off();
    int ran = 0;
    for (int step = 1; step <= NPROC; step++) {
      int i = (scan_cursor + step) % NPROC;
      struct proc *p = &processes[i]; acquire(p);
      if (p->state == RUNNABLE && p->level == chosen_level) {
        ran = 1; scan_cursor = i; current = p; cpu0.proc = p; p->state = RUNNING;
        p->last_slice_ticks = 0; slice_start = ticks;
        if (cpu0.noff != 1) panic("sched lock invariant");
        swtch(&cpu0.scheduler, &p->context);
        if (p->state == RUNNING) panic("sched running invariant");
        current = 0; cpu0.proc = 0;
      }
      release_lock(p);
    }
    if (!ran) { intr_on(); asm volatile("wfi"); intr_off(); }
  }
}

void proc_scheduler_init(void) { for (int i=0;i<NPROC;i++) processes[i].locked=0; }

void forkret(void) {
  if (current) release_lock(current);
  usertrapret();
}

void proc_yield(void) {
  struct proc *p=current; acquire(p);
  if (p->state == RUNNING) p->state=RUNNABLE;
  swtch(&p->context, &cpu0.scheduler);
  release_lock(p);
}

void proc_sleep(uint64 chan, volatile uint *lk) {
  struct proc *p=current; acquire(p); p->channel=chan; p->state=SLEEPING;
  if (lk) unlock(lk);
  swtch(&p->context, &cpu0.scheduler);
  p->channel=0; release_lock(p); if (lk) lock(lk);
}

void proc_wakeup(uint64 chan) {
  for (int i=0;i<NPROC;i++) { struct proc *p=&processes[i]; if(p==current) continue;
    acquire(p); if(p->state==SLEEPING && p->channel==chan) p->state=RUNNABLE; release_lock(p);
  }
}

void proc_timer_tick(void) {
  if (!current) return;
  acquire(current); current->cpu_ticks++;
  uint64 elapsed=ticks-slice_start; current->last_slice_ticks=elapsed;
  if (elapsed >= (uint64)(LAB2_TICK << current->level)) {
    if (current->level < 3) current->level++;
    current->state=RUNNABLE;
  }
  release_lock(current);
}

int proc_schedstat(int pid, uint64 *cpu, uint64 *last) {
  struct proc *p=0;
  if(pid==-1) p=current;
  else for(int i=0;i<NPROC;i++) if(processes[i].pid==pid && processes[i].state!=UNUSED) { p=&processes[i]; break; }
  if(!p) return -1;
  acquire(p);
  if(p->state==UNUSED || p->state==ZOMBIE || p->state==REAPING) { release_lock(p); return -1; }
  *cpu=p->cpu_ticks; *last=p->last_slice_ticks; release_lock(p); return 0;
}

int proc_settickets(int n) { if(n<1) return -1; acquire(current); current->tickets=n; release_lock(current); return 0; }
