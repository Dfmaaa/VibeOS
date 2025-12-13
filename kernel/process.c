/*
 * VibeOS Process Management
 *
 * Cooperative multitasking - Win3.1/Classic Mac style.
 * Programs run in kernel space and call kernel functions directly.
 * No memory protection, no preemption.
 */

#include "process.h"
#include "elf.h"
#include "vfs.h"
#include "memory.h"
#include "string.h"
#include "printf.h"
#include "kapi.h"
#include <stddef.h>

// Process table
static process_t proc_table[MAX_PROCESSES];
static int current_pid = -1;  // -1 means kernel/shell is running
static int next_pid = 1;

// Kernel context - saved when switching from kernel to a process
// This allows us to return to kernel (e.g., desktop running via process_exec)
static cpu_context_t kernel_context;

// Program load address - grows upward as we load programs
// Set dynamically based on heap_end
static uint64_t program_base = 0;
static uint64_t next_load_addr = 0;

// Align to 64KB boundary for cleaner loading
#define ALIGN_64K(x) (((x) + 0xFFFF) & ~0xFFFFULL)

// Program entry point signature
typedef int (*program_entry_t)(kapi_t *api, int argc, char **argv);

// Forward declaration
static void process_entry_wrapper(void);

void process_init(void) {
    // Clear process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].state = PROC_STATE_FREE;
        proc_table[i].pid = 0;
    }
    current_pid = -1;
    next_pid = 1;

    // Programs load right after the heap
    program_base = ALIGN_64K(heap_end);
    next_load_addr = program_base;

    printf("[PROC] Process subsystem initialized (max %d processes)\n", MAX_PROCESSES);
    printf("[PROC] Program load area: 0x%lx+\n", program_base);
}

// Find a free slot in the process table
static int find_free_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_STATE_FREE) {
            return i;
        }
    }
    return -1;
}

process_t *process_current(void) {
    if (current_pid < 0) return NULL;
    return &proc_table[current_pid];
}

process_t *process_get(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_STATE_FREE) {
            return &proc_table[i];
        }
    }
    return NULL;
}

int process_count_ready(void) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_STATE_READY ||
            proc_table[i].state == PROC_STATE_RUNNING) {
            count++;
        }
    }
    return count;
}

int process_get_info(int index, char *name, int name_size, int *state) {
    if (index < 0 || index >= MAX_PROCESSES) return 0;
    process_t *p = &proc_table[index];
    if (p->state == PROC_STATE_FREE) return 0;

    // Copy name
    if (name && name_size > 0) {
        int len = strlen(p->name);
        if (len >= name_size) len = name_size - 1;
        for (int i = 0; i < len; i++) name[i] = p->name[i];
        name[len] = '\0';
    }

    // Return state
    if (state) *state = (int)p->state;

    return 1;
}

// Create a new process (load the binary but don't start it)
int process_create(const char *path, int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Find free slot
    int slot = find_free_slot();
    if (slot < 0) {
        printf("[PROC] No free process slots\n");
        return -1;
    }

    // Look up file
    vfs_node_t *file = vfs_lookup(path);
    if (!file) {
        printf("[PROC] File not found: %s\n", path);
        return -1;
    }

    if (vfs_is_dir(file)) {
        printf("[PROC] Cannot exec directory: %s\n", path);
        return -1;
    }

    size_t size = file->size;
    if (size == 0) {
        printf("[PROC] File is empty: %s\n", path);
        return -1;
    }

    // Read the ELF file
    char *data = malloc(size);
    if (!data) {
        printf("[PROC] Out of memory reading %s\n", path);
        return -1;
    }

    int bytes = vfs_read(file, data, size, 0);
    if (bytes != (int)size) {
        printf("[PROC] Failed to read %s\n", path);
        free(data);
        return -1;
    }

    // Calculate how much memory the program needs
    uint64_t prog_size = elf_calc_size(data, size);
    if (prog_size == 0) {
        printf("[PROC] Invalid ELF: %s\n", path);
        free(data);
        return -1;
    }

    // Align load address
    uint64_t load_addr = ALIGN_64K(next_load_addr);

    // Load the ELF at this address
    elf_load_info_t info;
    if (elf_load_at(data, size, load_addr, &info) != 0) {
        printf("[PROC] Failed to load ELF: %s\n", path);
        free(data);
        return -1;
    }

    free(data);

    // Update next load address for future programs
    next_load_addr = ALIGN_64K(load_addr + info.load_size + 0x10000);

    // Set up process structure
    process_t *proc = &proc_table[slot];
    proc->pid = next_pid++;
    strncpy(proc->name, path, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    proc->state = PROC_STATE_READY;
    proc->load_base = info.load_base;
    proc->load_size = info.load_size;
    proc->entry = info.entry;
    proc->parent_pid = current_pid;
    proc->exit_status = 0;

    // Allocate stack
    proc->stack_size = PROCESS_STACK_SIZE;
    proc->stack_base = malloc(proc->stack_size);
    if (!proc->stack_base) {
        printf("[PROC] Failed to allocate stack\n");
        proc->state = PROC_STATE_FREE;
        return -1;
    }

    // Initialize context
    // Stack grows down, SP starts at top (aligned to 16 bytes)
    uint64_t stack_top = ((uint64_t)proc->stack_base + proc->stack_size) & ~0xFULL;

    // Set up initial context so when we switch to this process,
    // it "returns" to process_entry_wrapper which calls main
    memset(&proc->context, 0, sizeof(cpu_context_t));
    proc->context.sp = stack_top;
    proc->context.x30 = (uint64_t)process_entry_wrapper;  // Return to wrapper
    proc->context.x19 = proc->entry;    // Pass entry point in callee-saved reg
    proc->context.x20 = (uint64_t)&kapi; // Pass kapi pointer
    proc->context.x21 = (uint64_t)argc;  // argc
    proc->context.x22 = (uint64_t)argv;  // argv

    printf("[PROC] Created process '%s' pid=%d at 0x%lx (slot %d)\n",
           proc->name, proc->pid, proc->load_base, slot);

    return proc->pid;
}

// Entry wrapper - called when a new process is switched to for the first time
// x19 = entry, x20 = kapi, x21 = argc, x22 = argv (set in context)
static void process_entry_wrapper(void) {
    // Get parameters from callee-saved registers (set during create)
    register uint64_t entry asm("x19");
    register uint64_t kapi_ptr asm("x20");
    register uint64_t argc asm("x21");
    register uint64_t argv asm("x22");

    program_entry_t prog_main = (program_entry_t)entry;
    int result = prog_main((kapi_t *)kapi_ptr, (int)argc, (char **)argv);

    // Process returned - exit
    process_exit(result);
}

// Start a process (make it runnable)
int process_start(int pid) {
    process_t *proc = process_get(pid);
    if (!proc) return -1;

    if (proc->state != PROC_STATE_READY) {
        printf("[PROC] Process %d not ready (state=%d)\n", pid, proc->state);
        return -1;
    }

    printf("[PROC] Starting process %d '%s'\n", pid, proc->name);
    return 0;  // Already ready, scheduler will pick it up
}

// Exit current process
void process_exit(int status) {
    if (current_pid < 0) {
        printf("[PROC] Exit called with no current process!\n");
        return;
    }

    int slot = current_pid;
    process_t *proc = &proc_table[slot];
    printf("[PROC] Process '%s' (pid %d) exited with status %d\n",
           proc->name, proc->pid, status);

    proc->exit_status = status;
    proc->state = PROC_STATE_ZOMBIE;

    // Free stack - but we're still on it! Don't free yet.
    // The stack will be freed when the slot is reused.

    // Mark slot as free (simple cleanup for now)
    proc->state = PROC_STATE_FREE;

    // We're done with this process - switch back to kernel context
    // This MUST not return - we context switch away
    current_pid = -1;

    // Switch directly back to kernel context
    // This will resume in process_exec_args() or process_schedule()
    // wherever the kernel was waiting
    context_switch(&proc->context, &kernel_context);

    // Should never reach here
    printf("[PROC] ERROR: process_exit returned!\n");
    while(1);
}

// Yield - voluntarily give up CPU
void process_yield(void) {
    if (current_pid >= 0) {
        // Mark current process as ready
        process_t *proc = &proc_table[current_pid];
        proc->state = PROC_STATE_READY;
    }
    // Always try to schedule - even from kernel context
    // This lets programs started via process_exec() yield to spawned children
    process_schedule();
}

// Simple round-robin scheduler
void process_schedule(void) {
    int old_pid = current_pid;
    process_t *old_proc = (old_pid >= 0) ? &proc_table[old_pid] : NULL;

    // Find next runnable process (round-robin)
    int start = (old_pid >= 0) ? old_pid + 1 : 0;
    int next = -1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if (proc_table[idx].state == PROC_STATE_READY) {
            next = idx;
            break;
        }
    }

    if (next < 0) {
        // No runnable processes
        if (old_pid >= 0 && old_proc->state == PROC_STATE_RUNNING) {
            // Current process still running, keep it
            return;
        }
        // Return to kernel (if we were in a process, switch back to kernel)
        if (old_pid >= 0) {
            current_pid = -1;
            context_switch(&old_proc->context, &kernel_context);
        }
        // Already in kernel with nothing to run - sleep until next interrupt
        // This prevents busy-waiting when all processes are waiting for I/O
        asm volatile("wfi");
        return;
    }

    if (next == old_pid && old_proc && old_proc->state == PROC_STATE_RUNNING) {
        // Same process and it's running - nothing to switch
        // But if it just yielded (state == READY), it's waiting for something
        // so we should sleep before giving it back the CPU
        return;
    }

    if (next == old_pid && old_proc && old_proc->state == PROC_STATE_READY) {
        // Process yielded but it's the only one - sleep until interrupt
        // then let it run again. This prevents busy-wait loops.
        asm volatile("wfi");
        old_proc->state = PROC_STATE_RUNNING;
        return;
    }

    // Switch to new process
    process_t *new_proc = &proc_table[next];

    if (old_proc && old_proc->state == PROC_STATE_RUNNING) {
        old_proc->state = PROC_STATE_READY;
    }

    new_proc->state = PROC_STATE_RUNNING;
    current_pid = next;

    // Context switch!
    // If old_pid == -1, we're switching FROM kernel context
    cpu_context_t *old_ctx = (old_pid >= 0) ? &old_proc->context : &kernel_context;
    context_switch(old_ctx, &new_proc->context);
}

// Execute and wait - creates a real process and waits for it to finish
int process_exec_args(const char *path, int argc, char **argv) {
    // Create the process
    int pid = process_create(path, argc, argv);
    if (pid < 0) {
        return pid;  // Error already printed
    }

    // Start it
    process_start(pid);

    // Find the slot for this process
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        printf("[PROC] exec: process disappeared?\n");
        return -1;
    }

    // Wait for it to finish by yielding until it's done
    // The process is READY, we need to run the scheduler to let it execute
    while (proc_table[slot].state != PROC_STATE_FREE &&
           proc_table[slot].state != PROC_STATE_ZOMBIE) {
        process_schedule();
    }

    int result = proc_table[slot].exit_status;
    printf("[PROC] Process '%s' (pid %d) finished with status %d\n", path, pid, result);
    return result;
}

int process_exec(const char *path) {
    char *argv[1] = { (char *)path };
    return process_exec_args(path, 1, argv);
}

// Called from IRQ handler for preemptive scheduling
// This runs after all registers are saved on the IRQ stack
void process_schedule_from_irq(void) {
    // Only reschedule if we have multiple processes
    int ready_count = process_count_ready();
    if (ready_count <= 1) {
        return;  // No point switching
    }

    // Call the normal scheduler
    process_schedule();
}

// Kill a process by PID
int process_kill(int pid) {
    // Don't allow killing kernel (pid would be invalid anyway)
    if (pid <= 0) {
        printf("[PROC] Cannot kill pid %d\n", pid);
        return -1;
    }

    // Find the process
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_STATE_FREE) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        printf("[PROC] Process %d not found\n", pid);
        return -1;
    }

    process_t *proc = &proc_table[slot];

    // Don't allow killing the current process this way - use exit() instead
    if (slot == current_pid) {
        printf("[PROC] Cannot kill current process (use exit)\n");
        return -1;
    }

    printf("[PROC] Killing process '%s' (pid %d)\n", proc->name, pid);

    // Free the process memory
    if (proc->stack_base) {
        free(proc->stack_base);
        proc->stack_base = NULL;
    }

    // Mark slot as free
    proc->state = PROC_STATE_FREE;
    proc->pid = 0;

    return 0;
}
