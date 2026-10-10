# Lab2 V2 验证记录

日期：2026-10-11。环境：macOS，RISC-V交叉GCC，QEMU virt RV64（Sstc）。

## 已完成的验证

`make test`：11组测试全部通过。
从暂存源码导出到全新目录后重新构建并测试：11组再次全部通过，
确认不依赖工作目录中已有的目标文件、用户镜像或测试日志。

| 检查 | 结果 |
|---|---|
| 启动Banner与lab1预期文件逐字节匹配 | PASS，72字节前缀 |
| Shell、hi、getpid/write/exit、fork/exec/wait | PASS |
| 赠送badecall，非法调用90..99 | PASS，全部返回-1 |
| 用户非法指针、越界/溢出地址、错误fd、零长度 | PASS |
| fork私有内存复制、wait退出码37、无子进程wait | PASS |
| 定时器pause与无系统调用忙循环的抢占 | PASS |
| 空缓冲read、未按回车时行读保持等待 | PASS |
| 96字节边界、180字符超长行、环形多轮复用 | PASS |
| 退格、CR转LF、exec不存在程序后恢复Shell | PASS |
| spin期间UART回显、Ctrl-C后重回Shell | PASS |
| 连续70次子进程创建/退出/回收 | PASS |
| 字符流测试构建：输入单字节即可返回read | PASS |
| 4 hart：只启动一个Shell且hi可执行 | PASS |
| 只读赠送件SHA-256一致 | PASS |
| 平铺用户镜像入口为0，含BSS范围不侵占栈 | PASS |
| 串口注入常规脚本 | PASS，BUFSTORM lines=4 bytes=26 |
| 串口注入超长行脚本 | PASS，BUFSTORM lines=4 bytes=384 |

用户ecall产生的cause=8属于正常系统调用；集成测试排除所有其他同步异常，
并检查输出中不存在PANIC/SELFTEST FAIL。lab2会正常产生UART与定时器中断，
所以不能像lab1一样要求整个中断日志为空。

`make test-lab1`：原lab1的6组测试全部通过。
lab0、lab1工作目录与其已提交版本一致，未在本轮改动。

## 可复现入口

```sh
make
make test
make test-lab1
make qemu
```

完整运行日志由测试自动保存到test-results/（忽略提交，可随时重新生成）。
`support/inject_uart.py --tree . --script tests/storm.txt --log test-results/manual.log`
可以单独执行注入测试。

## 集成差异与限制

1. 只读跳板固定高地址、用户程序链接地址0，需要最小Sv39映射；这不是完整lab3实现。
2. 用户空间暂定64KB、页暂RWX；无文件系统、完整argv或通用内存增长服务。
3. 阻塞调用使用单核待完成状态和调度扫描，不是lab4的完整sleep/wakeup模型。
4. 与赠送平铺镜像约定一致，入口必须为0。测试同时检查ELF入口及end符号；
   新增用户程序不能在main前意外放入独立函数而改变入口。
5. 课程原注入器仍使用`$ `和磁盘参数。工作副本修正为`sh> `、无磁盘启动、
   消费式EXPECT和明确失败退出；原件保存在materials以便审计。
6. 原链接布局及赠送用户布局产生RWX LOAD警告，未通过隐藏警告来冒充无警告构建。
