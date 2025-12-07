# VibeOS Makefile
# Build system for VibeOS - an aarch64 operating system

# Cross-compiler toolchain
# On macOS, install with: brew install aarch64-elf-gcc
# Or use: brew tap ArmMbed/homebrew-formulae && brew install arm-none-eabi-gcc
CROSS_COMPILE ?= aarch64-elf-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Directories
BOOT_DIR = boot
KERNEL_DIR = kernel
USER_DIR = user
BUILD_DIR = build
USER_BUILD_DIR = $(BUILD_DIR)/user

# Source files
BOOT_SRC = $(BOOT_DIR)/boot.S
KERNEL_C_SRCS = $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_S_SRCS = $(wildcard $(KERNEL_DIR)/*.S)

# Userspace programs (disabled - monolith kernel)
USER_PROGS =

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_C_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_S_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_S_OBJS)

# Embedded binary objects (userspace programs linked into kernel)
USER_ELFS = $(patsubst %,$(USER_BUILD_DIR)/%.elf,$(USER_PROGS))
USER_OBJS = $(patsubst %,$(USER_BUILD_DIR)/%.o,$(USER_PROGS))

# Output files
KERNEL_ELF = $(BUILD_DIR)/vibeos.elf
KERNEL_BIN = $(BUILD_DIR)/vibeos.bin
DISK_IMG = disk.img
DISK_SIZE = 64

# Compiler flags
CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72 -mgeneral-regs-only -Wall -Wextra -O2 -I$(KERNEL_DIR)
ASFLAGS = -mcpu=cortex-a72
LDFLAGS = -nostdlib -T linker.ld

# Userspace compiler flags
USER_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72 -mgeneral-regs-only -Wall -Wextra -O2 -I$(USER_DIR)/lib
USER_LDFLAGS = -nostdlib -T user/linker.ld

# QEMU settings
QEMU = qemu-system-aarch64
# Graphical mode with virtio-keyboard and virtio-blk disk
# Use force-legacy=false to get modern virtio (version 2) which is easier to program
QEMU_FLAGS = -M virt -cpu cortex-a72 -m 256M -global virtio-mmio.force-legacy=false -device ramfb -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-keyboard-device -serial stdio -kernel $(KERNEL_BIN)
# No-graphics mode (terminal only) - no keyboard in nographic mode
QEMU_FLAGS_NOGRAPHIC = -M virt -cpu cortex-a72 -m 256M -global virtio-mmio.force-legacy=false -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -nographic -kernel $(KERNEL_BIN)

.PHONY: all clean run run-nographic debug user disk

all: $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(USER_BUILD_DIR):
	mkdir -p $(USER_BUILD_DIR)

# Boot object
$(BOOT_OBJ): $(BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Kernel C objects
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# virtio_blk needs -O0 to prevent optimization issues
$(BUILD_DIR)/virtio_blk.o: $(KERNEL_DIR)/virtio_blk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O0 -c $< -o $@

# Kernel assembly objects
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Userspace crt0
$(USER_BUILD_DIR)/crt0.o: $(USER_DIR)/lib/crt0.S | $(USER_BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Userspace program compilation
$(USER_BUILD_DIR)/%.prog.o: $(USER_DIR)/bin/%.c | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Link userspace program
$(USER_BUILD_DIR)/%.elf: $(USER_BUILD_DIR)/crt0.o $(USER_BUILD_DIR)/%.prog.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "Built userspace program: $@"

# Convert userspace ELF to linkable object (embedded binary)
$(USER_BUILD_DIR)/%.o: $(USER_BUILD_DIR)/%.elf
	$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
	@echo "Embedded binary: $@"

# Build all userspace programs
user: $(USER_ELFS)

# Link kernel with embedded userspace programs
$(KERNEL_ELF): $(BOOT_OBJ) $(KERNEL_OBJS) $(USER_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  VibeOS built successfully!"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "  Run with: make run"
	@echo "========================================="

# Create disk image with FAT32 filesystem
# Only creates if it doesn't exist - to recreate, delete disk.img first
disk: $(DISK_IMG)

$(DISK_IMG):
	@echo "Creating FAT32 disk image..."
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE)
	@echo "Formatting as FAT32..."
	@DISK_DEV=$$(hdiutil attach -nomount $(DISK_IMG) | head -1 | awk '{print $$1}') && \
		newfs_msdos -F 32 -v VIBEOS $$DISK_DEV && \
		hdiutil detach $$DISK_DEV
	@echo "Creating directory structure..."
	@hdiutil attach $(DISK_IMG) > /dev/null
	@mkdir -p /Volumes/VIBEOS/home/user
	@mkdir -p /Volumes/VIBEOS/bin
	@mkdir -p /Volumes/VIBEOS/etc
	@mkdir -p /Volumes/VIBEOS/tmp
	@echo "Welcome to VibeOS!" > /Volumes/VIBEOS/etc/motd
	@hdiutil detach /Volumes/VIBEOS > /dev/null
	@echo ""
	@echo "========================================="
	@echo "  Disk image created: $(DISK_IMG)"
	@echo ""
	@echo "  To mount and add files on macOS:"
	@echo "    hdiutil attach $(DISK_IMG)"
	@echo "    # ... add files ..."
	@echo "    hdiutil detach /Volumes/VIBEOS"
	@echo "========================================="

run: $(KERNEL_BIN) $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS)

run-nographic: $(KERNEL_BIN) $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS_NOGRAPHIC)

debug: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS) -S -s

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $<

disasm-user: $(USER_ELFS)
	$(OBJDUMP) -d $(USER_BUILD_DIR)/hello.elf

clean:
	rm -rf $(BUILD_DIR)

# Clean everything including disk image
distclean: clean
	rm -f $(DISK_IMG)

# Alternative cross-compiler detection
# Try different common toolchain names
check-toolchain:
	@which $(CC) > /dev/null 2>&1 || \
	(echo "Cross-compiler not found. Try one of:" && \
	 echo "  brew install aarch64-elf-gcc" && \
	 echo "  # or" && \
	 echo "  brew tap ArmMbed/homebrew-formulae && brew install arm-none-eabi-gcc" && \
	 echo "  # Then set CROSS_COMPILE=aarch64-none-elf- or arm-none-eabi-" && \
	 exit 1)
