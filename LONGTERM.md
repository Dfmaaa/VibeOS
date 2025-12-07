# VibeOS Long-term Roadmap

The dream: A usable retro computer you could run on a Raspberry Pi. Classic Mac vibes, self-hosting capability, maybe even networking.

## Phase 1: Desktop Apps (Current)

Build real GUI applications into the desktop:

- [ ] Terminal emulator (shell in a window - biggest unlock)
- [ ] File explorer (navigate filesystem, launch programs)
- [ ] Notepad (simple text editor, non-modal unlike vi)
- [ ] Calculator (classic Mac style)

**Architecture:** Apps are compiled into desktop.c (Approach 2). Simpler than IPC between processes. External programs (games) still take over fullscreen.

## Phase 1.5: Fun Stuff

- [ ] Paint (draw pixels, save as BMP)
- [ ] Image viewer (BMP first, maybe PNG later)
- [ ] Clock widget

## Phase 2: Pick Your Adventure

### Music Path
- [ ] virtio-sound or Intel HDA audio driver
- [ ] WAV playback (trivial format)
- [ ] MP3 playback (port minimp3)
- [ ] Music player app with playlist

### Compiler Path (Self-Hosting!)
- [ ] Port TCC (Tiny C Compiler, ~30k lines)
- [ ] Port or write simple assembler
- [ ] VibeOS can compile C code for itself
- [ ] THE DREAM: write VibeOS code from within VibeOS

### Network Path
- [ ] virtio-net driver
- [ ] IP/UDP (stateless, easier)
- [ ] TCP (state machine, retransmits)
- [ ] DNS resolver
- [ ] HTTP client (no TLS - plaintext only)
- [ ] Simple "browser" (HTML only, no CSS/JS)
- [ ] Email send (SMTP is just text)

### DOOM Path
- [ ] Implement missing libc functions DOOM needs
- [ ] Port DOOM source code
- [ ] Flex on everyone

## Phase 3: Hardware

### Raspberry Pi Port
Same architecture (aarch64), different peripherals:
- [ ] Pi-specific UART addresses
- [ ] Mailbox framebuffer interface
- [ ] SD card driver (replaces virtio-blk)
- [ ] USB keyboard (this is the hard part)

Estimate: 2-3 focused sessions

### x86 ThinkPad Port
This is basically a rewrite:
- [ ] x86 boot code (BIOS/GRUB nightmare)
- [ ] x86 assembly (context switch, etc)
- [ ] VGA/VESA framebuffer
- [ ] PS/2 or USB keyboard
- [ ] ATA/IDE disk driver

Estimate: Many weeks. Different project really.

## Wild Ideas (Probably Not Happening)

| Idea | Verdict | Why |
|------|---------|-----|
| Python | No | CPython is 500k+ lines, needs full libc |
| Vulkan | No | We have dumb framebuffer, no GPU |
| Node.js | No | V8 is millions of lines of C++ |
| Full web browser | Probably no | HTML+CSS+JS rendering is insane |
| HTTPS/TLS | Maybe | Need to port crypto library (mbedTLS) |
| SSH | Maybe | Same crypto problem |
| Claude API | Maybe | Need HTTPS, or use a local proxy |

## The Self-Hosting Dream

The ultimate goal: VibeOS can build itself.

Requirements:
- C compiler (TCC)
- Text editor (vi or notepad)
- Assembler
- Linker
- Shell with basic commands

Then someone could theoretically:
1. Boot VibeOS
2. Write new code in notepad
3. Compile it in terminal
4. Run it
5. Infinite capability expansion

## Desktop Environment Vision

Classic Mac System 7 / Apple Lisa aesthetic:
- 1-bit or limited color icons
- Chicago-style bitmap font
- Striped title bars (already have)
- Menu bar at top (already have)
- Apple logo () in menu bar
- Finder-style file browser
- "About This Mac" dialog
- Desktop pattern/wallpaper
- Trash can icon

## What's NOT the Goal

- Linux compatibility
- POSIX compliance
- Modern security
- Production readiness
- Performance

This is a vibes-first operating system. It should feel like 1987, not 2024.
