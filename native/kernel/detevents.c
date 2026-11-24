/*
 * HakuXP native core - a playbook's events at the title's own points
 * (detevents.h). The guest CPU's thread.
 */
#include "native/kernel/detevents.h"
#include "native/kernel/detevents_calc.h"

#include "native/core/nc.h"
#include "native/cpu/cpu.h"
#include "native/dev/dev.h"
#include "native/kernel/kernel.h"
#include "hw/xbox/hle/host.h"
#include "hw/xbox/hle/pb/pbmode.h"

#include <stdio.h>

static bool g_on;
static uint64_t g_idle_due, g_idle_busy;    /* idle points with a vblank due; of them, the decoder busy */
static uint64_t g_spin_given;               /* vblanks given in a spin */
static uint64_t g_poll_given;               /* vblanks given to a thread polling the clock */
static NcDetevPoll g_poll;

/* The guest at an interrupt check (the clock's, the vblank's), while a
 * vblank is due and the decoder quiescent: the next check finding it the
 * same, nothing but its own code between them, is a spin. */
static struct {
    bool valid;
    unsigned thread;
    uint32_t eip, eflags, gpr[8];
} g_snap;

#define NOTES 16
static struct {
    uint16_t op, ordinal;
    uint32_t eip;
    unsigned thread;
} g_note[NOTES];
static unsigned g_nnote;

static void note(unsigned op, uint32_t eip, unsigned thread, unsigned ordinal)
{
    const unsigned i = g_nnote++ % NOTES;
    g_note[i].op = (uint16_t)op;
    g_note[i].ordinal = (uint16_t)ordinal;
    g_note[i].eip = eip;
    g_note[i].thread = thread;
}

/* The due vblank to the guest, and the APU's frames for its time
 * (native/dev/apu/lockstep.c) */
static void give(bool forced)
{
    nc_dev_nv2a_vblank_deliver(forced);
    const int64_t period = hle_host_vblank_period_ns();
    nc_dev_apu_vblank(period > 0 ? (uint64_t)period : 16683333u);
}

static bool spinning(NcFrame *f, unsigned thread)
{
    const bool was = g_snap.valid;
    bool same = was && g_snap.thread == thread && g_snap.eip == nc_frame_eip(f) &&
                g_snap.eflags == nc_frame_eflags(f);
    g_snap.thread = thread;
    g_snap.eip = nc_frame_eip(f);
    g_snap.eflags = nc_frame_eflags(f);
    for (int r = 0; r < 8; r++) {
        const uint32_t v = nc_frame_gpr(f, r);
        same = same && g_snap.gpr[r] == v;
        g_snap.gpr[r] = v;
    }
    g_snap.valid = true;
    return same;
}

void nc_detev_sysop(NcFrame *f, unsigned op, unsigned thread, unsigned ordinal)
{
    note(op, nc_frame_eip(f), thread, ordinal);
    /* a thread polling the clock (detevents_calc.h): the due vblank at this
     * read; its line is served on the way back from the read (kernel.c,
     * nc_intr_dispatch), so the thread's next read sees the new floor */
    if (op != NC_SYSOP_INTERRUPT && nc_detev_poll(&g_poll, thread, ordinal) &&
        nc_dev_nv2a_vblank_due_ns() && hle_pb_quiescent()) {
        nc_detev_poll_reset(&g_poll);
        g_snap.valid = false;
        if (g_poll_given++ % 1000 == 0) {
            nc_log("hakuX-native-playbook", "events: vblank given to t%u polling the clock (k%u, %u reads in "
                   "a row; %llu so far)", thread, ordinal, NC_DETEV_CLOCK_POLLS,
                   (unsigned long long)g_poll_given);
        }
        give(false);
        return;
    }
    if (op != NC_SYSOP_INTERRUPT || !nc_dev_nv2a_vblank_due_ns() || !hle_pb_quiescent()) {
        g_snap.valid = false;   /* a kernel call, a hook: not a spin of its own code */
        return;
    }
    if (spinning(f, thread)) {
        g_snap.valid = false;
        g_spin_given++;
        give(false);
    }
}

static void notes_text(char *buf, size_t n)
{
    size_t o = 0;
    buf[0] = 0;
    const unsigned have = g_nnote < NOTES ? g_nnote : NOTES;
    for (unsigned k = 0; k < have && o + 32 < n; k++) {
        const unsigned i = (g_nnote - have + k) % NOTES;
        o += (size_t)snprintf(buf + o, n - o, " t%u:%u%s%u@%08x", g_note[i].thread, g_note[i].op,
                              g_note[i].ordinal ? "/k" : "", g_note[i].ordinal, g_note[i].eip);
    }
}

/* The due vblank (the host's time it came) and the guest's time when it
 * was first seen due: the guest's time stands still while paused or saving
 * (nc_ke_now_ns), which mustn't force one. */
static uint64_t g_due, g_due_seen;

void nc_detev_start(void)
{
    nc_dev_nv2a_vblank_gate(true);
    hle_pb_lockstep(true);
    nc_dev_apu_lockstep(true, nc_ke_told_ns(false));
    g_on = true;
    nc_log("hakuX-native-playbook", "events: the vblank when the title waits (a playbook's)");
}

bool nc_detev_on(void)
{
    return g_on;
}

int nc_detev_idle(void)
{
    if (!g_on || !nc_dev_nv2a_vblank_due_ns()) {
        return NC_DETEV_NONE;
    }
    g_idle_due++;
    if (!hle_pb_quiescent()) {
        g_idle_busy++;
        return NC_DETEV_WAIT;
    }
    give(false);
    return NC_DETEV_GIVEN;
}

void nc_detev_check(void)
{
    if (!g_on) {
        return;
    }
    const uint64_t due = nc_dev_nv2a_vblank_due_ns();
    if (!due) {
        return;
    }
    const uint64_t now = nc_ke_now_ns();
    if (due != g_due) {
        g_due = due;
        g_due_seen = now;
        return;
    }
    if (now - g_due_seen < NC_DETEV_FORCE_NS) {
        return;
    }
    give(true);
    const uint64_t n = nc_dev_nv2a_vblanks_forced();
    if (n <= 3 || n % 100 == 0) {
        char why[160], notes[640];
        hle_pb_quiescent_why(why, sizeof(why));
        notes_text(notes, sizeof(notes));
        nc_log("hakuX-native-playbook", "events: the last system ops (thread:op[/k ordinal]@eip):%s", notes);
        nc_log("hakuX-native-playbook", "events: vblank given while the title ran (%llu so far): it didn't wait "
               "for %u ms, so this run's timing isn't the title's own here; idle points with one due %llu, the "
               "decoder busy at %llu; given in a spin %llu, to a clock poll %llu; decoder now: %s",
               (unsigned long long)n, (unsigned)(NC_DETEV_FORCE_NS / 1000000u), (unsigned long long)g_idle_due,
               (unsigned long long)g_idle_busy, (unsigned long long)g_spin_given,
               (unsigned long long)g_poll_given, why);
    }
}
