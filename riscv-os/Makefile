CROSS_COMPILE ?= riscv64-unknown-elf-

CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJDUMP = $(CROSS_COMPILE)objdump

CFLAGS  = -Wall -Wextra -O2 \
          -ffreestanding -nostdlib -nostartfiles \
          -mcmodel=medany -march=rv64gc -mabi=lp64 \
          -Iinclude

LDFLAGS = -T kernel/kernel.ld -nostdlib --no-relax

KERNEL_OBJS = \
    kernel/entry.o     \
    kernel/start.o     \
    kernel/main.o      \
    kernel/uart.o      \
    kernel/console.o   \
    kernel/printf.o    \
    kernel/pmm.o       \
    kernel/vm.o        \
    kernel/trap.o      \
    kernel/test.o      \
    kernel/kernelvec.o \
    kernel/proc.o      \
    kernel/swtch.o     \
    kernel/syscall.o   \
    kernel/sysproc.o   \
    kernel/sysfile.o   \
    kernel/bio.o       \
    kernel/log.o       \
    kernel/fs.o        \
    kernel/file.o      \
    kernel/virtio_disk.o  \
    kernel/fs_debug.o \
    kernel/klog.o \


all: kernel.elf

kernel.elf: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	$(OBJDUMP) -d $@ > kernel.asm

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/%.o: kernel/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

qemu: kernel.elf
	qemu-system-riscv64 -machine virt -bios none -nographic -kernel kernel.elf

clean:
	rm -f kernel/*.o kernel.elf kernel.asm

.PHONY: all clean qemu
