/*
 * HakuXP native core - the title's clocks the same every run, for playbooks.
 *
 * Normally the guest's RDTSC (FEX: the host's counter, scaled to 733 MHz)
 * and the kernel's time (KeQuerySystemTime, KeQueryInterruptTime,
 * KeQueryPerformanceCounter) follow the host's clock, so a title that
 * seeds or steps its random numbers by them goes another way every run:
 * Soulcalibur II's computer opponent did, with no input at all (2026-11-17).
 *
 * While a playbook plays or records (on from the core's start: playbook_jni
 * starts it before the core boots), the clock is the title's own vblanks:
 *   - each vblank the title takes (on its CPU's thread, as the kernel
 *     dispatches the GPU's line) sets the floor: the start's time plus the
 *     vblanks since times the frame's period;
 *   - a read gives max(floor, the reading thread's last + a step): each
 *     guest thread's sequence of values is the same every run, whatever the
 *     other threads read meanwhile, and never goes back for that thread
 *     (between threads, within a frame, it can);
 *   - RDTSC is hooked where the title's code has it (0F 31, found as FEX
 *     translates: core.c's hook query) and answered here.
 * The start: a savestate's own TSC and time, exactly; a boot's TSC 0, time
 * 0 since boot and a fixed date (NC_DETCLOCK_EPOCH), not the phone's.
 *
 * Not covered: the kernel's own scheduling (waits, timers, slices) runs on
 * the host's clock still, so which frame a timer fires in can differ; the
 * tick counts the kernel writes to guest memory (KeTickCount and the rest)
 * follow the host's clock too.
 */
#ifndef NATIVE_KERNEL_DETCLOCK_H
#define NATIVE_KERNEL_DETCLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "native/kernel/kernel.h"
#include "native/kernel/detclock_calc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On from now: since_boot_ns and tsc are the guest's at the start. */
void nc_detclock_start(NcKernel *k, uint64_t since_boot_ns, uint64_t tsc);
bool nc_detclock_on(void);

/* The guest CPU's thread, as the kernel is about to dispatch lines: a new
 * vblank moves the floor. */
void nc_detclock_irq(uint32_t lines);

/* Translation: whether the instruction at eip is an RDTSC to answer here. */
bool nc_detclock_hook_query(uint32_t eip);
/* The hook at eip: true when it answered an RDTSC (EDX:EAX, EIP past it). */
bool nc_detclock_hook(NcKernel *k, NcFrame *f, uint32_t eip);

/* A guest-facing time read (ns since boot): the reading thread's next value.
 * peek: the floor alone, no step (the kernel's own reads). False: off. */
bool nc_detclock_since_boot_ns(bool peek, uint64_t *ns);

#ifdef __cplusplus
}
#endif

#endif
