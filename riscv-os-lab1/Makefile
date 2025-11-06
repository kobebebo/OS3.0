CROSS   ?= riscv64-unknown-elf-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc
LD      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
CFLAGS  := -march=rv64imac -mabi=lp64 -mcmodel=medany -nostdlib -nostartfiles -ffreestanding -O2 -g -Wall -Wextra
LDFLAGS := -T linker.ld -nostdlib -nostartfiles -ffreestanding -Wl,--build-id=none

SRCDIR  := kernel
OBJDIR  := build
TARGET  := kernel.elf

CSRCS := $(wildcard $(SRCDIR)/*.c)
ASMS  := $(wildcard $(SRCDIR)/*.s)
OBJS  := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CSRCS)) \
         $(patsubst $(SRCDIR)/%.s,$(OBJDIR)/%.o,$(ASMS))

.PHONY: all clean run qemu-gdb

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld | $(OBJDIR)
$(LD) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

$(OBJDIR):
mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.s | $(OBJDIR)
$(AS) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
qemu-system-riscv64 -machine virt -nographic -bios default -kernel $(TARGET)

qemu-gdb: $(TARGET)
qemu-system-riscv64 -machine virt -nographic -bios default -kernel $(TARGET) -S -s

clean:
rm -rf $(OBJDIR) $(TARGET)
