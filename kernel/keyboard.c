/*
 * VibeOS Virtio Keyboard Driver
 *
 * Implements virtio-input for keyboard input on QEMU virt machine.
 * Virtio MMIO devices start at 0x0a000000 with 0x200 stride.
 */

#include "keyboard.h"
#include "printf.h"
#include "string.h"

// Virtio MMIO registers
#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

// Virtio MMIO register offsets
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

// Virtio status bits
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

// Virtio device types
#define VIRTIO_DEV_INPUT        18

// Virtio input event types (Linux input event codes)
#define EV_KEY      0x01

// Key states
#define KEY_RELEASED 0
#define KEY_PRESSED  1

// Virtio input event structure
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

// Virtqueue structures
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

// Keyboard state
static volatile uint32_t *kbd_base = NULL;
static virtq_desc_t *desc = NULL;
static virtq_avail_t *avail = NULL;
static virtq_used_t *used = NULL;
static virtio_input_event_t *events = NULL;
static uint16_t last_used_idx = 0;

#define QUEUE_SIZE 16
#define DESC_F_WRITE 2

// Key buffer
#define KEY_BUF_SIZE 32
static char key_buffer[KEY_BUF_SIZE];
static int key_buf_read = 0;
static int key_buf_write = 0;

// Queue memory (must be at global scope for proper static allocation)
static uint8_t queue_mem[4096] __attribute__((aligned(4096)));
static virtio_input_event_t event_bufs[QUEUE_SIZE] __attribute__((aligned(16)));

// Scancode to ASCII (simple US layout, lowercase only for now)
static const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static inline uint32_t read32(volatile uint32_t *addr) {
    return *addr;
}

static inline void write32(volatile uint32_t *addr, uint32_t val) {
    *addr = val;
}

static volatile uint32_t *find_virtio_input(void) {
    // Scan virtio MMIO devices
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_INPUT) {
            return base;
        }
    }
    return NULL;
}

int keyboard_init(void) {
    printf("[KBD] Initializing keyboard...\n");

    kbd_base = find_virtio_input();
    if (!kbd_base) {
        printf("[KBD] No virtio-input device found\n");
        return -1;
    }

    printf("[KBD] Found virtio-input at %p\n", kbd_base);

    // Reset device
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, 0);

    // Acknowledge
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);

    // Driver loaded
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Read and write features (we don't need any special features)
    write32(kbd_base + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);

    // Features OK
    write32(kbd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    // Select queue 0 (eventq)
    write32(kbd_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);

    uint32_t max_queue = read32(kbd_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    printf("[KBD] Max queue size: %d\n", max_queue);

    if (max_queue < QUEUE_SIZE) {
        printf("[KBD] Queue too small\n");
        return -1;
    }

    // Set queue size
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NUM/4, QUEUE_SIZE);
    printf("[KBD] Set queue size to %d\n", QUEUE_SIZE);

    // Use globally allocated queue memory
    printf("[KBD] Using queue memory at %p\n", queue_mem);

    desc = (virtq_desc_t *)queue_mem;
    avail = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
    used = (virtq_used_t *)(queue_mem + 2048);  // Put used ring in second half
    events = event_bufs;
    printf("[KBD] desc=%p avail=%p used=%p events=%p\n", desc, avail, used, events);

    // Set queue addresses
    uint64_t desc_addr = (uint64_t)desc;
    uint64_t avail_addr = (uint64_t)avail;
    uint64_t used_addr = (uint64_t)used;
    printf("[KBD] Setting queue addresses...\n");

    write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));
    printf("[KBD] Queue addresses set\n");

    // Initialize descriptors with buffers for receiving events
    printf("[KBD] Initializing descriptors...\n");
    for (int i = 0; i < QUEUE_SIZE; i++) {
        desc[i].addr = (uint64_t)&events[i];
        desc[i].len = sizeof(virtio_input_event_t);
        desc[i].flags = DESC_F_WRITE;  // Device writes to this buffer
        desc[i].next = 0;
    }
    printf("[KBD] Descriptors initialized\n");

    // Add all descriptors to available ring
    avail->flags = 0;
    for (int i = 0; i < QUEUE_SIZE; i++) {
        avail->ring[i] = i;
    }
    avail->idx = QUEUE_SIZE;
    printf("[KBD] Available ring set up\n");

    printf("[KBD] Setting queue ready...\n");
    // Queue ready
    write32(kbd_base + VIRTIO_MMIO_QUEUE_READY/4, 1);

    printf("[KBD] Setting driver OK...\n");
    // Driver OK
    write32(kbd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    printf("[KBD] Notifying device...\n");
    // Notify device
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    printf("[KBD] Keyboard initialized!\n");
    return 0;
}

static void process_events(void) {
    if (!kbd_base) return;

    // Check for new events
    while (last_used_idx != used->idx) {
        uint16_t idx = last_used_idx % QUEUE_SIZE;
        uint32_t desc_idx = used->ring[idx].id;

        virtio_input_event_t *ev = &events[desc_idx];

        // Process key event
        if (ev->type == EV_KEY && ev->value == KEY_PRESSED) {
            uint16_t code = ev->code;
            if (code < 128) {
                char c = scancode_to_ascii[code];
                if (c != 0) {
                    // Add to buffer
                    int next = (key_buf_write + 1) % KEY_BUF_SIZE;
                    if (next != key_buf_read) {
                        key_buffer[key_buf_write] = c;
                        key_buf_write = next;
                    }
                }
            }
        }

        // Re-add descriptor to available ring
        uint16_t avail_idx = avail->idx % QUEUE_SIZE;
        avail->ring[avail_idx] = desc_idx;
        avail->idx++;

        last_used_idx++;
    }

    // Notify device we added buffers
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Ack interrupt
    write32(kbd_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(kbd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));
}

int keyboard_has_key(void) {
    process_events();
    return key_buf_read != key_buf_write;
}

int keyboard_getc(void) {
    process_events();

    if (key_buf_read == key_buf_write) {
        return -1;
    }

    char c = key_buffer[key_buf_read];
    key_buf_read = (key_buf_read + 1) % KEY_BUF_SIZE;
    return c;
}
