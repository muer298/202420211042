# Lab5 设计记录

## 个性化参数与方案

学号为 `2024302111042`，因此 `LAB5_REFCNT_SCHEME=1`、`LAB5_KILL_SEMANTICS=0`、`LAB5_COW_BUDGET=194`。本实现把权威引用计数放在分配器的 `kmem.refcnt[]` 中。当前工程没有独立的 `kalloc.c`，物理页分配器位于 `kernel/vm.c`。

全局数组方案每个物理页使用一个 `uint16`，128 MiB RAM 对应 32768 页，数组占 64 KiB。引用计数的读取、递增和递减均为 O(1)，统计共享页数为 O(物理页数)。`kmem.lock` 同时保护空闲位图、引用计数和 COW 总拷贝数。

PTE 保留位方案只有 bit 8、bit 9 两位，COW 已占用 bit 8，无法单独表示任意数量共享者；且 `kfree(pa)` 只有物理地址，无法定位其他进程的 PTE。页表反向扫描不需要计数数组，但每次释放需要扫描进程页表，时间成本高，并容易在持有进程锁时形成跨页表锁序问题。因此本项目按学号使用全局数组方案作为实际计数方式。

## COW 状态快照

设父子共享一个原可写页 `PA0`。

1. `fork()` 完成：父子 PTE 都指向 `PA0`，都清除 `PTE_W` 并设置 `PTE_COW`；`PA0.refcnt=2`。
2. 子进程先写：`cow_alloc()` 发现引用数为 2，分配并复制到 `PA1`。子 PTE 改为指向 `PA1`、设置 `PTE_W`、清除 `PTE_COW`；`PA1.refcnt=1`。释放子对 `PA0` 的引用后，父 PTE 仍指向 `PA0` 且 `PA0.refcnt=1`。
3. 父进程随后写：它发现 `PA0.refcnt=1`，不再分配第三页，只在原地恢复 `PTE_W` 并清除 `PTE_COW`。最终只有 `PA0`、`PA1` 两页，各自 refcnt 为 1。

修改 PTE 后调用 `sfence_vma()`，避免旧 TLB 翻译继续允许写入共享页。

## 引用计数增减位置

| 场景 | 代码位置 | 操作 |
|---|---|---|
| 新页分配 | `kernel/vm.c:kalloc()` | 分配时 refcnt 置 1 |
| fork 共享页 | `kernel/vm.c:copy_user_level()` | 子 PTE 成功建立后 `kref_inc(pa)` |
| COW 实际复制 | `kernel/vm.c:cow_alloc()` | 新页由 kalloc 置 1；旧页经 kfree 减 1；增加全局实际复制计数 |
| 独占页 COW | `kernel/vm.c:cow_alloc()` | refcnt 为 1 时原地恢复写权限，不分配、不增加复制计数 |
| 解除用户映射 | `kernel/vm.c:uvmunmap()` / `free_user_level()` | `kfree()` 减引用，只有归零才填充 0x5a 并回收到位图 |

## 异常、内核写用户内存与配额

用户写 COW 页触发 `scause=15`，`kernel/trap.c:usertrap()` 在 `LAB5_KILL_SEMANTICS=0` 时先检查 killed 标志，再调用 `cow_alloc()`。未映射页、非用户页、只读代码页或分配失败都会走进程故障退出路径。

内核通过 `copyout()` 或写权限预检 `user_access()` 写用户缓冲区时不会触发硬件用户态缺页，因此 `kernel/vm.c` 会主动识别 `PTE_COW` 并调用 `cow_alloc()`。这保证 shell 在 fork 后仍能通过 `read()` 写回命令缓冲区。

每进程成功进行一次实际 COW 物理页复制，就增加 `cow_copies`；达到 194 时只打印一次告警并继续复制。`pmc(0)` 统计开机以来真实分配并复制的页数，独占原地恢复不计；`pmc(1)` 扫描并统计当前 `refcnt>1` 的物理页。

COW 只作用于用户叶映射。`uvmcopy()` 遍历 `[0, sz)` 且跳过无效空洞和不带 `PTE_U` 的映射，因此 trampoline 与 trapframe 不参与共享 COW。

## exec 参数栈

`sys_exec()` 从旧用户地址空间用 `copyin/copyinstr` 复制路径和参数字符串到临时内核页；非法指针或参数过多时释放临时页并返回错误。`proc_exec()` 为新程序创建新页表，向新用户栈压入参数字符串，再压入 `argv[0..argc]` 指针和 NULL 哨兵。最终 `sp` 向下按 16 字节对齐，`tf->a1=sp`，`tf->sp=sp`，并由 `proc_exec()` 返回 `argc`，系统调用分发将其放入 `tf->a0`。

## 验收命令与判据

在 Lab5 项目目录运行 `make test`，覆盖 Lab3/Lab4 回归与 Lab5 测试。进入 QEMU shell 后可手动运行：

- `bench`：Lab4 fork/exit/wait 与睡眠唤醒回归。
- `badaccess`、`memcap`、`vmtest`：非法访问隔离、OOM 与虚拟内存回归。
- `zerocopy`：fork 后立即 exec，`pmc(0)` 增量不超过 2。
- `cowpair`：父子数据隔离，实际复制增量恰好为 2。
- `echo hi`：打印 `hi` 并通过 argc/argv、NULL 哨兵和 16 字节对齐检查。
- `cowtest`：引用计数守恒、配额继续运行、多子进程清理、只读代码写保护、非法 exec 指针和参数栈。

## 现场推导要点

- 若把 trapframe 纳入用户 COW，用户态与内核态切换时 trampoline 需要访问的特权现场可能变为只读共享页，破坏陷入/返回；它是每进程独占的内核现场，不应作为普通用户数据页复制。
- 双核下 refcnt 的 `++/--` 若不互斥，两个 hart 可能同时读到 2 并各自减为 1，丢失一次更新；页面可能在仍被引用时被回收到空闲池并覆写，造成释放后使用。`kmem.lock` 串行化分配器计数，`cow_lock` 串行化 fork 的 COW PTE 建立与写缺页分离。
