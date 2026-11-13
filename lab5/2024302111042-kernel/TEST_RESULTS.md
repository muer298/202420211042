# Lab4 验证记录

验证日期：2026-10-26。环境：macOS、RISC-V 交叉工具链、QEMU virt RV64，单核运行。

## 结果

- `make clean && make`：通过，内核与全部用户程序完成构建。
- `python3 check_expect.py 2024302111042`：通过，学号 Banner 校验通过。
- `make test`：通过，Lab3 回归 11/11，Lab4 新增测试 2/2。
- `bench`：输出非零 `RESULT avg_fork_wait`、`RESULT concurrent_children = 8`、`PASS: bench`（耗时数值随运行环境变化）。
- `mlfq_test`：输出 `RESULT mlfq_observations = 37`、`RESULT mlfq_quantum_mask = 15`、`PASS: mlfq timer accounting and process lifecycle`。掩码 15 表示 2、4、8、16 tick 四档均被观测到。
- 既有页表、跨页复制、badaccess、memcap、UART、进程回收复用和 gifts 完整性回归均通过。

链接器仍报告基线 `kernel/kernel` 含 RWX LOAD 段；这是已有内核链接脚本的属性。本实验用户页权限由 Lab3 页表实现和对应运行测试验证。
