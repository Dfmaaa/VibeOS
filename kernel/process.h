/*
 * VibeOS Process Management
 *
 * Cooperative multitasking - programs call yield() voluntarily.
 * Classic Mac OS / Windows 3.1 style.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define PROCESS_NAME_MAX 32
#define PROCESS_STACK_SIZE 0x100000  // 1MB per process (TLS crypto needs lots of stack)
#define MAX_PROCESSES 16

// Process states
typedef enum {
    PROC_STATE_FREE = 0,     // Slot available
    PROC_STATE_READY,        // Ready to run
    PROC_STATE_RUNNING,      // Currently executing
    PROC_STATE_BLOCKED,      // Waiting for something
    PROC_STATE_ZOMBIE        // Exited, waiting to be cleaned up
} proc_state_t;

// Saved CPU context for context switching
typedef struct {
    // General purpose callee-saved registers
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;  // Frame pointer
    uint64_t x30;  // Link register (return address)
    uint64_t sp;   // Stack pointer
    // FPU state
    uint64_t fpcr;
    uint64_t fpsr;
    uint64_t _pad;  // Padding to align fp_regs to 16 bytes (offset 0x80)
    uint64_t fp_regs[64];  // q0-q31 (each 128-bit = 2 x 64-bit)
} __attribute__((aligned(16))) cpu_context_t;

typedef struct process {
    int pid;
    char name[PROCESS_NAME_MAX];
    proc_state_t state;

    // Memory
    uint64_t load_base;       // Where program code is loaded
    uint64_t load_size;       // Size of loaded code
    void *stack_base;         // Stack allocation base
    uint64_t stack_size;      // Stack size

    // Execution
    uint64_t entry;           // Entry point
    cpu_context_t context;    // Saved registers for context switch

    // Exit
    int exit_status;
    int parent_pid;           // Who spawned us
} process_t;

// Initialize process subsystem
void process_init(void);

// Create a new process from ELF path (does NOT start it yet)
int process_create(const char *path, int argc, char **argv);

// Start a created process (makes it ready to run)
int process_start(int pid);

// Execute and wait (old behavior - run to completion)
int process_exec(const char *path);
int process_exec_args(const char *path, int argc, char **argv);

// Exit current process
void process_exit(int status);

// Get current/specific process
process_t *process_current(void);
process_t *process_get(int pid);

// Scheduling
void process_yield(void);              // Give up CPU voluntarily
void process_schedule(void);           // Pick next process to run
void process_schedule_from_irq(void);  // Called from timer IRQ for preemption
int process_count_ready(void);         // Count runnable processes

// Context switch (implemented in assembly)
void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

// Get info about process by index (for sysmon)
// Returns 1 if slot is active, 0 if free
int process_get_info(int index, char *name, int name_size, int *state);

// Kill a process by PID
// Returns 0 on success, -1 if not found or cannot kill
int process_kill(int pid);

#endif
