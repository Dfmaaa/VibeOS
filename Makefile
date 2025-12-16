# VibeOS Makefile
# Build system for VibeOS - an aarch64 operating system
#
# Targets:
#   make              - Build kernel for QEMU (default)
#   make TARGET=pi    - Build kernel for Raspberry Pi Zero 2W
#   make user         - Build userspace programs
#   make install      - Install to disk image
#   make run          - Build, install, and run in QEMU

# Target selection (default: qemu)
TARGET ?= qemu

# Cross-compiler toolchain
CROSS_COMPILE ?= aarch64-elf-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Directories
BOOT_DIR = boot
KERNEL_DIR = kernel
HAL_DIR = kernel/hal
USER_DIR = user
BUILD_DIR = build
SYSROOT = vibeos_root

# Target-specific settings
ifeq ($(TARGET),pi-debug)
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI -DPI_DEBUG_MODE
else ifeq ($(TARGET),pi)
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI
else
    HAL_PLATFORM = qemu
    CPU = cortex-a72
    LINKER_SCRIPT = linker.ld
    BOOT_SRC = $(BOOT_DIR)/boot.S
    KERNEL_BIN_NAME = vibeos.bin
    CFLAGS_TARGET = -DTARGET_QEMU
endif

# Printf output: uart (default) or screen
PRINTF ?= uart
ifeq ($(PRINTF),uart)
    CFLAGS_TARGET += -DPRINTF_UART
endif

# Source files
KERNEL_C_SRCS = $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_S_SRCS = $(wildcard $(KERNEL_DIR)/*.S)
HAL_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/*.c)
HAL_USB_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/usb/*.c)

# Userspace programs (single-file)
USER_PROGS = snake tetris desktop calc vibesh echo ls cat pwd mkdir touch rm term uptime sysmon textedit files date play music ping fetch viewer vim led \
             clear yes sleep seq whoami hostname uname which basename dirname \
             head tail wc df free ps stat grep find hexdump du cp mv kill lscpu lsusb dmesg mousetest readtest

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_C_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_S_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS))
HAL_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/%.c,$(BUILD_DIR)/hal_%.o,$(HAL_C_SRCS))
HAL_USB_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c,$(BUILD_DIR)/hal_usb_%.o,$(HAL_USB_C_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_S_OBJS) $(HAL_OBJS) $(HAL_USB_OBJS)

# Output files
KERNEL_ELF = $(BUILD_DIR)/vibeos.elf
KERNEL_BIN = $(BUILD_DIR)/$(KERNEL_BIN_NAME)
DISK_IMG = disk.img
DISK_SIZE = 1024

# Compiler flags
CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -Wall -Wextra -O3 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc $(CFLAGS_TARGET)
TLS_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -O2 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc -w $(CFLAGS_TARGET)
ASFLAGS = -mcpu=$(CPU)
LDFLAGS = -nostdlib -T $(LINKER_SCRIPT)

# Userspace compiler flags
USER_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -fPIE -Wall -Wextra -O3 -I$(USER_DIR)/lib
USER_LDFLAGS = -nostdlib -pie -T user/linker.ld

# QEMU settings
QEMU = qemu-system-aarch64
QEMU_FLAGS = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device ramfb -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-keyboard-device -device virtio-tablet-device -device virtio-sound-device,audiodev=audio0 -audiodev coreaudio,id=audio0 -device virtio-net-device,netdev=net0 -netdev user,id=net0 -serial stdio -bios $(BUILD_DIR)/vibeos.bin
QEMU_FLAGS_NOGRAPHIC = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-sound-device,audiodev=audio0 -audiodev coreaudio,id=audio0 -device virtio-net-device,netdev=net0 -netdev user,id=net0 -nographic -bios $(BUILD_DIR)/vibeos.bin

.PHONY: all clean run run-nographic run-pi user install disk pi pi-debug sync-disk

all: $(KERNEL_BIN)
	@echo ""
	@echo "Built for target: $(TARGET)"
	@echo "Output: $(KERNEL_BIN)"

# ============ Kernel build ============

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_OBJ): $(BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal_usb_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls.o: $(KERNEL_DIR)/tls.c | $(BUILD_DIR)
	@echo "Building TLS (this takes a while)..."
	$(CC) $(TLS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(BOOT_OBJ) $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  VibeOS kernel built!"
	@echo "  Target: $(TARGET)"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "========================================="

# ============ Userspace build ============

$(BUILD_DIR)/user:
	mkdir -p $(BUILD_DIR)/user

$(BUILD_DIR)/user/crt0.o: $(USER_DIR)/lib/crt0.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/crti.o: $(USER_DIR)/lib/crti.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/crtn.o: $(USER_DIR)/lib/crtn.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/%.prog.o: $(USER_DIR)/bin/%.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Single-file programs -> vibeos_root/bin/
$(SYSROOT)/bin/%: $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/%.prog.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "  Built /bin/$*"

# Browser (multi-file)
$(BUILD_DIR)/user/browser_main.o: $(USER_DIR)/bin/browser/main.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -I$(USER_DIR)/bin/browser -c $< -o $@

$(SYSROOT)/bin/browser: $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/browser_main.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "  Built /bin/browser"

# MicroPython (external build)
$(SYSROOT)/bin/micropython: $(wildcard micropython/ports/vibeos/*.c)
	@echo "Building MicroPython..."
	$(MAKE) -C micropython/ports/vibeos
	cp micropython/ports/vibeos/build/micropython.elf $@
	@echo "  Built /bin/micropython"

# TCC (external build)
$(SYSROOT)/bin/tcc: $(wildcard tinycc/vibeos/*.c) $(wildcard tinycc/vibeos/*.h)
	@echo "Building TCC..."
	$(MAKE) -C tinycc/vibeos clean
	$(MAKE) -C tinycc/vibeos
	cp tinycc/vibeos/build/tcc $@
	@echo "  Built /bin/tcc"

# CRT files for TCC
CRT_FILES = $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/crti.o $(BUILD_DIR)/user/crtn.o

# Build all userspace programs
USER_BINS = $(patsubst %,$(SYSROOT)/bin/%,$(USER_PROGS)) $(SYSROOT)/bin/browser $(SYSROOT)/bin/micropython $(SYSROOT)/bin/tcc

user: $(USER_BINS) $(CRT_FILES)
	@echo ""
	@echo "========================================="
	@echo "  Userspace programs built!"
	@echo "  Output: $(SYSROOT)/bin/"
	@echo "========================================="

# ============ Disk image ============

$(DISK_IMG):
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE)
	@DISK_DEV=$$(hdiutil attach -nomount $(DISK_IMG) | head -1 | awk '{print $$1}') && \
		newfs_msdos -F 32 -v VIBEOS $$DISK_DEV && \
		hdiutil detach $$DISK_DEV
	@echo "Disk image created: $(DISK_IMG)"

disk: $(DISK_IMG)

# Sync vibeos_root/ to QEMU disk image (internal target)
sync-disk: user $(DISK_IMG)
	@hdiutil attach $(DISK_IMG) -nobrowse -mountpoint /tmp/vibeos_mount > /dev/null
	@rsync -a $(SYSROOT)/ /tmp/vibeos_mount/
	@mkdir -p /tmp/vibeos_mount/usr/src
	@rsync -a --exclude='*.o' --exclude='*.elf' --exclude='build/' user/ /tmp/vibeos_mount/usr/src/user/
	@rsync -a --exclude='*.o' --exclude='build/' tinycc/ /tmp/vibeos_mount/usr/src/tinycc/
	@mkdir -p /tmp/vibeos_mount/lib/tcc/include
	@mkdir -p /tmp/vibeos_mount/lib/tcc/lib
	@cp -r tinycc/include/* /tmp/vibeos_mount/lib/tcc/include/
	@cp tinycc/vibeos/tcc_include/* /tmp/vibeos_mount/lib/tcc/include/ 2>/dev/null || true
	@cp user/lib/vibe.h /tmp/vibeos_mount/lib/tcc/include/
	@cp user/lib/gfx.h /tmp/vibeos_mount/lib/tcc/include/ 2>/dev/null || true
	@cp $(BUILD_DIR)/user/crt0.o /tmp/vibeos_mount/lib/tcc/lib/crt1.o
	@cp $(BUILD_DIR)/user/crt0.o /tmp/vibeos_mount/lib/tcc/lib/Scrt1.o
	@cp $(BUILD_DIR)/user/crti.o /tmp/vibeos_mount/lib/tcc/lib/
	@cp $(BUILD_DIR)/user/crtn.o /tmp/vibeos_mount/lib/tcc/lib/
	@cp tinycc/vibeos/libtcc1.a /tmp/vibeos_mount/lib/tcc/lib/
	@cp tinycc/vibeos/libc.a /tmp/vibeos_mount/lib/tcc/lib/
	@cp tinycc/vibeos/libc.a /tmp/vibeos_mount/lib/tcc/lib/libc.so
	@cp user/linker.ld /tmp/vibeos_mount/lib/tcc/lib/
	@dot_clean /tmp/vibeos_mount 2>/dev/null || true
	@find /tmp/vibeos_mount -name '._*' -delete 2>/dev/null || true
	@find /tmp/vibeos_mount -name '.DS_Store' -delete 2>/dev/null || true
	@rm -rf /tmp/vibeos_mount/.fseventsd /tmp/vibeos_mount/.Spotlight-V100 /tmp/vibeos_mount/.Trashes 2>/dev/null || true
	@hdiutil detach /tmp/vibeos_mount > /dev/null

# ============ Run targets ============

run: $(KERNEL_BIN) sync-disk
	$(QEMU) $(QEMU_FLAGS)

run-nographic: $(KERNEL_BIN) sync-disk
	$(QEMU) $(QEMU_FLAGS_NOGRAPHIC)

# Run Pi kernel in QEMU raspi3b (no disk - just tests boot)
run-pi: pi
	$(QEMU) -M raspi3b -kernel $(BUILD_DIR)/kernel8.img -serial stdio -usb -device usb-kbd

# ============ Pi targets ============

pi:
	$(MAKE) TARGET=pi PRINTF=$(PRINTF)

pi-debug:
	$(MAKE) TARGET=pi-debug PRINTF=screen

# Install to Pi SD card
# Usage: make install DISK=/dev/disk4
install: pi user
	@if [ -z "$(DISK)" ]; then \
		echo "Usage: make install DISK=/dev/diskN"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make install DISK=/dev/disk4      # macOS"; \
		echo "  make install DISK=/dev/sda        # Linux"; \
		echo ""; \
		echo "Run 'diskutil list' (macOS) or 'lsblk' (Linux) to find your SD card"; \
		exit 1; \
	fi
	@echo "Installing to $(DISK)..."
	@# Mount the disk
	@if [ "$$(uname)" = "Darwin" ]; then \
		diskutil unmountDisk $(DISK) 2>/dev/null || true; \
		PART=$$(echo $(DISK) | sed 's|/dev/||')s1; \
		mkdir -p /tmp/vibeos_sd; \
		mount -t msdos /dev/$$PART /tmp/vibeos_sd || (echo "Failed to mount. Try: sudo make install DISK=$(DISK)"; exit 1); \
	else \
		sudo umount $(DISK)1 2>/dev/null || true; \
		mkdir -p /tmp/vibeos_sd; \
		sudo mount $(DISK)1 /tmp/vibeos_sd; \
	fi
	@echo "  Copying kernel..."
	@cp $(BUILD_DIR)/kernel8.img /tmp/vibeos_sd/
	@echo "  Copying userspace..."
	@rsync -a $(SYSROOT)/ /tmp/vibeos_sd/
	@mkdir -p /tmp/vibeos_sd/usr/src
	@rsync -a --exclude='*.o' --exclude='*.elf' --exclude='build/' user/ /tmp/vibeos_sd/usr/src/user/
	@rsync -a --exclude='*.o' --exclude='build/' tinycc/ /tmp/vibeos_sd/usr/src/tinycc/
	@mkdir -p /tmp/vibeos_sd/lib/tcc/include
	@mkdir -p /tmp/vibeos_sd/lib/tcc/lib
	@cp -r tinycc/include/* /tmp/vibeos_sd/lib/tcc/include/
	@cp tinycc/vibeos/tcc_include/* /tmp/vibeos_sd/lib/tcc/include/ 2>/dev/null || true
	@cp user/lib/vibe.h /tmp/vibeos_sd/lib/tcc/include/
	@cp user/lib/gfx.h /tmp/vibeos_sd/lib/tcc/include/ 2>/dev/null || true
	@cp $(BUILD_DIR)/user/crt0.o /tmp/vibeos_sd/lib/tcc/lib/crt1.o
	@cp $(BUILD_DIR)/user/crt0.o /tmp/vibeos_sd/lib/tcc/lib/Scrt1.o
	@cp $(BUILD_DIR)/user/crti.o /tmp/vibeos_sd/lib/tcc/lib/
	@cp $(BUILD_DIR)/user/crtn.o /tmp/vibeos_sd/lib/tcc/lib/
	@cp tinycc/vibeos/libtcc1.a /tmp/vibeos_sd/lib/tcc/lib/
	@cp tinycc/vibeos/libc.a /tmp/vibeos_sd/lib/tcc/lib/
	@cp tinycc/vibeos/libc.a /tmp/vibeos_sd/lib/tcc/lib/libc.so
	@cp user/linker.ld /tmp/vibeos_sd/lib/tcc/lib/
	@if [ "$$(uname)" = "Darwin" ]; then \
		dot_clean /tmp/vibeos_sd 2>/dev/null || true; \
		find /tmp/vibeos_sd -name '._*' -delete 2>/dev/null || true; \
		find /tmp/vibeos_sd -name '.DS_Store' -delete 2>/dev/null || true; \
		diskutil unmount /tmp/vibeos_sd; \
	else \
		sudo umount /tmp/vibeos_sd; \
	fi
	@echo "Done! SD card ready."

# ============ Utility targets ============

debug: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS) -S -s

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SYSROOT)/bin/*
	$(MAKE) -C tinycc/vibeos clean 2>/dev/null || true
	$(MAKE) -C micropython/ports/vibeos clean 2>/dev/null || true

distclean: clean
	rm -f $(DISK_IMG)

check-toolchain:
	@which $(CC) > /dev/null 2>&1 || \
	(echo "Cross-compiler not found. Install with: brew install aarch64-elf-gcc" && exit 1)
