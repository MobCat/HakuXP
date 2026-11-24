/*
 * HakuXP native core - a playbook's events at the title's own points.
 *
 * The vblank came from the host's timer: when the title was still busy, it
 * (and the playbook clock's step with it, detclock.h) landed at another
 * point of its frame each run, and Soulcalibur II's fight went another way
 * after a few seconds with no input at all (2026-11-17).
 *
 * While a playbook plays or records, the guest's vblank is an event
 * (native/dev/dev.h nc_dev_nv2a_vblank_gate): the host's vblank thread only
 * counts them and wakes the guest, and the guest gets one
 *   - when every guest thread waits (the scheduler's idle point,
 *     nc_detev_idle) and the GPU's decoder has done what was pushed or waits
 *     on the guest itself (hle_pb_quiescent): the title's state then can't
 *     change before it, whatever the host's timing;
 *   - or when the guest spins with the decoder quiescent: two interrupt
 *     checks in a row (the clock's, about a millisecond apart) find the same
 *     thread at the same EIP with the same registers and flags, no system
 *     op between them. Soulcalibur II never idles: its D3D polls the GPU's
 *     progress at 0x152850 while the decoder waits in FLIP_STALL for that
 *     very vblank, and waiting for an idle point gave it a frame per
 *     NC_DETEV_FORCE_NS, 8 fps (2026-11-17). Any iteration of such a loop
 *     is the same guest state, so the point is still the title's own;
 *   - or when a thread polls the kernel's clock: its NC_DETEV_CLOCK_POLLS-th
 *     read in a row with no other system op between (detevents_calc.h),
 *     the decoder quiescent. The clock moves only with the vblanks here,
 *     so a wait for a moment by reading it never ended before
 *     NC_DETEV_FORCE_NS: TransWorld Snowboarding flipped a frame 100 ms
 *     late after each trick sound (2026-11-18);
 *   - one only, however many the host had meanwhile: a title busy past a
 *     vblank never sees a late frame, it runs slower instead;
 *   - or, for a title that does neither, after NC_DETEV_FORCE_NS anyway
 *     (forced: not at its own point; counted and logged).
 * And the GPU runs in lockstep with the guest (hw/xbox/hle/pb/pbmode.h
 * hle_pb_lockstep): what it does reaches the guest at the guest's own
 * writes, not after however long the decoder took. So does the APU
 * (native/dev/apu/lockstep.c): its frames run with each vblank given.
 *
 * Not covered yet: the kernel's timers and the disc's completions still
 * come on the host's clock.
 */
#ifndef NATIVE_KERNEL_DETEVENTS_H
#define NATIVE_KERNEL_DETEVENTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_DETEV_FORCE_NS 100000000ull  /* 100 ms: a vblank due that long is given while the title runs */

enum { NC_DETEV_NONE, NC_DETEV_GIVEN, NC_DETEV_WAIT };

/* On from now (the core's start, a playbook playing or recording). */
void nc_detev_start(void);
bool nc_detev_on(void);
/* The scheduler, every guest thread waiting: NC_DETEV_GIVEN when it gave
 * the due vblank (dispatch it), NC_DETEV_WAIT when one is due but the
 * decoder is still busy (wait briefly and ask again). */
int nc_detev_idle(void);
/* The kernel's interrupt dispatch: a vblank due for NC_DETEV_FORCE_NS of
 * the guest's time (not counting a pause or a playbook's save) is given now. */
void nc_detev_check(void);
/* Each system op while on (the guest's thread, before the op): an
 * interrupt check may give the due vblank (a spin, above), and so may a
 * clock read (a poll, above). The last few go in a forced vblank's report,
 * to show what the title did instead. */
struct NcFrame;
void nc_detev_sysop(struct NcFrame *f, unsigned op, unsigned thread, unsigned ordinal);

#ifdef __cplusplus
}
#endif

#endif
