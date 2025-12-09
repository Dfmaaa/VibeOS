/*
 * VibeOS Interrupt Handling
 *
 * GIC-400 driver for QEMU virt machine.
 * Uses GICv2 interface at 0x08000000.
 */

#include "irq.h"
#include "printf.h"
#include "keyboard.h"
#include "virtio_sound.h"

// QEMU virt machine GIC addresses
#define GICD_BASE   0x08000000UL  // Distributor
#define GICC_BASE   0x08010000UL  // CPU Interface

// GIC Distributor registers
#define GICD_CTLR       (*(volatile uint32_t *)(GICD_BASE + 0x000))  // Control
#define GICD_TYPER      (*(volatile uint32_t *)(GICD_BASE + 0x004))  // Type
#define GICD_IGROUPR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x080 + (n)*4))  // Group (0=Secure, 1=Non-Secure)
#define GICD_ISENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x100 + (n)*4))  // Set-Enable
#define GICD_ICENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x180 + (n)*4))  // Clear-Enable
#define GICD_ISPENDR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x200 + (n)*4))  // Set-Pending
#define GICD_ICPENDR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x280 + (n)*4))  // Clear-Pending
#define GICD_IPRIORITYR(n) (*(volatile uint32_t *)(GICD_BASE + 0x400 + (n)*4)) // Priority
#define GICD_ITARGETSR(n)  (*(volatile uint32_t *)(GICD_BASE + 0x800 + (n)*4)) // Target CPU
#define GICD_ICFGR(n)      (*(volatile uint32_t *)(GICD_BASE + 0xC00 + (n)*4)) // Config

// GIC CPU Interface registers
#define GICC_CTLR   (*(volatile uint32_t *)(GICC_BASE + 0x000))  // Control
#define GICC_PMR    (*(volatile uint32_t *)(GICC_BASE + 0x004))  // Priority Mask
#define GICC_IAR    (*(volatile uint32_t *)(GICC_BASE + 0x00C))  // Interrupt Acknowledge
#define GICC_EOIR   (*(volatile uint32_t *)(GICC_BASE + 0x010))  // End of Interrupt

// Timer registers (Generic Timer)
#define CNTFRQ_EL0      "cntfrq_el0"
#define CNTP_CTL_EL0    "cntp_ctl_el0"
#define CNTP_TVAL_EL0   "cntp_tval_el0"
#define CNTP_CVAL_EL0   "cntp_cval_el0"
#define CNTPCT_EL0      "cntpct_el0"

// Timer IRQ (EL1 Physical Timer is PPI 30, which is IRQ 30)
#define TIMER_IRQ   30

// Virtio IRQs start at SPI 32 (IRQ 32+)
#define VIRTIO_IRQ_BASE 48

// Maximum number of IRQs we support
#define MAX_IRQS    128

// IRQ handlers
static irq_handler_t irq_handlers[MAX_IRQS] = {0};

// Timer state
static uint64_t timer_ticks = 0;
static uint32_t timer_interval_ticks = 0;
static uint32_t timer_freq = 0;

// Memory barrier
static inline void dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}

static inline void dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

static inline void isb(void) {
    asm volatile("isb" ::: "memory");
}

// Read system register
static inline uint64_t read_sysreg(const char *reg) {
    uint64_t val;
    if (__builtin_strcmp(reg, CNTFRQ_EL0) == 0) {
        asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    } else if (__builtin_strcmp(reg, CNTP_CTL_EL0) == 0) {
        asm volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    } else if (__builtin_strcmp(reg, CNTP_TVAL_EL0) == 0) {
        asm volatile("mrs %0, cntp_tval_el0" : "=r"(val));
    } else if (__builtin_strcmp(reg, CNTPCT_EL0) == 0) {
        asm volatile("mrs %0, cntpct_el0" : "=r"(val));
    }
    return val;
}

// Write system register
static inline void write_sysreg(const char *reg, uint64_t val) {
    if (__builtin_strcmp(reg, CNTP_CTL_EL0) == 0) {
        asm volatile("msr cntp_ctl_el0, %0" :: "r"(val));
    } else if (__builtin_strcmp(reg, CNTP_TVAL_EL0) == 0) {
        asm volatile("msr cntp_tval_el0, %0" :: "r"(val));
    } else if (__builtin_strcmp(reg, CNTP_CVAL_EL0) == 0) {
        asm volatile("msr cntp_cval_el0, %0" :: "r"(val));
    }
    isb();
}

void irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");  // Clear IRQ mask
}

void irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");  // Set IRQ mask
}

void irq_enable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    dsb();
    GICD_ISENABLER(reg) = (1 << bit);
    dsb();
}

void irq_disable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    dsb();
    GICD_ICENABLER(reg) = (1 << bit);
    dsb();
}

void irq_register_handler(uint32_t irq, irq_handler_t handler) {
    if (irq < MAX_IRQS) {
        irq_handlers[irq] = handler;
    }
}

void irq_init(void) {
    printf("[IRQ] Initializing GIC...\n");

    // Disable distributor while configuring
    dsb();
    GICD_CTLR = 0;
    dsb();

    // Get number of IRQ lines
    uint32_t typer = GICD_TYPER;
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;
    printf("[IRQ] GIC supports %d IRQs\n", num_irqs);

    // Disable all IRQs
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_ICENABLER(i) = 0xFFFFFFFF;
    }
    dsb();

    // Clear all pending IRQs
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_ICPENDR(i) = 0xFFFFFFFF;
    }
    dsb();

    // Set all SPIs to Group 0 (Secure)
    // We're running in Secure EL1, so interrupts must be Group 0
    // Group 0 interrupts are delivered as IRQ in Secure state
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_IGROUPR(i) = 0x00000000;  // All bits = 0 means Group 0 (Secure)
    }
    dsb();
    printf("[IRQ] All interrupts set to Group 0 (Secure)\n");

    // Set all IRQs to mid priority
    for (uint32_t i = 0; i < num_irqs / 4; i++) {
        GICD_IPRIORITYR(i) = 0xA0A0A0A0;  // Priority 160 for all
    }
    dsb();

    // Route all SPIs to CPU 0
    // ITARGETSR registers 0-7 are for SGIs/PPIs (read-only or banked)
    // SPIs start at register 8 (IRQ 32+)
    for (uint32_t i = 8; i < num_irqs / 4; i++) {
        GICD_ITARGETSR(i) = 0x01010101;  // Target CPU 0
    }
    dsb();
    printf("[IRQ] All SPIs targeted to CPU 0\n");

    // Configure all SPIs as level-sensitive (required for virtio!)
    // ICFGR registers: 2 bits per IRQ, 00 = level, 10 = edge
    // Registers 0-1 are for SGIs/PPIs
    for (uint32_t i = 2; i < num_irqs / 16; i++) {
        GICD_ICFGR(i) = 0x00000000;  // Level-sensitive
    }
    dsb();

    // Enable distributor - Group 0 only (we're Secure)
    // Bit 0: Enable Group 0
    GICD_CTLR = 0x1;
    dsb();

    // Configure CPU interface
    GICC_PMR = 0xFF;   // Accept all priority levels (lowest threshold)
    dsb();

    // Enable CPU interface - Group 0 only
    // Bit 0: Enable Group 0
    GICC_CTLR = 0x1;
    dsb();

    printf("[IRQ] GIC initialized (Secure, Group 0)\n");
}

void timer_init(uint32_t interval_ms) {
    // Get timer frequency
    asm volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    printf("[TIMER] Frequency: %u Hz\n", timer_freq);

    // Calculate ticks per interval
    timer_interval_ticks = (timer_freq / 1000) * interval_ms;
    printf("[TIMER] Interval: %u ms (%u ticks)\n", interval_ms, timer_interval_ticks);

    // Set timer value
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval_ticks));
    isb();

    // Enable timer (bit 0 = enable, bit 1 = mask output)
    asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1));
    isb();

    // Enable timer IRQ in GIC
    irq_enable_irq(TIMER_IRQ);

    printf("[TIMER] Timer initialized\n");
}

void timer_set_interval(uint32_t interval_ms) {
    timer_interval_ticks = (timer_freq / 1000) * interval_ms;
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

void wfi(void) {
    asm volatile("wfi");
}

void sleep_ms(uint32_t ms) {
    // Timer runs at 100Hz (10ms per tick)
    // Convert ms to ticks, rounding up
    uint64_t ticks_to_wait = (ms + 9) / 10;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint64_t target = timer_ticks + ticks_to_wait;
    while (timer_ticks < target) {
        wfi();  // Sleep until next interrupt (timer, keyboard, mouse)
    }
}

// Timer IRQ handler
static void timer_handler(void) {
    timer_ticks++;

    // Pump audio if playing
    virtio_sound_pump();

    // Reload timer
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval_ticks));
    isb();
}

// Main IRQ handler - called from vectors.S
void handle_irq(void) {
    dsb();

    // Read interrupt ID
    uint32_t iar = GICC_IAR;
    uint32_t irq = iar & 0x3FF;

    // Check for spurious interrupt
    if (irq == 1023) {
        return;
    }

    // Handle the interrupt
    if (irq == TIMER_IRQ) {
        timer_handler();
    } else if (irq_handlers[irq]) {
        irq_handlers[irq]();
    } else {
        printf("[IRQ] Unhandled IRQ %d\n", irq);
    }

    // Signal end of interrupt
    dsb();
    GICC_EOIR = iar;
    dsb();
}

// Synchronous exception handler
void handle_sync_exception(uint64_t esr, uint64_t elr, uint64_t far) {
    uint32_t ec = (esr >> 26) & 0x3F;   // Exception Class
    uint32_t iss = esr & 0x1FFFFFF;      // Instruction Specific Syndrome

    printf("\n");
    printf("==========================================\n");
    printf("  KERNEL PANIC: Synchronous Exception\n");
    printf("==========================================\n");
    printf("  ESR_EL1: 0x%08lx\n", esr);
    printf("  ELR_EL1: 0x%016lx (return address)\n", elr);
    printf("  FAR_EL1: 0x%016lx (fault address)\n", far);
    printf("\n");
    printf("  Exception Class (EC): 0x%02x = ", ec);

    switch (ec) {
        case 0x00: printf("Unknown reason\n"); break;
        case 0x01: printf("Trapped WFI/WFE\n"); break;
        case 0x0E: printf("Illegal execution state\n"); break;
        case 0x15: printf("SVC instruction (syscall)\n"); break;
        case 0x20: printf("Instruction abort from lower EL\n"); break;
        case 0x21: printf("Instruction abort from current EL\n"); break;
        case 0x22: printf("PC alignment fault\n"); break;
        case 0x24: printf("Data abort from lower EL\n"); break;
        case 0x25: printf("Data abort from current EL\n"); break;
        case 0x26: printf("SP alignment fault\n"); break;
        case 0x2C: printf("Floating-point exception\n"); break;
        default:   printf("(see ARM ARM)\n"); break;
    }

    printf("  ISS: 0x%06x\n", iss);

    // For data/instruction aborts, decode more info
    if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
        printf("  Access type: %s\n", (iss & (1 << 6)) ? "Write" : "Read");
        printf("  DFSC/IFSC: 0x%02x\n", iss & 0x3F);
    }

    printf("\n");
    printf("  System halted.\n");
    printf("==========================================\n");

    // Halt
    irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}

// FIQ handler (not used)
void handle_fiq(void) {
    printf("[IRQ] FIQ received (unexpected)\n");
}

// SError handler
void handle_serror(uint64_t esr) {
    printf("\n");
    printf("==========================================\n");
    printf("  KERNEL PANIC: SError (Async Abort)\n");
    printf("==========================================\n");
    printf("  ESR_EL1: 0x%08lx\n", esr);
    printf("  System halted.\n");
    printf("==========================================\n");

    irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}
