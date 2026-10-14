.DEFAULT_GOAL := kernel/kernel
.NOTPARALLEL:
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; else echo riscv64-elf-; fi)
CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
NM = $(TOOLPREFIX)nm
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie -fno-builtin
UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie -I.
QEMU = qemu-system-riscv64
UPROGS = sh hi spin badecall bufstorm selftest readprobe badaccess memcap pgtbl vmtest
OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o kernel/main.o \
       kernel/trampoline.o kernel/kernelvec.o kernel/trap.o kernel/syscall.o \
       kernel/proc.o kernel/vm.o kernel/string.o kernel/userimg.o

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

user-flat/%.bin: user/%.c user/user.h user/ulib.c user/printf.c user/usys.S user/user-lab3.ld
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c user/ulib.c -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c user/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c user/usys.S -o user-flat/usys.o
	$(CC) $(UFLAGS) -Iuser -c $< -o user-flat/$*.o
	$(LD) -T user/user-lab3.ld -o user-flat/$*.elf \
		user-flat/$*.o user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

kernel/userimg.S: support/mkuserimg.py $(addprefix user-flat/,$(UPROGS:=.bin))
	python3 support/mkuserimg.py --nm $(NM) --output $@ $(UPROGS)

.PHONY: qemu clean test test-lab1
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -rf user-flat kernel/userimg.S user/usys.S kernel/*.o kernel/kernel test-results __pycache__ support/__pycache__ tests/__pycache__

test: kernel/kernel
	python3 tests/test_lab3.py

test-lab1:
	$(MAKE) -C ../../lab1/2024302111042-kernel test
