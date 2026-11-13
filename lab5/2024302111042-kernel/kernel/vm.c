#include "lab3.h"

#define RAM_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)//空闲页表
#define BITBYTE(i) ((i) >> 3)//一个字节有 8 位，所以它算出页 i 的状态位位于位图的第几个字节
#define BITMASK(i) (1U << ((i) & 7))//i & 7 相当于 i % 8，算出该状态位在字节中的位置；1U << ... 则生成对应的位掩码

pagetable_t kernel_pagetable;
static struct {
  volatile uint lock;
  uchar freebits[(RAM_PAGES + 7) / 8];
  uint16 refcnt[RAM_PAGES];
  uint64 cow_copies;
} kmem;
//位图在这里约占 4KB，释放页只需更新一个位，避免大量释放时链表有序插入的高开销。
//freebits[0]这个字节，里面 8 个 bit，依次对应页号 0、1、2、3、4、5、6、7。如果最低位是 `1`：第 0 页空闲；置 0，就是分配出去了
static uint first_page, limit_page;
static uint64 free_count;
static int alloc_hint;

static void kmem_acquire(void) {
  while (__sync_lock_test_and_set(&kmem.lock, 1)) ;
  __sync_synchronize();
}
static void kmem_release(void) {
  __sync_synchronize();
  __sync_lock_release(&kmem.lock);
}
static int bit_get(uint i) { return (kmem.freebits[BITBYTE(i)] & BITMASK(i)) != 0; }
static void bit_set(uint i) { kmem.freebits[BITBYTE(i)] |= BITMASK(i); }
static void bit_clear(uint i) { kmem.freebits[BITBYTE(i)] &= (uchar)~BITMASK(i); }

void
kinit(void)
{
  //kernel_end 之前是内核代码、数据、BSS、跳板等，不能分配
  uint64 first = PGROUNDUP((uint64)kernel_end);// PGROUNDUP 将起始地址向上对齐到4KB；
  first_page = (uint)((first - KERNBASE) / PGSIZE);
  limit_page = (uint)RAM_PAGES;
  if (LAB3_MEMCAP_DELTA >= limit_page - first_page)
    panic("LAB3 memory cap");
  limit_page -= LAB3_MEMCAP_DELTA;//顶部保留两个物理页
  memset(&kmem, 0, sizeof(kmem));//初始化空闲位图与引用计数
  for (uint i = first_page; i < limit_page; i++) bit_set(i);//设置位图
  free_count = limit_page - first_page;
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = (int)first_page;
#else
  alloc_hint = (int)limit_page - 1;//降序分配物理页
#endif
}

uint64 kfree_pages(void) { kmem_acquire(); uint64 n=free_count; kmem_release(); return n; }

//分配一页物理内存
void *
kalloc(void)
{
  kmem_acquire();
  int found = -1;
#if LAB3_ALLOC_ORDER == 0
  for (int i = alloc_hint; i < (int)limit_page; i++)
    if (bit_get((uint)i)) { found = i; break; }
#elif LAB3_ALLOC_ORDER == 1
  for (int i = alloc_hint; i >= (int)first_page; i--)
    if (bit_get((uint)i)) { found = i; break; }//位图置1空闲
#else
  uint mid = first_page + (limit_page - first_page) / 2;
  for (uint d = 0; d < limit_page - first_page; d++) {
    uint i = (d & 1) ? mid - (d + 1) / 2 : mid + d / 2;
    if (i >= first_page && i < limit_page && bit_get(i)) { found = (int)i; break; }
  }
#endif
  if (found < 0) { kmem_release(); return 0; }//如果找不到空闲页，返回空指针，而不是 panic
  //这是 OOM 能够安全传递到 sbrk、fork、exec 的基础
  bit_clear((uint)found);//清0位图该位，表示不空闲
  if (kmem.refcnt[found] != 0) panic("kalloc refcnt not zero");
  kmem.refcnt[found] = 1;
  free_count--;
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = found + 1;
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = found - 1;//更新下一次搜索位置，之前已经搜索过的地方就不再搜索
#endif
  void *p = (void *)(KERNBASE + (uint64)(uint)found * PGSIZE);//计算物理地址
  kmem_release();
  memset(p, 0, PGSIZE);//整页清零
  return p;
}

//释放一页物理内存，0x5a填充
void
kfree(void *p)
{
  uint64 pa = (uint64)p;
  //* 指针不为空；地址4KB对齐；地址处于可管理区间；对应位不是已经空闲，防止 double free
  if (!p || pa % PGSIZE || pa < KERNBASE + (uint64)first_page * PGSIZE ||
      pa >= KERNBASE + (uint64)limit_page * PGSIZE)
    panic("kfree address");
  uint i = (uint)((pa - KERNBASE) / PGSIZE);
  kmem_acquire();
  if (bit_get(i) || kmem.refcnt[i] == 0) panic("kfree: refcnt underflow");
  kmem.refcnt[i]--;
  if (kmem.refcnt[i] != 0) { kmem_release(); return; }
  memset(p, 0x5a, PGSIZE);//仅最后一个引用释放时 poison
  //某段代码释放页面后继续通过悬挂指针读取，开发过程中出现过返回地址变成 0x5a5a5a...
  bit_set(i);//置为空闲
  free_count++;
#if LAB3_ALLOC_ORDER == 0
  if ((int)i < alloc_hint) alloc_hint = (int)i;
#elif LAB3_ALLOC_ORDER == 1
  if ((int)i > alloc_hint) alloc_hint = (int)i;
#endif
  kmem_release();
}

static uint pa_index(uint64 pa) {
  if (pa % PGSIZE || pa < KERNBASE + (uint64)first_page * PGSIZE ||
      pa >= KERNBASE + (uint64)limit_page * PGSIZE) panic("kref address");
  return (uint)((pa - KERNBASE) / PGSIZE);
}

void kref_inc(uint64 pa) {
  uint i=pa_index(pa); kmem_acquire();
  if (bit_get(i) || kmem.refcnt[i] == 0 || kmem.refcnt[i] == 0xffff)
    panic("kref_inc invalid");
  kmem.refcnt[i]++; kmem_release();
}

int kref_get(uint64 pa) {
  uint i=pa_index(pa); kmem_acquire();
  int n=kmem.refcnt[i]; kmem_release(); return n;
}

uint64 kshared_count(void) {
  uint64 n=0; kmem_acquire();
  for (uint i=first_page;i<limit_page;i++) if (kmem.refcnt[i]>1) n++;
  kmem_release(); return n;
}

uint64 kcow_copy_count(void) {
  kmem_acquire(); uint64 n=kmem.cow_copies; kmem_release(); return n;
}

void kcow_copy_record(void) {
  kmem_acquire(); kmem.cow_copies++; kmem_release();
}

void kref_selftest(void) {
  uint64 free0=kfree_pages();
  uchar *p=kalloc();
  if (!p || kref_get((uint64)p)!=1 || kfree_pages()!=free0-1)
    panic("kref selftest alloc");
  p[0]=0x37;
  kref_inc((uint64)p); kref_inc((uint64)p);
  if (kref_get((uint64)p)!=3 || kshared_count()!=1) panic("kref selftest share");
  kfree(p); if (kref_get((uint64)p)!=2 || p[0]!=0x37) panic("kref selftest release 1");
  kfree(p); if (kref_get((uint64)p)!=1 || p[0]!=0x37) panic("kref selftest release 2");
  kfree(p);
  if (kfree_pages()!=free0 || kshared_count()!=0) panic("kref selftest balance");
  printf("LAB5-SELFTEST PASS refcnt alloc/share/release balance\n");
}
//页表生命周期管理
//SV39 页表——虚拟地址如何变成物理地址
//一个虚拟地址分成三级索引和 12 位页内偏移；PX(level, va) 从地址中提取每级索引。
//页表项中 V=1 且 R/W/X 都为 0 时，在本实现里表示指向下一级页表；有读、写或执行权限位的项则表示叶映射
/* Return whether this table still contains mappings; free empty descendants. */
//返回当前这一级页表是否还存有有效映射；同时释放已经空了的下级子页表
//不能一股脑直接把整棵树删掉：一部分虚拟地址可能还在用
static int
prune(pagetable_t pt, int level)
{
  int used = 0;
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;//无效跳过
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      used = 1;//叶 PTE 表示仍有实际映射，页表不为空
      //只要存在任意一个叶 PTE，当前这张页表就不能被回收，置 `used=1`
      continue;
    }
    //非叶节点，指向子页表，`V=1`、R/W/X 全 0
    pagetable_t child = (pagetable_t)PTE2PA(pte);
    if (level > 0 && !prune(child, level - 1)) {//递归进入子页表修剪，递归返回 `0`：子页表里面一个叶映射都没有，是空的
      pt[i] = 0;//把父节点这条 PTE 清空，切断对子页表的引用
      kfree(child);
    } else {
      used = 1;//子页表里还有有效映射，不能删子页表→ 父这条 PTE 必须保留，所以本级标记 `used=1`
    }
  }
  return used;
}

//SV39 三级页表
//查虚拟地址 va对应的 PTE 返回 L0 的 PTE 指针，供调用者检查或修改 可能新建中间页表
//可选：找不到下级页表时，可以自动分配下级中间页表（由第三个参数 `alloc` 控制）
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA) return 0;
  //一次 walk 最多创建两个中间页表
  pagetable_t created[2];
  pte_t *parents[2];
  int ncreated = 0;
  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      if (*pte & (PTE_R | PTE_W | PTE_X)) return 0;//不合法中间页表
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc) return 0;
      //alloc = 0：中间页表不存在就失败；alloc = 1：中间页表不存在就调用 kalloc 创建
      pagetable_t next = kalloc();
      if (!next) {
        while (ncreated) {
          //如果创建下一层时 kalloc() 失败，就反向处理：这里必须先清父 PTE，再释放子页表，否则父页表会短暂保留一个指向已释放物理页的有效项
          ncreated--;
          *parents[ncreated] = 0;
          kfree(created[ncreated]);
        }
        return 0;
      }
      parents[ncreated] = pte;
      created[ncreated++] = next;
      *pte = PA2PTE(next) | PTE_V;//打上有效标记
      pagetable = next;
    }
  }
  //一个指针，指向 L0 层的 PTE 结构体
  return &pagetable[PX(0, va)];//它只遍历 L2 和 L1，最终返回 L0 页表里的 PTE 地址，而不是最终物理地址
  //返回 PTE 指针的原因是调用方需要修改它：mappages 往里面写映射；uvmunmap 把它清零；copyin 读取权限和物理页号；fork 读取原页权限；dump_pagetable 输出内容。 
}

//表示把连续虚拟区间：[va, va + size) 映射到连续物理区间[pa, pa + size)
//逐页写入映射 每一轮循环里，给当前这个虚拟页分配绑定一个物理页，物理页是上层提前准备好传给 mappages 的
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 perm)
{
  //检查地址和大小是否按页对齐
  if (!size || va % PGSIZE || pa % PGSIZE || size % PGSIZE ||
      va >= MAXVA || size > MAXVA - va)
    return -1;
  uint64 start = va;
  for (uint64 done = 0; done < size; done += PGSIZE) {
    pte_t *pte = walk(pagetable, va + done, 1);//允许创建中间页表
    //// 两种失败场景进入回滚分支：
    // ① pte == NULL：walk内部分配中间页表时kalloc失败；walk已经清理了它自己临时创建的中间页
    // ② (*pte & PTE_V) != 0：这个va对应的PTE已经是有效项，重复映射，禁止静默覆盖，触发回滚
    if (!pte || (*pte & PTE_V)) {
      for (uint64 undo = 0; undo < done; undo += PGSIZE) {
        // 回滚第一步：undo 遍历【本次循环已经成功写入的那些页】，清零它们的叶PTE
        pte_t *old = walk(pagetable, start + undo, 0);// alloc=0：只查找，不要新建任何页表
        if (old) *old = 0;//清除PTE也就是映射
      }
      // 回滚第二步：prune 递归清理页表树
      // 很多中间页表现在下面所有叶PTE都被清零变空了，prune释放这些空的中间页表页
      prune(pagetable, 2);
      return -1;
    }
    //全部映射预置PTE_A；带写权限perm则额外预置PTE_D
    uint64 ad = PTE_A | ((perm & PTE_W) ? PTE_D : 0);//Accessed 访问位，Dirty 脏位
    *pte = PA2PTE(pa + done) | perm | ad | PTE_V;//把目标物理地址转成 PTE 里的 PPN 字段；传入的权限位（R/W/X）；Valid 有效位
  }
  return 0;
}
//维护页表结构而非直接管理数据页
//mappages
//  ├─负责：PTE、中间页表、映射结构
//  └─不负责：调用者传入的数据页

//uvmalloc / proc_exec
//  └─负责：自己分配的数据页

//解除映射，删除用户数据页
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  va = PGROUNDDOWN(va);
  for (uint64 i = 0; i < npages; i++, va += PGSIZE) {
    pte_t *pte = walk(pagetable, va, 0);//只读不新建中间页表
    // 过滤条件：
    // pte为空  OR  PTE无效(V=0) OR 不是叶PTE（R/W/X全0，只是中间页表节点）→ 跳过
    if (!pte || !(*pte & PTE_V) || !(*pte & (PTE_R | PTE_W | PTE_X))) continue;
    //从叶PTE拿到它映射的物理地址
    uint64 pa = PTE2PA(*pte);
    //先清零PTE，撤销映射（注释重点：先失效PTE，再释放物理页）
    //不能反过来。这既可能导致数据损坏，也可能造成跨进程信息泄露。
    *pte = 0; /* Invalidate before the physical page becomes reusable. */
    //do_free非0，才释放这个数据物理页；do_free=0只删映射、页保留
    if (do_free) kfree((void *)pa);
  }
  //全部页面的叶PTE清理完毕后，prune递归修剪，回收空的下级中间页表
  prune(pagetable, 2);
}

//阶段四：给用户进程建独立地址空间
//建立两项固定高地址映射
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pt = kalloc();
  if (!pt) return 0;
  //所有进程的页表都映射到同一个物理 trampoline，不需要每个进程单独拷贝一份
  if (mappages(pt, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0 ||//映射到共享跳板代码，权限为 RX，也没有 PTE_U
  //全局唯一
      mappages(pt, TRAPFRAME, PGSIZE, (uint64)p->tf, PTE_R | PTE_W) < 0) {//映射到当前进程自己的 trapframe，权限为 RW，但没有 PTE_U
  //当前这个进程 p 自己专属的 trapframe 物理页指针。
    /* Non-user leaves are only detached; the owned trapframe is not freed. */
    //因此用户态代码不能直接访问这两页
    free_user_pagetable(pt);//这两个不带 PTE_U 的内核侧映射，`free_user_pagetable` 在清理时只会把 PTE 清零（detach），不会 kfree 这两个物理页；
    return 0;
  }
  return pt;
}

static void
free_user_level(pagetable_t pt, int level)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      pt[i] = 0;
      if (pte & PTE_U) kfree((void *)PTE2PA(pte));
    } else {
      pagetable_t child = (pagetable_t)PTE2PA(pte);
      if (level > 0) free_user_level(child, level - 1);
      pt[i] = 0;
      kfree(child);
    }
  }
}

void
free_user_pagetable(pagetable_t pt)
{
  if (!pt) return;
  free_user_level(pt, 2);
  kfree(pt);
}

static int
copy_user_level(pagetable_t src, pagetable_t dst, int level, uint64 base)
{
  for (int i=0; i<512; i++) {
    pte_t pte=src[i];
    if (!(pte & PTE_V)) continue; // guard 页等空洞不复制
    uint64 va=base | ((uint64)i << PXSHIFT(level));
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      if (!(pte & PTE_U)) continue; // trampoline/trapframe 不参与 COW
      uint64 pa=PTE2PA(pte);
      uint64 flags=PTE_FLAGS(pte) & ~(PTE_V | PTE_A | PTE_D);
      if (flags & PTE_W) {
        flags=(flags & ~PTE_W) | PTE_COW;
        src[i]=PA2PTE(pa) | flags | PTE_V;
      }
      if (mappages(dst, va, PGSIZE, pa, flags) < 0) return -1;
      kref_inc(pa);
    } else if (level > 0 &&
               copy_user_level((pagetable_t)PTE2PA(pte), dst, level-1, va) < 0) {
      return -1;
    }
  }
  return 0;
}

static volatile uint cow_lock;
static void cow_acquire(void) {
  while (__sync_lock_test_and_set(&cow_lock, 1)) ;
  __sync_synchronize();
}
static void cow_release(void) {
  __sync_synchronize();
  __sync_lock_release(&cow_lock);
}

int
uvmcopy(pagetable_t src, pagetable_t dst)
{
  cow_acquire();
  int result=copy_user_level(src, dst, 2, 0);
  sfence_vma(); // 父进程可写 PTE 已被改成 COW 只读
  cow_release();
  return result;
}

int
cow_alloc(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA) return -1;
  uint64 va0=PGROUNDDOWN(va);
  cow_acquire();
  pte_t *pte=walk(pagetable, va0, 0);
  if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U) ||
      !(*pte & (PTE_R | PTE_W | PTE_X))) {
    cow_release(); return -1;
  }
  if (*pte & PTE_W) { cow_release(); return 0; }
  if (!(*pte & PTE_COW)) { cow_release(); return -1; }

  uint64 oldpa=PTE2PA(*pte);
  int refs=kref_get(oldpa);
  if (refs < 1) { cow_release(); return -1; }
  uint64 flags=PTE_FLAGS(*pte);
  if (refs == 1) {
    *pte=PA2PTE(oldpa) | ((flags & ~PTE_COW) | PTE_W | PTE_D);
    sfence_vma();
    cow_release();
    return 0;
  }

  void *newpage=kalloc();
  if (!newpage) { cow_release(); return -1; }
  memcpy(newpage, (void *)oldpa, PGSIZE);
  *pte=PA2PTE((uint64)newpage) | ((flags & ~PTE_COW) | PTE_W | PTE_D);
  kcow_copy_record();
  kfree((void *)oldpa);
  if (current) {
    current->cow_copies++;
    if (current->cow_copies >= LAB5_COW_BUDGET && !current->cow_budget_warned) {
      current->cow_budget_warned=1;
      printf("[cow] pid=%d reached LAB5_COW_BUDGET=%d, downgrade: continue copying\n",
             current->pid, LAB5_COW_BUDGET);
    }
  }
  sfence_vma();
  cow_release();
  return 0;
}

//给一段连续虚拟地址区间 `[start, end)`，逐页分配全新物理页，并且建立映射
static int
map_fresh_range(pagetable_t pt, uint64 start, uint64 end, uint64 perm)
{
  uint64 mapped = 0;//成功映射的字节，失败时用于回滚
  for (uint64 va = start; va < end; va += PGSIZE) {
    void *page = kalloc();
    // 两种失败情况：kalloc拿不到内存 或者 mappages建映射失败
    if (!page || mappages(pt, va, PGSIZE, (uint64)page, perm) < 0) {
      if (page) kfree(page);// 如果刚才kalloc成功、只是mappages炸了，要释放这个刚拿到的页
      uvmunmap(pt, start, mapped / PGSIZE, 1);// 回滚：把前面已经映射好的 [start, start+mapped) 全部撤销、并且kfree物理页（最后参数1=do_free）
      return -1;
    }
    mapped += PGSIZE;// 当前这一页成功，累计成功映射长度
  }
  return 0;
}

int
uvmalloc(struct proc *p, uint64 oldsz, uint64 newsz)
{
  if (newsz < oldsz || newsz >= TRAPFRAME) return -1;
  uint64 first = PGROUNDUP(oldsz), end = PGROUNDUP(newsz);
  if (end < first) return -1;
  return map_fresh_range(p->pagetable, first, end, PTE_R | PTE_W | PTE_U);
}

int
uvmdealloc(struct proc *p, uint64 oldsz, uint64 newsz)
{
  if (newsz > oldsz || newsz < p->heap_base) return -1;
  uint64 first = PGROUNDUP(newsz), end = PGROUNDUP(oldsz);
  if (end > first) uvmunmap(p->pagetable, first, (end - first) / PGSIZE, 1);//删除映射：删除页，释放内存
  return 0;
}

int user_range(uint64 va, uint64 n) { return va < MAXVA && n <= MAXVA - va; }
//这片区间不能侵入内核高地址（TRAMPOLINE/TRAPFRAME 那一侧），必须落在用户可用地址区间内


//sys_write 在输出前预检缓冲区，之后通过 copyin() 取数据；控制台 read 检查目标区可写，再用 copyout() 写回；exec 用 copyinstr() 读入程序名，wait 用 copyout() 写回状态。
/* Check every page before a syscall starts doing externally visible work. */
//系统调用前的用户地址合法性检查函数
int
user_access(struct proc *p, uint64 va, uint64 n, int write)
{
  if (!user_range(va, n)) return 0;//判断这个区间是不是合法的用户地址范围
  if (!n) return 1;
  uint64 last = PGROUNDDOWN(va + n - 1);//算出这个区间最后一页的页号（向下页对齐）
  for (uint64 page = PGROUNDDOWN(va); ; page += PGSIZE) {
    // walk：在进程p的页表里，查找page这个VA对应的PTE；第三个参数0代表【不自动创建下级页表】，只查询
    pte_t *pte = walk(p->pagetable, page, 0);
    if (write && pte && (*pte & PTE_V) && !(*pte & PTE_W) &&
        (*pte & PTE_COW)) {
      if (cow_alloc(p->pagetable, page) < 0) return 0;
      pte = walk(p->pagetable, page, 0);
    }
    uint64 need = PTE_V | PTE_U | (write ? PTE_W : PTE_R);
    // 失败条件三选一：
    // ① pte == NULL：找不到PTE（这个虚拟页没有映射，是空洞）
    // ② (*pte & need) != need：缺少必须的权限位
    // ③ 同时必须保证至少有 R/W/X 其中一种（是合法叶PTE）
    if (!pte || (*pte & need) != need || !(*pte & (PTE_R | PTE_W | PTE_X)))
      return 0;
    if (page == last) break;//检查到最后一页
  }
  return 1;//全部页面校验通过，地址合法
}

//在内核缓冲区 和 用户虚拟地址缓冲区之间搬运字节
//`out = 1`：内核 → 用户（输出，比如 read 把数据写给用户缓冲区）
//`out = 0`：用户 → 内核（输入，比如 write 从用户缓冲区读数据进来）
//一边校验页表权限，一边逐页拷贝数据
static int
transfer(struct proc *p, void *kernel, uint64 user, uint64 n, int out, int require_user)
{
  //整体范围检查，不能跑到内核高地址
  if (!user_range(user, n)) return -1;
  uchar *k = kernel;//内核侧缓冲区指针
  while (n) {// 还有字节要拷贝就循环
    uint64 pageva = PGROUNDDOWN(user);// 当前用户地址所在页的页起始VA
    pte_t *pte = walk(p->pagetable, pageva, 0);// 查询这个VA对应的PTE，不新建页表
    if (out && require_user && pte && (*pte & PTE_V) &&
        !(*pte & PTE_W) && (*pte & PTE_COW)) {
      if (cow_alloc(p->pagetable, pageva) < 0) return -1;
      pte = walk(p->pagetable, pageva, 0);
    }
    if (!pte || !(*pte & PTE_V) || !(*pte & (PTE_R | PTE_W | PTE_X)) ||
        (require_user && !(*pte & PTE_U)) ||
        (out && require_user && !(*pte & PTE_W)) ||
        (!out && require_user && !(*pte & PTE_R)))
      return -1;
    //当前这一页还剩多少字节可以拷贝
    uint64 count = PGSIZE - (user - pageva);
    if (count > n) count = n;// 剩下要拷贝的不足一页，就只拷剩余n字节
    // 算出用户虚拟地址对应的物理地址：PTE拿到PA + 页内偏移
    uchar *physical = (uchar *)PTE2PA(*pte) + (user - pageva);
    if (out) memcpy(physical, k, count); else memcpy(k, physical, count);//内核可以直接读写物理地址
    k += count; user += count; n -= count;// 内核指针前进；用户虚拟地址前进；剩余待拷贝字节减少
  }
  return 0;
}

//用户内存 → 内核缓冲区
int copyin(struct proc *p, void *dst, uint64 src, uint64 n) { return transfer(p, dst, src, n, 0, 1); }
//内核缓冲区 → 用户内存
int copyout(struct proc *p, uint64 dst, const void *src, uint64 n) { return transfer(p, (void *)src, dst, n, 1, 1); }
//内核 → 进程虚拟地址，和 copyout 一样是往用户侧写，`exec` 加载程序的时候，要填充进程地址空间里的代码段数据段
//用户仅发起 exec 系统调用请求，加载工作由内核完成
//连接用户程序生成elf转为.bin将元数据和二进制一起嵌入内核
int uvm_load_bytes(struct proc *p, uint64 dst, const void *src, uint64 n) { return transfer(p, (void *)src, dst, n, 1, 0); }

//从用户态读取以 `\0` 结尾的 C 字符串（比如文件名）
int
copyinstr(struct proc *p, char *dst, uint64 src, uint64 max)
{
  for (uint64 i = 0; i < max; i++) {
    // 一次拷贝1字节：src+i（用户） → dst+i（内核）
    if (copyin(p, dst + i, src + i, 1)) return -1;
    if (!dst[i]) return 0;// 读到 '\0' 字符串结束符，成功返回
  }
  return -1;// 循环跑完max字节还没遇到\0，字符串过长，失败
}

//页表打印调试函数，把整张页表的所有有效 PTE 打印出来
static void
dump_level(pagetable_t pt, int level, uint64 base)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V)) continue;
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    uint64 pa = PTE2PA(pte);
    if (pte & (PTE_R | PTE_W | PTE_X)) {
      char perm[5] = {
        (pte & PTE_R) ? 'r' : '-', (pte & PTE_W) ? 'w' : '-',
        (pte & PTE_X) ? 'x' : '-', (pte & PTE_U) ? 'u' : '-', 0
      };
      printf("LEAF va=%lx pa=%lx perm=%s\n", va, pa, perm);
    } else {
      printf("L%d va=%lx pa=%lx perm=----\n", level, va, pa);
      if (level > 0) dump_level((pagetable_t)pa, level - 1, va);
    }
  }
}

void
dump_pagetable(pagetable_t pt)
{
  printf("DUMP-PAGETABLE begin\n");
  dump_level(pt, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}

void
vm_selftest(void)
{
  uint64 before = kfree_pages();
  void *a = kalloc(), *b = kalloc(), *c = kalloc();//连续分配三页
  if (!a || !b || !c || (uint64)a % PGSIZE ||//检查地址4KB对齐
#if LAB3_ALLOC_ORDER == 1
      !((uint64)a > (uint64)b && (uint64)b > (uint64)c) ||//3. 检查降序关系 a > b > c；
#else
      0 ||
#endif
      *(volatile uint64 *)a != 0)//检查新页为零
    panic("allocator selftest");
  kfree(a); kfree(b); kfree(c);
  if (kfree_pages() != before) panic("allocator conservation");//释放后检查空闲页数量守恒

  pagetable_t pt = kalloc();
  void *held = 0;
  //把空闲页压到只剩 2 张，不断 `kalloc` 拿页，串成链表挂在 `held`；直到全局空闲页数 = 2
  //第一页：只需要 L0 页表，消耗 1 张空闲页；
  //第二页：walk 需要新建 L1 中间页表，此时空闲只剩 1 张，kalloc 失败，walk 返回 NULL，触发 mappages 回滚。
  while (kfree_pages() > 2) {
    void *page = kalloc();
    if (!page) panic("rollback setup");
    *(void **)page = held;
    held = page;
  }
  uint64 low = kfree_pages();//返回当前系统空闲物理页总数
  //2MB - 4KB和2MB
  //条件1:竟然分配成功了
  //条件2:mappages返回失败了，但是空闲页数量和进入前low不一致。说明回滚不干净，有页表页泄漏（L0或者L1页表没有被prune回收），这就是bug
  if (mappages(pt, (1UL << 21) - PGSIZE, 2 * PGSIZE,
               KERNBASE, PTE_R | PTE_W) != -1 || kfree_pages() != low)
    panic("mappages rollback");
  //释放根页表
  //把前面我们吃掉、串在 `held` 链表上的所有页面全部归还，恢复到测试前的空闲数量，最后全局空闲页数和 `before` 对比，保证整体守恒。
  kfree(pt);
  while (held) {
    void *next = *(void **)held;
    kfree(held);
    held = next;
  }
  if (kfree_pages() != before) panic("rollback conservation");
  printf("LAB3-SELFTEST PASS allocator order=%d rollback\n", LAB3_ALLOC_ORDER);//分配器检测通过
}


//阶段三：建立内核页表并开启分页
void
vm_init(void)
{
  kernel_pagetable = kalloc();//分配内核根页表（Sv39根页表，L2）内核只有一张全局内核页表，是所有内核地址翻译的根
  //它全程一直在线，不会被 uvmunmap 释放。
  if (!kernel_pagetable) panic("kernel page table");
  //恒等映射是指虚拟地址等于物理地址 内核直接用物理地址当作虚拟地址访问硬件 / 内存。
  if (mappages(kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0 ||//RW 内核直接读写串口寄存器
      mappages(kernel_pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W) < 0 ||//RW 内核访问中断控制器寄存器
      mappages(kernel_pagetable, KERNBASE, PHYSTOP - KERNBASE,//必须映射到 PHYSTOP，不能只映射到 kernel_end。
               KERNBASE, PTE_R | PTE_W | PTE_X) < 0 ||//RWX 访问内核、空闲物理页、用户数据页、页表等 RAM
               //说明书指出当前 kernel.ld 没有导出 etext 等边界，所以无法方便地区分：内核代码；只读数据；可写数据；空闲物理内存。因此本实验允许阶段性统一映射为 RWX。用户空间仍严格区分代码和数据权限。
      mappages(kernel_pagetable, TRAMPOLINE, PGSIZE,//RX 执行用户态和内核态切换所需的陷阱入口代码
               (uint64)trampoline, PTE_R | PTE_X) < 0)
    panic("kernel mappings");
  sfence_vma();//当前是单核实验，所以刷新当前 hart 即可；多核系统还需要让其他 hart 同步刷新各自 TLB。
  w_satp(MAKE_SATP(kernel_pagetable));//把 SV39 模式号和根页表的物理页号组合起来，把 Sv39 模式标记 + 根页表的物理页号打包成 satp 寄存器的值。
  //把它写入 satp，分页开始生效
  sfence_vma();//前后的 sfence_vma() 则刷新 TLB，避免继续使用旧的地址转换缓存。防止硬件继续使用过期的翻译缓存，产生错误访问。
}
