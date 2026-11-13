#include "lab3.h"

static struct proc processes[NPROC];
struct proc *current;
static int nextpid = 1;
static struct proc *deferred_reap;
static struct cpu cpu0;//单核，cpu0保存信息
static volatile uint ticks_lock;
static int scan_cursor = -1;
static uint64 slice_start;
static void
//先记录当前中断状态，再关闭中断并增加 noff
push_off(void)
{
  int enabled = intr_get();
  intr_off();
  if (cpu0.noff == 0) cpu0.intena = enabled;
  cpu0.noff++;
}

//pop_off() 减少 noff；只有退回最外层且进入前中断原本开启时，才重新打开中断
static void
pop_off(void)
{
  if (intr_get() || cpu0.noff < 1) panic("pop_off");
  cpu0.noff--;
  if (cpu0.noff == 0 && cpu0.intena) intr_on();
}

//acquire(p) 先调用 push_off()，再原子地获取 p->locked
static void
acquire(struct proc *p)
{
  push_off();
  while (__sync_lock_test_and_set(&p->locked, 1)) ;
  __sync_synchronize();
}

//release_lock(p) 释放 p->locked，再调用 pop_off()
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

static uint64 kstrlen(const char *s) {
  uint64 n=0; while (s[n]) n++; return n;
}

static void
release(struct proc *p)
{
  if (p->pagetable) free_user_pagetable(p->pagetable);
  if (p->tf) kfree(p->tf);
  if (p->kstack) kfree(p->kstack);
  memset(p, 0, sizeof(*p));
}

//在内核态运行
//新建进程、分配一个 PCB的函数
//fork（）调用该函数
//给新进程准备内核栈和初始 context
static struct proc *
allocate(void)
{
  struct proc *p = 0;
  //先找到一个 UNUSED 进程槽位，然后分配 trapframe、内核栈和用户页表
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
  p->level = 0;//队列级别初始化成0
  //新进程还没有保存过的内核现场，context 初始化
  //这个进程第一次被切换进来时，从 forkret() 开始执行；它使用自己的内核栈顶。
  p->context.ra = (uint64)forkret;//fork 出来的子进程是全新的 PCB，它从来没有跑过任何代码
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
proc_exec(struct proc *p, const char *name, char *const argv[], int argc)
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

      if (argc < 0 || argc > MAXARG) { free_user_pagetable(newpt); return -1; }
      uint64 ustack[MAXARG + 1];
      uint64 sp=stack_top;
      for (int i=0; i<argc; i++) {
        if (!argv || !argv[i]) { free_user_pagetable(newpt); return -1; }
        uint64 len=kstrlen(argv[i])+1;
        if (len > PGSIZE || sp < stack_base + len) {
          free_user_pagetable(newpt); return -1;
        }
        sp=(sp-len) & ~0xfUL;
        if (sp < stack_base || copyout(&shadow, sp, argv[i], len) < 0) {
          free_user_pagetable(newpt); return -1;
        }
        ustack[i]=sp;
      }
      ustack[argc]=0;
      uint64 argbytes=(uint64)(argc+1)*sizeof(uint64);
      if (sp < stack_base + argbytes) { free_user_pagetable(newpt); return -1; }
      sp=(sp-argbytes) & ~0xfUL;
      if (sp < stack_base || copyout(&shadow, sp, ustack, argbytes) < 0) {
        free_user_pagetable(newpt); return -1;
      }

      //构造新的陷阱帧trapframe
      //这里只是在栈上临时搭一个陷阱帧副本 ntf，还没有写进进程真正的 trapframe 页。
      struct trapframe ntf;
      memset(&ntf, 0, sizeof(ntf));
      ntf.epc = 0;// 程序入口地址设为0（用户程序从虚拟地址0开始执行）
      ntf.sp = sp;
      ntf.a0 = argc;
      ntf.a1 = sp;
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
      return argc;
    }
    /* Descriptor/name is followed by its binary, then the next descriptor. */
    // 跳到下一个程序描述符
    entry = (uint64 *)((bin_end + 7) & ~7UL);//8 字节对齐（把地址向上对齐到 8 的倍数）
    //`_uprog_table`的布局：一个程序描述符 + 它的二进制镜像，紧跟着下一个描述符
  }
  return -1;
}

//它先初始化进程锁，再调用 allocate() 创建进程，并用 proc_exec() 装入 shell。
void
proc_init(void)
{
  proc_scheduler_init();
  struct proc *p = allocate();
  if (!p || proc_exec(p, "sh", 0, 0) < 0) panic("load sh");
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

  //复制父进程地址空间和 trapframe
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
  child->level = current->level;//继承父亲级别
  return child->pid;
}

void
proc_exit(int status)
{
  if (current->pid == 1) panic("initial shell exited");
  acquire(current);
  current->status = status;
  current->state = ZOMBIE;//状态
  current->pending = NONE;
  proc_wakeup((uint64)current->parent);//然后用父进程 PID 调用 proc_wakeup()
  //这里的通道值是父进程 PID。
  //父进程被唤醒后重新检查子进程状态
  swtch(&current->context, &cpu0.scheduler);
  panic("zombie resumed");
}
//退出进程不会在 proc_exit() 里释放自己的内核栈，因为此时它还在该栈上运行。它先切到调度器，等父进程执行 wait() 时再回收

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
    // 情况1：这个表项是空的UNUSED，或者这个进程的父PID不是当前parent → 不是它的子进程，跳过
    found = 1;// 标记：父进程至少存在一个子进程
    // 找到了属于parent的子进程，并且这个子进程是ZOMBIE僵尸态！可以回收
    if (p->state == ZOMBIE) {
      int pid = p->pid;
      *status = p->status;// 取出子进程的退出状态码
      //如果父进程调用 wait() 时传入了状态地址，就把子进程的退出码复制到那个用户地址。
      // copyout：把内核里的status拷贝到用户态传入的status指针位置
      // parent->tf->a0 就是用户 wait(&status) 传进来的指针
      if (parent->tf->a0 && copyout(parent, parent->tf->a0, status, sizeof(*status)))
        { release_lock(p); return -1; }
      p->state = REAPING;// 标记为正在回收
      release_lock(p);
      release(p);// 释放这个僵尸进程的资源（PCB、内存等）
      return pid;// 返回子进程pid，上层SYS_wait拿到这个值就结束等待；找到僵尸子进程，回收成功
    }
    release_lock(p);
  }
  return found ? -2 : -1;//=1，存在子进程，但全部还活着；父进程没有任何子进程
}

int
proc_kill(int pid)
{
  if (pid <= 1) return -1;
  for (int i = 0; i < NPROC; i++)
    if (processes[i].pid == pid) {
      struct proc *p=&processes[i]; acquire(p);
      if (p->state == UNUSED || p->state == ZOMBIE || p->state == REAPING) {
        release_lock(p); continue;
      }
      p->killed=1; p->status=130;
      if (p->state == SLEEPING) p->state=RUNNABLE;
      release_lock(p);
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

//调度器带着进程锁切入
void
schedule(void)
{
  // 调度器死循环，永不退出，调度器一旦启动就无限循环，不停找可以跑的进程。
  for (;;) {
    int chosen_level = 99;
    //保证 MLFQ 规则：**优先跑高优先级（小 level）的就绪进程，低优先级只有高层没有就绪才会被选
    //遍历所有 PCB，拿锁看状态：只要是 `RUNNABLE` 就绪，并且它的 `level` 比当前 chosen_level 更小 → 更新 chosen_level
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &processes[i]; acquire(p);
      if (p->state == RUNNABLE && p->level < chosen_level) chosen_level = p->level;
      release_lock(p);
    }
    //设置中断状态，保证进入下面挑选进程阶段时，中断是关闭的，避免中断打断调度选择逻辑
    intr_on();
    intr_off();
    //第二轮扫描：在刚才选出的这个最优 level 内部，做轮询 RR
    int ran = 0;
    for (int step = 1; step <= NPROC; step++) {
      int i = (scan_cursor + step) % NPROC;
      //scan_cursor 轮询游标，实现轮转，避免每次都从 0 号进程开始，防止排在前面的进程一直霸占
      //从上次停下的位置往后遍历，环形遍历数组
      struct proc *p = &processes[i]; acquire(p);
      //**同一优先级内部，时间片轮转调度 RR**，完美匹配 MLFQ 定义：高层优先，同层 RR
      if (p->state == RUNNABLE && p->level == chosen_level) {
        ran = 1; scan_cursor = i; //更新游标：下次轮询从这个位置往后找
        current = p; cpu0.proc = p;//cpu结构标记：当前CPU正在跑这个进程
        p->state = RUNNING;
        p->last_slice_ticks = 0; //重置本次时间片计时
        slice_start = ticks;//记录这个时间片开始的tick时刻
        if (cpu0.noff != 1) panic("sched lock invariant");//断言检查锁的不变量：调度器的关中断嵌套计数必须等于 1，内核调试用，破坏就 panic 崩溃。
        swtch(&cpu0.scheduler, &p->context);//这行把 CPU 从调度器的 context 切到进程的 context。进程之后让出 CPU 时，反过来切回 cpu0.scheduler
        if (p->state == RUNNING) panic("sched running invariant");
        //正常切回来的时候，进程已经不再是 RUNNING了：要么变成 RUNNABLE（时间片到，放回就绪）、SLEEPING（阻塞）、ZOMBIE（退出）
        current = 0; cpu0.proc = 0;
      }
      release_lock(p);
    }
    if (!ran) { intr_on(); asm volatile("wfi"); intr_off(); }
    //当前没有任何 RUNNABLE 就绪进程可以跑，CPU 无事可做
    //刚才一轮找了一圈，没有任何就绪进程
    //wfi：RISC-V 硬件指令，Wait For Interrupt，CPU 进入低功耗等待，直到中断到来（时钟中断、IO 中断）唤醒 CPU。
    //中断来了之后退出 wfi，继续循环扫描进程。
  }
}

void proc_scheduler_init(void) { for (int i=0;i<NPROC;i++) processes[i].locked=0; }

//新进程第一次进入内核时的入口
//调度器切换进进程时持有进程锁，因此新进程先释放这把锁，再通过 usertrapret() 返回用户态。
void forkret(void) {
  if (current) release_lock(current);
  usertrapret();
  //fork 的语义：创建一个子进程，子进程在用户态继续执行，和父进程几乎一模一样的用户地址空间，在用户态拿到返回值 pid=0
  //上下文切换（swtch）只能在内核态里跑
  //swtch 只是恢复内核寄存器（s0-s11、sp、ra），它不能直接把 CPU 切回用户态
}

//运行中的进程怎样带锁切回调度器，普通让出 CPU
void proc_yield(void) {
  struct proc *p=current; acquire(p);//获取当前进程锁
  if (p->state == RUNNING) p->state=RUNNABLE;//若状态仍是 RUNNING，改为 RUNNABLE
  swtch(&p->context, &cpu0.scheduler);//切回调度器
  //切换点保存和恢复的是内核态 context；
  //被中断的用户寄存器由 trapframe 和 trampoline 保存、恢复。这也是为什么这条链路里会同时看到 trap 处理和 swtch()。
  release_lock(p);
}

//睡眠路径也采用相同的锁交接方式
//它把状态改为 SLEEPING，但不会增加 level；所以只有用完整个时间片才降级，主动等待不会降级。
void proc_sleep(uint64 chan, volatile uint *lk) {
  struct proc *p=current; acquire(p); p->channel=chan; p->state=SLEEPING;
  //把等待的事件编号 chan 保存到 p->channel
  if (lk) unlock(lk);
  swtch(&p->context, &cpu0.scheduler);//内核设置了 supervisor 定时器，trap.c 的timer_init()。定时器到期时，CPU 暂停原来的执行，进入中断处理。
  //1. 进程被唤醒并重新调度后，清除 channel 并释放锁。
  p->channel=0; release_lock(p); if (lk) lock(lk);
  //lk 参数用于需要交接外部锁的场景；当前 Lab4 的 pause、控制台读和 wait 调用都传入 0
}

//把符合条件的睡眠进程变成可运行
//唤醒只表示进程可以再次参与调度；真正运行仍由 schedule() 决定。
void proc_wakeup(uint64 chan) {
  for (int i=0;i<NPROC;i++) { struct proc *p=&processes[i]; if(p==current) continue;
    acquire(p); if(p->state==SLEEPING && p->channel==chan) p->state=RUNNABLE; release_lock(p);
  }
}

//判断时间片是否用完
void proc_timer_tick(void) {
  if (!current) return;
  acquire(current); current->cpu_ticks++;//增加 cpu_ticks，并用 ticks - slice_start 更新最近运行长度 last_slice_ticks
  uint64 elapsed=ticks-slice_start; current->last_slice_ticks=elapsed;
  //比较已运行 tick 和当前级别的时间片 LAB2_TICK << current->level，LAB2_TICK：2
  if (elapsed >= (uint64)(LAB2_TICK << current->level)) {
    if (current->level < 3) current->level++;
    current->state=RUNNABLE;
    //时间片用完时，级别加一（最高到 3），并把进程状态设为 RUNNABLE
  }
  release_lock(current);
}

//查询子进程累计运行 tick 和最近时间片长度
int proc_schedstat(int pid, uint64 *cpu, uint64 *last) {
  struct proc *p=0;
  if(pid==-1) p=current;
  else for(int i=0;i<NPROC;i++) if(processes[i].pid==pid && processes[i].state!=UNUSED) { p=&processes[i]; break; }
  if(!p) return -1;
  acquire(p);
  if(p->state==UNUSED || p->state==ZOMBIE || p->state==REAPING) { release_lock(p); return -1; }
  *cpu=p->cpu_ticks; //cpu_ticks：该进程从启动到现在，累计拿到多少个CPU tick
  *last=p->last_slice_ticks;//last_slice_ticks：上一次被时间片抢占换下时，所在MLFQ层级的时间片大小（2/4/8/16）
  release_lock(p); return 0;
}

int proc_settickets(int n) { if(n<1) return -1; acquire(current); current->tickets=n; release_lock(current); return 0; }
