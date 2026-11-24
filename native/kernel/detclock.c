/*
 * HakuXP native core - the title's clocks the same every run (detclock.h).
 *
 * Everything here runs on the guest CPU's thread (the hooks, the kernel's
 * calls and its interrupt dispatch); only the on flag is read elsewhere.
 */
#include "native/kernel/detclock.h"

#include "native/core/nc.h"
#include "native/dev/dev.h"
#include "hw/xbox/hle/host.h"

#define NV2A_LINE 3

static struct {
    bool on;                    /* atomic */
    NcKernel *k;
    NcVm *vm;
    uint64_t base_ns, base_tsc; /* at the start */
    bool ticked;
    uint64_t v0, v;             /* the first tick's vblank count, the last's */
    uint64_t floor_ns, floor_tsc;
    uint64_t idle_tsc, idle_ns; /* reads with no current thread */
    uint64_t hooks;
} g_dc;

void nc_detclock_start(NcKernel *k, uint64_t since_boot_ns, uint64_t tsc)
{
    g_dc.k = k;
    g_dc.vm = k->vm;
    g_dc.base_ns = g_dc.floor_ns = since_boot_ns;
    g_dc.base_tsc = g_dc.floor_tsc = tsc;
    g_dc.ticked = false;
    g_dc.idle_tsc = g_dc.idle_ns = 0;
    for (unsigned i = 0; i < NC_MAX_THREADS; i++) {
        k->threads[i].det_tsc = k->threads[i].det_ns = 0;
    }
    __atomic_store_n(&g_dc.on, true, __ATOMIC_RELEASE);
    nc_log("hakuX-native-playbook", "clock: the title's own vblanks from %.3f s since boot, TSC %llu (RDTSC and the "
           "kernel's time the same every run)", since_boot_ns / 1e9, (unsigned long long)tsc);
}

bool nc_detclock_on(void)
{
    return __atomic_load_n(&g_dc.on, __ATOMIC_ACQUIRE);
}

void nc_detclock_irq(uint32_t lines)
{
    if (!(lines & (1u << NV2A_LINE)) || !nc_detclock_on()) {
        return;
    }
    const uint64_t v = nc_dev_nv2a_vblanks();
    if (!g_dc.ticked) {
        g_dc.ticked = true;
        g_dc.v0 = g_dc.v = v;
        return;
    }
    if (v == g_dc.v) {
        return;     /* the GPU's other interrupts */
    }
    g_dc.v = v;
    const int64_t period = hle_host_vblank_period_ns();
    uint64_t ns, tsc;
    nc_detclock_floor(g_dc.base_ns, g_dc.base_tsc, v - g_dc.v0, period > 0 ? (uint64_t)period : 16683333u, &ns,
                      &tsc);
    if (ns > g_dc.floor_ns) {
        g_dc.floor_ns = ns;
    }
    if (tsc > g_dc.floor_tsc) {
        g_dc.floor_tsc = tsc;
    }
}

bool nc_detclock_hook_query(uint32_t eip)
{
    if (!nc_detclock_on() || !g_dc.vm || nc_vm_phys(g_dc.vm, eip) == UINT32_MAX ||
        nc_vm_phys(g_dc.vm, eip + 1) == UINT32_MAX) {
        return false;
    }
    const uint8_t *p = nc_vm_host(g_dc.vm, eip);
    const uint8_t *q = nc_vm_host(g_dc.vm, eip + 1);
    return p[0] == 0x0F && q[0] == 0x31;
}

bool nc_detclock_hook(NcKernel *k, NcFrame *f, uint32_t eip)
{
    if (!nc_detclock_hook_query(eip)) {
        return false;
    }
    NcThread *t = k->current;
    const uint64_t v = nc_detclock_next(g_dc.floor_tsc, t ? &t->det_tsc : &g_dc.idle_tsc, NC_DETCLOCK_TSC_STEP);
    nc_frame_set_gpr(f, NC_EAX, (uint32_t)v);
    nc_frame_set_gpr(f, NC_EDX, (uint32_t)(v >> 32));
    nc_frame_set_eip(f, eip + 2);
    if (g_dc.hooks++ == 0) {
        nc_log("hakuX-native-playbook", "clock: the title's first RDTSC answered at %08x", eip);
    }
    return true;
}

bool nc_detclock_since_boot_ns(bool peek, uint64_t *ns)
{
    NcKernel *k = g_dc.k;
    if (!nc_detclock_on()) {
        return false;
    }
    if (peek) {
        *ns = g_dc.floor_ns;
        return true;
    }
    NcThread *t = k->current;
    *ns = nc_detclock_next(g_dc.floor_ns, t ? &t->det_ns : &g_dc.idle_ns, NC_DETCLOCK_NS_STEP);
    return true;
}
