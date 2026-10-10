# lab1 骨架 Makefile(课程提供;新增源文件时把它加进 OBJS)
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; else echo riscv64-elf-; fi)
CC   = $(TOOLPREFIX)gcc
LD   = $(TOOLPREFIX)ld
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie
QEMU = qemu-system-riscv64

OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o kernel/main.o

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c kernel/riscv.h kernel/course_sid.h kernel/console.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S kernel/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

.PHONY: test
test:
	python3 tests/test_lab1.py

# 验收环境固定(排雷环节禁止改动本行以下内容)
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -f kernel/*.o kernel/kernel
