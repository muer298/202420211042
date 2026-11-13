# Lab4：进程上下文切换与调度

学号：2024302111042，`LAB4_POLICY_ID = 2`，采用四级 MLFQ。项目以 Lab3 为基线，保留 SV39 页表、用户态陷入、fork/exec/wait、UART 和先前实验测试。

## 实现内容

- `kernel/swtch.S` 保存/恢复 `ra`、`sp`、`s0`–`s11`；每个进程新增内核上下文和进程锁，CPU 结构记录调度器上下文、当前进程、嵌套关中断计数和中断状态。
- `kernel/proc.c` 在 MLFQ 的最高优先级队列内轮转；时间片为 `LAB2_TICK × {1,2,4,8}`。本项目每次定时器中断递增一个 tick，`LAB2_TICK=2`，所以四档是 2/4/8/16 个中断。用完时间片降级，主动睡眠后唤醒保留队列等级。
- `sleep/wakeup`、`wait/exit` 与控制台读通过 channel 协调；子进程退出码通过 `copyout` 写回用户内存，退出进程在切换到调度器后回收。
- 新增系统调用 `schedstat(23)`、`time_rdtime(24)`、`setickets(25)`。`schedstat` 提供 CPU tick 和最近时间片观测，`time_rdtime` 读取硬件 `time` CSR；MLFQ 中 `setickets` 只保存设置值，不改变队列调度。
- 用户程序 `bench` 检查 fork/exit/wait 及并发回收；`mlfq_test` 检查四档时间片、定时器统计和新系统调用。

详细策略和 tick 口径见同目录的 `../调度设计说明.md` 与 Lab4 能力接口文档。

## 构建和运行

```sh
make clean && make
python3 check_expect.py 2024302111042
make test
make qemu
```

QEMU shell 中运行 `bench`、`mlfq_test`、`selftest`、`vmtest`、`pgtbl`、`badaccess`、`memcap` 或 `hi`。退出 QEMU：按 `Ctrl-a`，再按 `x`。
