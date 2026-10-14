# Lab3：物理页分配与 SV39 虚拟内存

学号：2024302111042。个性化参数：`LAB3_ALLOC_ORDER=1`（物理页降序分配）、
`LAB3_GUARD_PAGES=1`、`LAB3_MEMCAP_DELTA=2`。工程运行于 QEMU virt RV64，
保留 Lab1 的个性化启动输出和 Lab2 的陷入、系统调用、定时抢占及 UART 控制台能力。

## 四阶段实现路线

1. **物理页管理**：`kinit/kalloc/kfree` 使用位图记录 4KB 页，页号提示指针保证连续
   分配/回收不退化为平方复杂度。分配页清零，释放页填 `0x5a`；地址、对齐和重复释放
   均有检查。最高 `LAB3_MEMCAP_DELTA` 页不进入可分配集合。
2. **页表机制**：`walk` 完成 SV39 的 L2→L1→L0 遍历，按需建立中间页表；
   `mappages` 建立叶映射，失败时撤销本次叶项并剪除空的中间页表。启动自检会把空闲页
   压缩到两页，故意让一个跨 2MB 边界的两页映射失败，再核对空闲页数守恒。
3. **地址空间装配**：内核页表恒等映射 UART、PLIC 和 `KERNBASE..PHYSTOP`，另在
   `TRAMPOLINE` 映射跳板。用户镜像由构建脚本从 ELF 提取 `data_start/end`，代码与
   只读数据映射为 `R-XU`，数据/BSS 为 `RW-U`，guard 不建立 PTE，栈和堆为 `RW-U`；
   trapframe 为 `RW--`，trampoline 为 `R-X-`。
4. **进程与受控拷贝**：系统调用只把用户地址当数值，经 `user_access`、`copyin`、
   `copyout` 或 `copyinstr` 逐页检查 `V/U/R/W` 后访问物理页。`sbrk` 采用 EAGER
   分配并在 OOM 时回滚；`fork` 深拷贝全部用户叶页且跳过 guard；`exec` 先完整建立
   新页表，成功后一次提交，失败时原地址空间保持不变。

```text
低地址
0                 代码 + rodata (R-XU)
data_start        data + bss    (RW-U)
image_end         guard × 1     (无映射)
stack_base        用户栈         (RW-U)
heap_base=sz      堆向高地址增长 (RW-U, sbrk EAGER)
...
TRAPFRAME         陷入现场       (RW--)
TRAMPOLINE        共享跳板       (R-X-)
高地址
```

## 关键不变式与设计选择

- `mappages` 只负责页表项，映射目标物理页仍由调用者拥有；失败时调用者决定释放数据页。
  `uvmalloc` 会回收本次已分配的数据页，`proc_exec` 则销毁尚未提交的整个新地址空间。
- 撤销叶映射时先清 PTE，再把物理页交回分配器；这避免可访问映射短暂指向已复用页面。
- `fork` 不复制 trapframe/trampoline：子进程有自己的 trapframe 物理页，trampoline 是
  全系统共享的只读代码页。guard 本来就没有叶映射，遍历时自然跳过。
- `exec` 的页表、段页和栈全部成功后才替换 `p->pagetable`；因此 OOM 不会破坏旧程序。
- 软件在建 PTE 时预置 A 位，并对可写页预置 D 位；dump 的四位权限按 ABI 只显示
  `r/w/x/u`，不显示 A/D。
- 用户态陷入后第一步把 `stvec` 改回 `kernelvec`，返回前再改为 `uservec`；页表切换前后
  均由赠送跳板执行 `sfence.vma`，防止内核期间的时钟中断误入用户跳板。
- 时钟通过 `rdtime/stimecmp` CSR 工作，不访问 CLINT，因此内核页表不映射 CLINT。
  内核栈采用分配器物理页并依靠 RAM 恒等映射访问，没有采用 `KSTACK(p)` 高地址映射；
  这与 Lab0 图中的高地址栈方案不同，但仍由每进程独占并由 trapframe 保存栈顶。
- 系统为单核教学内核；额外 hart 在入口处停驻。僵尸进程若仍是当前执行上下文，先标记
  `REAPING`，切到其他进程后再释放内核栈，避免在自身栈上执行释放后的代码。
- 用户库的 `sbrklazy` 在本实验按 EAGER 处理，LAZY 缺页分配留到 Lab5。

## 构建、运行与验证

在本目录执行：

```sh
make clean && make
python3 check_expect.py 2024302111042
make test
python3 tests/test_lab2.py
make qemu
```

QEMU 的 shell 中可运行 `pgtbl`、`vmtest`、`badaccess`、`memcap`、`selftest` 和
Lab2 原有程序；退出 QEMU 使用 `Ctrl-a` 后按 `x`。`make test` 会覆盖页表输出权限、
跨页 copy、EAGER sbrk/OOM 回滚、非法访问隔离、进程回收、4 hart 单实例启动、
UART 两种输入语义以及赠送件哈希。日志写入被忽略的 `test-results/`。

`lab3-start` 指向 Lab2 完成快照，最终可交付快照使用 `lab3-submit`。
