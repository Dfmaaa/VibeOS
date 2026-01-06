# VibeOS

A hobby OS built from scratch with Claude.
Not everything works, some stuff is not even tested, but most things do. 
![VibeOS Desktop](screenshots/desktop.png)

## What is this?

VibeOS is an operating system written from scratch for ARM64 (aarch64). It runs on QEMU and real Raspberry Pi Zero 2W hardware. The entire OS was built in collaboration with Claude over 64 sessions, documented in the [session logs](SESSION_LOG_1.md).


## Features

**Core**
- Custom kernel with cooperative multitasking (preemptive backup)
- FAT32 filesystem with long filename support
- Memory allocator, process scheduler, interrupt handling
- GIC-400 (QEMU) and BCM2836/BCM2835 (Pi) interrupt controllers

**GUI**
- Desktop environment with draggable windows
- Menu bar, dock, window minimize/maximize/close
- Mouse and keyboard input
- Modern macOS-inspired aesthetic

**Networking**
- Full TCP/IP stack (Ethernet, ARP, IP, ICMP, UDP, TCP)
- DNS resolver
- HTTP client
- TLS 1.2 with HTTPS support

**Apps**
- Web browser with HTML/CSS rendering
- Terminal emulator with readline-style shell
- Text editor (vim clone) with syntax highlighting
- File manager with drag-and-drop
- Music player (MP3/WAV)
- Calculator, system monitor
- VibeCode IDE
- DOOM

**Development**
- TCC (Tiny C Compiler) - compile C programs directly on VibeOS
- MicroPython interpreter with full kernel API bindings
- 60+ userspace programs (coreutils, games, GUI apps)

**Hardware**
- Runs on Raspberry Pi Zero 2W
- USB keyboard and mouse via DWC2 driver
- SD card via EMMC driver
- 1920x1080 framebuffer

## Screenshots

| Desktop | Browser | DOOM |
|---------|---------|------|
| ![Desktop](screenshots/desktop.png) | ![Browser](screenshots/browser.png) | ![DOOM](screenshots/doom.png) |

| Terminal | VibeCode | Real Hardware |
|----------|----------|---------------|
| ![Terminal](screenshots/terminal.png) | ![VibeCode](screenshots/vibecode.png) | ![Pi Boot](screenshots/pi-boot.gif) |

## Quick Start

### Requirements

- macOS or Linux
- `aarch64-elf-gcc` cross-compiler
- QEMU with `qemu-system-aarch64`

On macOS:
```bash
brew install aarch64-elf-gcc qemu
```

### Build and Run

```bash
# Create disk image (first time only)
make disk

# Build and run
make run
```

This builds the kernel and all userspace programs, syncs them to the disk image, and launches QEMU with a GUI window.

### Controls

- Mouse and keyboard work as expected
- Click dock icons to launch apps
- Ctrl+S to save in editors
- Type `help` in terminal for shell commands

## Running on Raspberry Pi Zero 2W

### Build for Pi

```bash
make TARGET=pi
```

### Install to SD Card

```bash
# Find your SD card
diskutil list  # macOS
lsblk          # Linux

# Install (example: /dev/disk4)
make install DISK=/dev/disk4
```

This partitions the SD card, installs the bootloader and kernel, and copies all programs.

### What Works on Pi

- Full desktop GUI at 1920x1080
- USB keyboard and mouse (including through hubs)
- SD card filesystem
- All userspace programs
- DOOM at 2x scale

### What's Missing on Pi

- Networking (no driver for Pi's WiFi/Ethernet)
- Audio (no driver for Pi's audio)

## Troubleshooting USB on Pi

If you boot on Pi Zero, have a USB hub with a keyboard and mouse plugged in, and your keyboard is not working, try to change the order they are plugged in to the hub. Keyboard should be plugged in before the mouse (at a lower port number)
This is (i am not sure) because some mouses show up as a mouse and a few keyboards, so vibeos does not find a real keyboard.

Also, USB on Pi does not support hotplug. If you unplug or forget to plug in either one of the peripherals, you have to reboot. 

If USB still does not work after verifying both of these, reboot until it does and it will probably work in 5th try tops.


## Documentation

- [USAGE.md](USAGE.md) - How to use VibeOS (shell commands, apps, keyboard shortcuts)
- [PROGRAMMING.md](PROGRAMMING.md) - Writing programs for VibeOS (TCC, Python, cross-compile, vibe.h API)
- [CLAUDE.md](CLAUDE.md) - Technical reference, gotchas, architecture decisions

## Third-Party Code

VibeOS includes the following third-party libraries:

| Library | License | Used For |
|---------|---------|----------|
| [doomgeneric](https://github.com/ozkl/doomgeneric) | GPL-2.0 | DOOM port |
| [MicroPython](https://micropython.org/) | MIT | Python interpreter |
| [TCC](https://bellard.org/tcc/) | LGPL-2.1 | C compiler |
| [TLSe](https://github.com/nickarls/tlse) | BSD-2-Clause | TLS 1.2 implementation |
| [minimp3](https://github.com/lieff/minimp3) | CC0 | MP3 decoding |
| [stb_truetype](https://github.com/nothings/stb) | MIT | TrueType font rendering |
| [stb_image](https://github.com/nothings/stb) | MIT | Image loading |

## Contributing

Issues and PRs will be reviewed. No guarantees.

## License

MIT. See [LICENSE](LICENSE).

DOOM port is GPL-2.0. See [user/bin/doom/LICENSE](user/bin/doom/LICENSE).

## Session Logs

The development of VibeOS is documented across 64 sessions:

- [Session Log 1](SESSION_LOG_1.md) - Sessions 1-10: Bootloader, kernel, shell, VFS, FAT32, GUI foundations
- [Session Log 2](SESSION_LOG_2.md) - Sessions 11-20: Desktop apps, PIE relocations, terminal, interrupts
- [Session Log 3](SESSION_LOG_3.md) - Sessions 21-32: Power management, LFN, audio, networking, browser
- [Session Log 4](SESSION_LOG_4.md) - Sessions 33-49: TLS/HTTPS, Pi port, USB driver, optimizations
- [Session Log 5](SESSION_LOG_5.md) - Sessions 50-55: USB fixes, DMA, performance tuning
- [Session Log 6](SESSION_LOG_6.md) - Sessions 56-64: MicroPython, TCC, DOOM, VibeCode, polish
