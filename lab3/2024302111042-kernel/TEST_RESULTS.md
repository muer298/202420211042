# Lab3 V3.1 验证记录

验证日期：2026-10-14。环境：macOS、RISC-V 交叉工具链、QEMU virt RV64（Sstc）。

## 自动化结果

- `make clean && make`：通过；所有内核与 11 个用户程序从零构建成功。
- `python3 check_expect.py 2024302111042`：个性化 Banner 形状校验通过。
- `make test`：11/11 组 Lab3 集成、ABI 与打包检查通过。
- `python3 tests/test_lab2.py`：11/11 组 Lab2 完整回归通过。
- `make test-lab1`：6/6 组 Lab1 启动、PMP、栈与 Banner 回归通过。

| 检查项 | 结果 |
|---|---|
| 位图分配、降序顺序、清零、释放守恒 | PASS |
| 低内存条件下跨 L1 边界映射失败及完整回滚 | PASS |
| 内核 UART、PLIC、全部 RAM、trampoline 映射 | PASS |
| 首进程页表 L2/L1/LEAF 输出 ABI | PASS |
| 代码 `R-XU`、数据/栈 `RW-U`、trapframe `RW--`、trampoline `R-X-` | PASS |
| guard 无映射，写地址 0 的子进程被安全终止 | PASS，status=143 |
| sbrk 后越界读的子进程被安全终止 | PASS，status=141 |
| 跨页 copyin/write 与 EAGER sbrk 增长、收缩 | PASS |
| memcap 分配至 OOM，错误返回且既有页内容完好 | PASS，2037 个 64KB 块 |
| fork 深拷贝、wait 状态、exec 事务切换 | PASS |
| 连续 70 次创建、退出和回收，无页/栈生命周期错误 | PASS |
| Lab2 系统调用、定时抢占、UART、行缓冲与字符流 | PASS |
| 常规与超长输入注入脚本 | PASS，26/384 字节 |
| 4 hart 启动仅出现一个 Banner/Shell | PASS |
| Lab2/Lab3 赠送件及官方测试源 SHA-256/字节一致 | PASS |
| 用户 ELF 入口 0、数据页边界 4KB 对齐、镜像尺寸 | PASS |

测试检查输出中不存在 `PANIC` 或测试 `FAIL`。除正常用户 `ecall` 外，只有
`badaccess` 刻意触发的 load/store page fault（cause 13/15）被允许；它们均只终止
出错子进程，随后 shell 和 `hi` 仍正常运行。

## 手工复核结果

```text
LAB3-SELFTEST PASS allocator order=1 rollback
VMTEST PASS cross-page copy and sbrk
PGTBL PASS
TEST-3a PASS: wild write killed (status=143)
TEST-3b PASS: beyond-sbrk access killed (status=141)
TEST-4 PASS: OOM returned error after 2037 chunks; existing alloc intact; kernel alive
```

`rg` 审计系统调用和控制台路径后，所有用户指针访问只出现在
`user_access/copyin/copyout/copyinstr` 调用点，没有内核直接解引用用户虚拟地址。
构建仅保留基线链接脚本已有的内核 RWX LOAD 段警告；未隐藏该警告，用户页表实际权限
由运行时 dump 和非法写代码页测试共同验证。
