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
BUILD_DIR = build

# Source files
BOOT_SRC = $(BOOT_DIR)/boot.S
KERNEL_SRCS = $(wildcard $(KERNEL_DIR)/*.c)

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SRCS))

# Output files
KERNEL_ELF = $(BUILD_DIR)/vibeos.elf
KERNEL_BIN = $(BUILD_DIR)/vibeos.bin

# Compiler flags
CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72 -Wall -Wextra -O2 -I$(KERNEL_DIR)
ASFLAGS = -mcpu=cortex-a72
LDFLAGS = -nostdlib -T linker.ld

# QEMU settings
QEMU = qemu-system-aarch64
# Graphical mode with virtio-keyboard
QEMU_FLAGS = -M virt -cpu cortex-a72 -m 256M -device ramfb -device virtio-keyboard-device -serial stdio -kernel $(KERNEL_BIN)
# No-graphics mode (terminal only)
QEMU_FLAGS_NOGRAPHIC = -M virt -cpu cortex-a72 -m 256M -nographic -kernel $(KERNEL_BIN)

.PHONY: all clean run run-nographic debug

all: $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_OBJ): $(BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(BOOT_OBJ) $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  VibeOS built successfully!"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "  Run with: make run"
	@echo "========================================="

run: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS)

run-nographic: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS_NOGRAPHIC)

debug: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS) -S -s

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD_DIR)

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
