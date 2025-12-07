/*
 * VibeOS Keyboard Driver
 *
 * Virtio keyboard input
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Initialize keyboard
int keyboard_init(void);

// Get a character (returns -1 if none available)
int keyboard_getc(void);

// Check if key is available
int keyboard_has_key(void);

#endif
