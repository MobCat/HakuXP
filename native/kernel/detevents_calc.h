/*
 * HakuXP native core - detevents.h's count of a thread polling the clock.
 * Pure, for the host test.
 *
 * In a playbook the kernel's clock moves only with the vblanks the guest
 * takes (detclock.h), and the vblank waits for the title's own point
 * (detevents.h). A thread waiting for a moment by reading the clock over
 * and over is neither waiting nor a spin of the same state (each read is a
 * kernel call, each value another), so it waited for NC_DETEV_FORCE_NS:
 * TransWorld Snowboarding's sound loader reads KeQuerySystemTime in a loop
 * (0x1E160C) after each trick sound, and every one flipped a frame 100 ms
 * late (2026-11-18). Counted in the guest's own reads, the point is the
 * same every run.
 */
#ifndef NATIVE_KERNEL_DETEVENTS_CALC_H
#define NATIVE_KERNEL_DETEVENTS_CALC_H

#include <stdbool.h>

/* A thread's clock reads in a row, no other system op between, that make
 * it a poll */
#define NC_DETEV_CLOCK_POLLS 64u

typedef struct NcDetevPoll {
    unsigned thread, n;
} NcDetevPoll;

/* The kernel's clock: KeQueryInterruptTime (125), KeQueryPerformanceCounter
 * (126), KeQuerySystemTime (128). */
static inline bool nc_detev_clock_ordinal(unsigned ordinal)
{
    return ordinal == 125 || ordinal == 126 || ordinal == 128;
}

/* A system op other than an interrupt check (those come on the host's
 * timing and must not count): ordinal is the kernel call's, 0 for another
 * op. True while thread has read the clock NC_DETEV_CLOCK_POLLS times or
 * more in a row; nc_detev_poll_reset when the vblank is given. */
static inline bool nc_detev_poll(NcDetevPoll *p, unsigned thread, unsigned ordinal)
{
    if (!nc_detev_clock_ordinal(ordinal)) {
        p->n = 0;
        return false;
    }
    if (p->n && p->thread != thread) {
        p->n = 0;
    }
    p->thread = thread;
    if (p->n < NC_DETEV_CLOCK_POLLS) {
        p->n++;
    }
    return p->n >= NC_DETEV_CLOCK_POLLS;
}

static inline void nc_detev_poll_reset(NcDetevPoll *p)
{
    p->n = 0;
}

#endif
