/*
 * HakuXP native core - detclock.h's arithmetic: its constants, the floors at
 * a vblank and a thread's next value. Pure, for the host test.
 */
#ifndef NATIVE_KERNEL_DETCLOCK_CALC_H
#define NATIVE_KERNEL_DETCLOCK_CALC_H

#include <stdint.h>

/* 2026-01-01 00:00:00 UTC as a FILETIME (100 ns since 1601): a boot's date. */
#define NC_DETCLOCK_EPOCH 134116992000000000ull
#define NC_DETCLOCK_TSC_HZ 733333333ull
#define NC_DETCLOCK_TSC_STEP 256u       /* cycles a read moves its thread's TSC on (0.35 us) */
#define NC_DETCLOCK_NS_STEP 100u        /* ns a kernel time read moves its thread's time on */

/* The floors at a tick: pure, for the host test. */
static inline void nc_detclock_floor(uint64_t base_ns, uint64_t base_tsc, uint64_t vblanks, uint64_t period_ns,
                                     uint64_t *ns, uint64_t *tsc)
{
    const uint64_t el = vblanks * period_ns;
    *ns = base_ns + el;
    *tsc = base_tsc + (uint64_t)((unsigned __int128)el * NC_DETCLOCK_TSC_HZ / 1000000000u);
}

/* A thread's next value: the floor, or its last plus the step. */
static inline uint64_t nc_detclock_next(uint64_t floor, uint64_t *last, uint64_t step)
{
    const uint64_t v = *last + step > floor ? *last + step : floor;
    *last = v;
    return v;
}

#endif
