# VibeOS Makefile
# Build system for VibeOS - an aarch64 operating system
#
# Supports two targets:
#   make              - Build for QEMU (default)
#   make TARGET=pi    - Build for Raspberry Pi Zero 2W

# Target selection (default: qemu)
TARGET ?= qemu

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
HAL_DIR = kernel/hal
USER_DIR = user
BUILD_DIR = build
USER_BUILD_DIR = $(BUILD_DIR)/user

# Target-specific settings
ifeq ($(TARGET),pi-debug)
    # Raspberry Pi Zero 2W - Debug Mode (minimal boot for USB debugging)
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI -DPI_DEBUG_MODE
else ifeq ($(TARGET),pi)
    # Raspberry Pi Zero 2W
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI
else
    # QEMU virt (default)
    HAL_PLATFORM = qemu
    CPU = cortex-a72
    LINKER_SCRIPT = linker.ld
    BOOT_SRC = $(BOOT_DIR)/boot.S
    KERNEL_BIN_NAME = vibeos.bin
    CFLAGS_TARGET = -DTARGET_QEMU
endif

# Printf output: uart (default) or screen
# Override with: make PRINTF=screen
PRINTF ?= uart
ifeq ($(PRINTF),uart)
    CFLAGS_TARGET += -DPRINTF_UART
endif

# Source files
KERNEL_C_SRCS = $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_S_SRCS = $(wildcard $(KERNEL_DIR)/*.S)
HAL_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/*.c)
HAL_USB_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/usb/*.c)

# Userspace programs to build and install to disk
# Note: browser is handled specially (multi-file build from user/bin/browser/)
USER_PROGS = snake tetris desktop calc vibesh echo ls cat pwd mkdir touch rm term uptime sysmon textedit files date play music ping fetch viewer vim blink \
             clear yes sleep seq whoami hostname uname which basename dirname \
             head tail wc df free ps stat grep find hexdump du cp mv kill lscpu lsusb
USER_PROGS_MULTIFILE = browser

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_C_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_S_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS))
HAL_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/%.c,$(BUILD_DIR)/hal_%.o,$(HAL_C_SRCS))
HAL_USB_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c,$(BUILD_DIR)/hal_usb_%.o,$(HAL_USB_C_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_S_OBJS) $(HAL_OBJS) $(HAL_USB_OBJS)

# Userspace ELF files (installed to disk, NOT embedded in kernel)
USER_ELFS = $(patsubst %,$(USER_BUILD_DIR)/%.elf,$(USER_PROGS))
USER_ELFS_MULTIFILE = $(patsubst %,$(USER_BUILD_DIR)/%.elf,$(USER_PROGS_MULTIFILE))

# Output files
KERNEL_ELF = $(BUILD_DIR)/vibeos.elf
KERNEL_BIN = $(BUILD_DIR)/$(KERNEL_BIN_NAME)
DISK_IMG = disk.img
DISK_SIZE = 1024

# Compiler flags - YOLO -O3
# Floating point enabled (no -mgeneral-regs-only)
# Use -mstrict-align to avoid unaligned SIMD accesses
# -I$(KERNEL_DIR)/libc is for TLSe to find our stdlib stubs
CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -Wall -Wextra -O3 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc $(CFLAGS_TARGET)

# Special flags for TLS (TLSe is huge and noisy)
TLS_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -O2 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc -w $(CFLAGS_TARGET)
ASFLAGS = -mcpu=$(CPU)
LDFLAGS = -nostdlib -T $(LINKER_SCRIPT)

# Userspace compiler flags (PIE for position-independent loading) - YOLO -O3
USER_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -fPIE -Wall -Wextra -O3 -I$(USER_DIR)/lib
USER_LDFLAGS = -nostdlib -pie -T user/linker.ld

# QEMU settings (only used for QEMU target)
QEMU = qemu-system-aarch64
# Graphical mode with virtio-keyboard, virtio-tablet (mouse), virtio-blk disk, virtio-sound, and virtio-net
# Use force-legacy=false to get modern virtio (version 2) which is easier to program
# Use secure=on and -bios to boot at EL3 with full GIC access
# Network: user-mode NAT networking (guest IP: 10.0.2.15, gateway: 10.0.2.2, DNS: 10.0.2.3)
QEMU_FLAGS = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device ramfb -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-keyboard-device -device virtio-tablet-device -device virtio-sound-device,audiodev=audio0 -audiodev coreaudio,id=audio0 -device virtio-net-device,netdev=net0 -netdev user,id=net0 -serial stdio -bios $(BUILD_DIR)/vibeos.bin
# No-graphics mode (terminal only) - no keyboard in nographic mode
QEMU_FLAGS_NOGRAPHIC = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-sound-device,audiodev=audio0 -audiodev coreaudio,id=audio0 -device virtio-net-device,netdev=net0 -netdev user,id=net0 -nographic -bios $(BUILD_DIR)/vibeos.bin

.PHONY: all clean run run-nographic debug user disk install-user pi install-pi

all: $(KERNEL_BIN)
	@echo ""
	@echo "Built for target: $(TARGET)"
	@echo "Output: $(KERNEL_BIN)"

# Shortcut for Pi build
# Use PRINTF=screen (default) for framebuffer output, PRINTF=uart for serial
pi:
	$(MAKE) TARGET=pi PRINTF=$(PRINTF)

# Install to Pi SD card
# Usage: make install-pi DISK=disk5s2
# Builds kernel for Pi and user programs, then installs to SD card
install-pi: pi user
	@if [ -z "$(DISK)" ]; then \
		echo "Usage: make install-pi DISK=<disk>"; \
		echo "Example: make install-pi DISK=disk5s2"; \
		echo ""; \
		echo "Run 'diskutil list' to find your SD card"; \
		exit 1; \
	fi
	./scripts/install-pi.sh $(DISK)

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

# HAL C objects
$(BUILD_DIR)/hal_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# HAL USB subdirectory objects
$(BUILD_DIR)/hal_usb_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Special rule for TLS (includes huge third-party library)
$(BUILD_DIR)/tls.o: $(KERNEL_DIR)/tls.c | $(BUILD_DIR)
	@echo "Building TLS (this takes a while)..."
	$(CC) $(TLS_CFLAGS) -c $< -o $@

# Kernel assembly objects
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Userspace crt0
$(USER_BUILD_DIR)/crt0.o: $(USER_DIR)/lib/crt0.S | $(USER_BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Userspace program compilation
$(USER_BUILD_DIR)/%.prog.o: $(USER_DIR)/bin/%.c | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Link userspace program (single-file)
$(USER_BUILD_DIR)/%.elf: $(USER_BUILD_DIR)/crt0.o $(USER_BUILD_DIR)/%.prog.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "Built userspace program: $@"

# Browser - multi-file build
$(USER_BUILD_DIR)/browser.elf: $(USER_BUILD_DIR)/crt0.o $(USER_BUILD_DIR)/browser_main.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "Built userspace program: $@ (multi-file)"

$(USER_BUILD_DIR)/browser_main.o: $(USER_DIR)/bin/browser/main.c $(USER_DIR)/bin/browser/*.h | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) -I$(USER_DIR)/bin/browser -c $< -o $@

# Build all userspace programs
user: $(USER_ELFS) $(USER_ELFS_MULTIFILE)

# Install userspace programs to disk image
install-user: user $(DISK_IMG)
	@echo "Installing userspace programs to disk..."
	@hdiutil attach $(DISK_IMG) -nobrowse -mountpoint /tmp/vibeos_mount > /dev/null
	@for prog in $(USER_PROGS) $(USER_PROGS_MULTIFILE); do \
		cp $(USER_BUILD_DIR)/$$prog.elf /tmp/vibeos_mount/bin/$$prog; \
		echo "  Installed /bin/$$prog"; \
	done
	@if [ -f beep.mp3 ]; then cp beep.mp3 /tmp/vibeos_mount/beep.mp3 && echo "  Installed /beep.mp3"; fi
	@if [ -f beep.wav ]; then cp beep.wav /tmp/vibeos_mount/beep.wav && echo "  Installed /beep.wav"; fi
	@if [ -d Music ]; then mkdir -p /tmp/vibeos_mount/home/user/Music && cp -r Music/* /tmp/vibeos_mount/home/user/Music/ && echo "  Installed /home/user/Music/"; fi
	@if [ -d fonts ]; then mkdir -p /tmp/vibeos_mount/fonts && cp -r fonts/* /tmp/vibeos_mount/fonts/ && echo "  Installed /fonts/"; fi
	@if [ -f duck.png ]; then cp duck.png /tmp/vibeos_mount/duck.png && echo "  Installed /duck.png"; fi
	@if [ -f duck.jpg ]; then cp duck.jpg /tmp/vibeos_mount/duck.jpg && echo "  Installed /duck.jpg"; fi
	@if [ -f duck.bmp ]; then cp duck.bmp /tmp/vibeos_mount/duck.bmp && echo "  Installed /duck.bmp"; fi
	@dot_clean /tmp/vibeos_mount 2>/dev/null || true
	@find /tmp/vibeos_mount -name '._*' -delete 2>/dev/null || true
	@find /tmp/vibeos_mount -name '.DS_Store' -delete 2>/dev/null || true
	@rm -rf /tmp/vibeos_mount/.fseventsd 2>/dev/null || true
	@rm -rf /tmp/vibeos_mount/.Spotlight-V100 2>/dev/null || true
	@rm -rf /tmp/vibeos_mount/.Trashes 2>/dev/null || true
	@hdiutil detach /tmp/vibeos_mount > /dev/null
	@echo "Done!"

# Link kernel (no embedded userspace - programs are on disk)
$(KERNEL_ELF): $(BOOT_OBJ) $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  VibeOS built successfully!"
	@echo "  Target: $(TARGET)"
	@echo "  Binary: $(KERNEL_BIN)"
ifeq ($(TARGET),qemu)
	@echo "  Run with: make run"
else
	@echo "  Copy to SD card as kernel8.img"
endif
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
	@hdiutil attach $(DISK_IMG) -nobrowse -mountpoint /tmp/vibeos_mount > /dev/null
	@mkdir -p /tmp/vibeos_mount/home/user
	@mkdir -p /tmp/vibeos_mount/bin
	@mkdir -p /tmp/vibeos_mount/etc
	@mkdir -p /tmp/vibeos_mount/tmp
	@echo "Welcome to VibeOS!" > /tmp/vibeos_mount/etc/motd
	@dot_clean /tmp/vibeos_mount 2>/dev/null || true
	@find /tmp/vibeos_mount -name '._*' -delete 2>/dev/null || true
	@find /tmp/vibeos_mount -name '.DS_Store' -delete 2>/dev/null || true
	@hdiutil detach /tmp/vibeos_mount > /dev/null
	@echo ""
	@echo "========================================="
	@echo "  Disk image created: $(DISK_IMG)"
	@echo ""
	@echo "  To mount and add files on macOS:"
	@echo "    hdiutil attach $(DISK_IMG)"
	@echo "    # ... add files ..."
	@echo "    hdiutil detach /Volumes/VIBEOS"
	@echo "========================================="

run: $(BUILD_DIR)/vibeos.bin install-user
	$(QEMU) $(QEMU_FLAGS)

run-nographic: $(BUILD_DIR)/vibeos.bin $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS_NOGRAPHIC)

# Run Pi build in QEMU raspi3b with USB keyboard
run-pi:
	$(MAKE) TARGET=pi
	$(QEMU) -M raspi3b -kernel build/kernel8.img -serial stdio -usb -device usb-kbd

# Build Pi debug mode (minimal boot for USB keyboard debugging)
# User deploys kernel8.img to SD card manually
# Forces PRINTF=screen since Pi has no serial without adapter
pi-debug:
	$(MAKE) TARGET=pi-debug PRINTF=screen
	@echo "========================================="
	@echo "  Pi Debug Mode kernel built!"
	@echo "  Output goes to SCREEN (not UART)"
	@echo "  Copy build/kernel8.img to SD card"
	@echo "========================================="

debug: $(BUILD_DIR)/vibeos.bin
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
