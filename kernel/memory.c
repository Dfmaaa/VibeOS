/*
 * VibeOS Memory Management
 *
 * Simple first-fit heap allocator. Not the fastest, but easy to understand.
 * Each allocation has a header with size and free flag.
 *
 * RAM is detected at runtime by parsing the Device Tree Blob (DTB).
 */

#include "memory.h"
#include "dtb.h"
#include "printf.h"

// Detected RAM info (populated by memory_init)
uint64_t ram_base;
uint64_t ram_size;

// Heap bounds
uint64_t heap_start;
uint64_t heap_end;

// Block header - sits before each allocation
typedef struct block_header {
    size_t size;                    // Size of data area (not including header)
    uint8_t is_free;                // 1 if block is free, 0 if allocated
    struct block_header *next;      // Next block in list
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

static block_header_t *free_list = NULL;

// Defined in linker script - end of BSS in RAM
extern uint64_t _bss_end;

// Stack location (must match boot.S!)
#define KERNEL_STACK_TOP 0x5F000000

// Leave some room below stack for safety (1MB)
#define STACK_BUFFER (1 * 1024 * 1024)

void memory_init(void) {
    // Parse DTB to get RAM info
    struct dtb_memory_info mem_info;
    if (dtb_parse((void *)DTB_ADDR, &mem_info) != 0) {
        // Fallback to safe defaults if DTB parsing fails
        printf("[MEM] WARNING: DTB parse failed, using defaults\n");
        ram_base = 0x40000000;
        ram_size = 256 * 1024 * 1024;  // 256MB
    } else {
        ram_base = mem_info.base;
        ram_size = mem_info.size;
    }

    printf("[MEM] RAM: base=0x%lx, size=%lu MB\n", ram_base, ram_size / (1024 * 1024));

    // Heap starts after BSS, aligned to 16 bytes
    // Add 64KB buffer after BSS for safety
    heap_start = ALIGN_UP((uint64_t)&_bss_end + 0x10000, 16);

    // Heap ends before the stack (with buffer)
    // Stack is at fixed address, so heap must stay below it
    uint64_t ram_end = ram_base + ram_size;
    uint64_t heap_max = KERNEL_STACK_TOP - STACK_BUFFER;

    // But also can't exceed actual RAM
    if (heap_max > ram_end) {
        heap_max = ram_end - STACK_BUFFER;
    }

    heap_end = heap_max;

    // Initialize with one giant free block
    free_list = (block_header_t *)heap_start;
    free_list->size = heap_end - heap_start - HEADER_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;

    printf("[MEM] Heap: 0x%lx - 0x%lx (%lu MB)\n",
           heap_start, heap_end, (heap_end - heap_start) / (1024 * 1024));
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    // Align size to 16 bytes
    size = ALIGN_UP(size, 16);

    block_header_t *current = free_list;

    // First-fit: find first block that's big enough
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            // Found a suitable block

            // Split if there's enough room for another block
            if (current->size >= size + HEADER_SIZE + 16) {
                // Create new block after this allocation
                block_header_t *new_block = (block_header_t *)((uint8_t *)current + HEADER_SIZE + size);
                new_block->size = current->size - size - HEADER_SIZE;
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            return (void *)((uint8_t *)current + HEADER_SIZE);
        }
        current = current->next;
    }

    // No suitable block found
    return NULL;
}

void free(void *ptr) {
    if (ptr == NULL) return;

    // Get header
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    block->is_free = 1;

    // Coalesce adjacent free blocks
    block_header_t *current = free_list;
    while (current != NULL) {
        if (current->is_free && current->next != NULL && current->next->is_free) {
            // Merge with next block
            current->size += HEADER_SIZE + current->next->size;
            current->next = current->next->next;
            // Don't advance - check if we can merge again
        } else {
            current = current->next;
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr != NULL) {
        // Zero the memory
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);

    // If current block is big enough, just return it
    if (block->size >= size) {
        return ptr;
    }

    // Otherwise allocate new block and copy
    void *new_ptr = malloc(size);
    if (new_ptr != NULL) {
        uint8_t *src = (uint8_t *)ptr;
        uint8_t *dst = (uint8_t *)new_ptr;
        for (size_t i = 0; i < block->size; i++) {
            dst[i] = src[i];
        }
        free(ptr);
    }
    return new_ptr;
}

size_t memory_used(void) {
    size_t used = 0;
    block_header_t *current = free_list;
    while (current != NULL) {
        if (!current->is_free) {
            used += current->size + HEADER_SIZE;
        }
        current = current->next;
    }
    return used;
}

size_t memory_free(void) {
    size_t free_mem = 0;
    block_header_t *current = free_list;
    while (current != NULL) {
        if (current->is_free) {
            free_mem += current->size;
        }
        current = current->next;
    }
    return free_mem;
}

uint64_t memory_heap_start(void) {
    return heap_start;
}

uint64_t memory_heap_end(void) {
    return heap_end;
}

uint64_t memory_get_sp(void) {
    uint64_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

int memory_alloc_count(void) {
    int count = 0;
    block_header_t *current = free_list;
    while (current != NULL) {
        if (!current->is_free) {
            count++;
        }
        current = current->next;
    }
    return count;
}
