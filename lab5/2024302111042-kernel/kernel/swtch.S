# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	

#内核代码（调度器）跳到另一段内核代码（forkret）
#实际保存和恢复寄存器
#swtch(old, new) 先将当前的 ra、`sp`、`s0–s11` 存入 old，再从 new 取出同一组寄存器，最后执行 ret。
#对已运行过的进程，`ret` 回到它上次暂停的内核位置；对新进程，`ret` 跳到 allocate() 预设的 forkret。
#它先把当前进程的 ra、sp、s0–s11 写进旧 context，再从新 context 读出这些寄存器，最后执行 ret。
#因此它既能切换到一个曾经运行过的进程，也能启动一个新进程。
.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret

	
