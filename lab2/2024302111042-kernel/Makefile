.DEFAULT_GOAL := kernel/kernel
# Gifted user recipes share intermediate objects; serialize even under make -j.
.NOTPARALLEL:
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; else echo riscv64-elf-; fi)
CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie -fno-builtin
QEMU = qemu-system-riscv64
OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o kernel/main.o \
       kernel/trampoline.o kernel/kernelvec.o kernel/trap.o kernel/syscall.o \
       kernel/proc.o kernel/vm.o kernel/string.o kernel/userimg.o
kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)
%.o: %.c $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<
%.o: %.S $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<
include ../gifts/Makefile.upgrade
UPROGS = sh hi spin badecall bufstorm selftest readprobe
kernel/userimg.S: ../gifts/Makefile.upgrade $(addprefix user-flat/,$(UPROGS:=.bin))
.PHONY: qemu clean test test-lab1
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic
clean: clean-extra
	rm -f kernel/*.o kernel/kernel
test: kernel/kernel
	python3 tests/test_lab2.py
test-lab1:
	$(MAKE) -C ../../lab1/2024302111042-kernel test
