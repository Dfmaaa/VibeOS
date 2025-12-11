# VibeOS v1 End Roadmap

**Goal**: Finish v1, then port to Raspberry Pi before starting v2.

---

## v1 Remaining Work

Polish and complete current feature set.

### File Manager - Default App Associations
- [ ] File type → app mapping system
  - `.txt`, `.c`, `.h`, `.md`, etc. → TextEdit
  - `.png`, `.jpg`, `.bmp` → Image Viewer
  - `.mp3`, `.wav` → Music Player
  - Binary/executable → "Run in Terminal" (optional, discuss)
- [ ] "Open With" prompt when no default association exists
  - Dialog: "No default app for this file type. Choose an app:"
  - List available apps, let user pick
- [ ] Double-click in Files app opens with associated app

### TextEdit - Unsaved Changes Warning
- [ ] Track "modified" state (already have this in status bar)
- [ ] On quit (close button or Cmd+Q): if modified, show dialog
  - "You have unsaved changes. Save before closing?"
  - Buttons: [Save] [Don't Save] [Cancel]

### Music Player - Open Any File Mode
- [ ] Support opening individual MP3/WAV files directly
  - `music /path/to/song.mp3` - plays that file
  - Double-click from Files app - opens and plays
- [ ] Windows Media Player vibe: can browse library OR open a file
- [ ] If opened with file argument, show "Now Playing" view instead of album browser
- [ ] Still support album browser mode when launched with no arguments

### Image Viewer (New App)
- [ ] `/bin/viewer` - simple image viewer
- [ ] Supports PNG, JPG, BMP (stb_image already in vendor/)
- [ ] Center image in window, scale if too large
- [ ] Arrow keys or click to go prev/next in same directory
- [ ] Launched from Files app on double-click


### General Polish
- [ ] Bug fixes and stability
- [ ] Any remaining UI polish
- [ ] Documentation cleanup

---

## Raspberry Pi Port

Real hardware. Forces driver abstraction.

### Target Hardware

**Raspberry Pi Zero 2W**
- SoC: BCM2710A1 (quad-core Cortex-A53)
- RAM: 512MB
- USB: Single micro USB OTG port (data)
- Video: Mini HDMI
- Storage: MicroSD
- Networking: WiFi + Bluetooth (CYW43439) - complex driver, skip for v1

### Dev Setup

```
Power: Wall adapter → Pi micro USB (power port)
Video: Pi mini HDMI → adapter → HDMI cable → LG monitor (1080p 100Hz)
Input: Pi micro USB (data) → OTG adapter → USB-A to C → USB-C dock
       └── Keychron B1 Pro (2.4GHz USB-A adapter)
       └── Razer Viper (USB-A wired)
```

**Note**: HDMI on dock won't work - it's for video *out* from host, not passthrough. Must use Pi's native mini HDMI.

### Boot Process

```
GPU ROM → bootcode.bin → start.elf → kernel8.img
```

Put `kernel8.img` on FAT32 SD card. GPU loads it to 0x80000.

For development:
1. Build on Mac
2. Copy kernel8.img to SD card
3. Put SD in Pi, boot
4. Debug output on HDMI (framebuffer console)

### New Drivers Needed

| Component | QEMU virt | Pi Zero 2W |
|-----------|-----------|------------|
| UART | PL011 @ 0x09000000 | Mini UART @ 0x7E215000 or PL011 @ 0x7E201000 |
| Interrupts | GIC-400 | BCM2835 legacy IRQ controller |
| Framebuffer | ramfb | VideoCore IV (mailbox) |
| Timer | ARM generic timer | ARM generic timer (same!) |
| Storage | virtio-blk | SDHCI (SD card) |
| Keyboard | virtio-input | USB HID |
| Mouse | virtio-input | USB HID |
| Network | virtio-net | Skip for v1 (WiFi too complex) |
| Sound | virtio-sound | Skip for v1 (PWM or HDMI audio) |

### Hardware Abstraction Layer

Abstract these behind interfaces:

```c
// Serial
void serial_init(void);
void serial_putc(char c);
char serial_getc(void);

// Interrupts
void irq_init(void);
void irq_register(int irq, void (*handler)(void));
void irq_enable(int irq);

// Framebuffer
void fb_init(int width, int height);
void fb_put_pixel(int x, int y, uint32_t color);
uint32_t *fb_get_buffer(void);

// Block device
void blk_init(void);
int blk_read(uint32_t sector, void *buf);
int blk_write(uint32_t sector, const void *buf);

// Input
void input_init(void);
int keyboard_getc(void);
void mouse_get_state(int *x, int *y, int *buttons);

// Timer
void timer_init(uint32_t hz);
uint64_t timer_get_ticks(void);
```

### Pi Zero 2W Specific Challenges

**USB Stack** - Biggest challenge. Single USB OTG port for keyboard + mouse.
- Port TinyUSB (~15K lines) or similar
- Need USB hub support (via dock)
- DWC2 USB controller (DesignWare)

**VideoCore Mailbox** - To get framebuffer
- Request FB from GPU via mailbox protocol
- Well documented, not too hard

**SD Card** - SDHCI controller
- More complex than virtio-blk
- Reference: circle, rpi4-osdev, USPi

**512MB RAM** - Tight but fine
- Current heap is ~238MB
- May need to be more careful with allocations

**No Networking for v1** - WiFi driver (CYW43439) is too complex
- Needs firmware blob
- SPI interface
- Skip for initial port

### Build System Changes

```makefile
# Target selection
ifeq ($(TARGET),pizero2w)
    UART_ADDR = 0x7E215000
    LOAD_ADDR = 0x80000
    CFLAGS += -DTARGET_PIZERO2W -mcpu=cortex-a53
else
    # QEMU virt (default)
    UART_ADDR = 0x09000000
    LOAD_ADDR = 0x40000000
    CFLAGS += -DTARGET_QEMU -mcpu=cortex-a72
endif
```

### SD Card Contents

```
/boot (FAT32 partition)
├── bootcode.bin    (from Raspberry Pi firmware)
├── start.elf       (from Raspberry Pi firmware)
├── config.txt      (Pi configuration)
├── kernel8.img     (VibeOS!)
└── (optional: cmdline.txt)
```

config.txt:
```
arm_64bit=1
kernel=kernel8.img
```

### Testing

Can test Pi Zero 2W build in QEMU with `-M raspi3b` (closest match, limited support) or real hardware.

---

## After Pi Port

Hardware abstraction done → cleaner codebase → ready for v2.

v2 starts with TCC port (see VIBEOSV2_ROADMAP.md).

---

## Boot System Notes

### Current (QEMU)

`-bios vibeos.bin` - Loads to flash @ 0x0, boots EL3 Secure.

Works fine for development. No need to change.

### Raspberry Pi

Custom boot chain (GPU ROM → bootcode → start.elf → kernel8.img).

No UEFI needed. Simple and works.

---

## Priority Order

1. Finish v1 on QEMU
2. Create HAL interfaces
3. Port to Pi Zero 2W
4. Iterate until stable on real hardware
5. Start v2 (POSIX, toolchain, Firefox)
