/*
 * HakuXP native core - debug: one guest function's calls by frame
 * (calltrace.h). The guest CPU's thread only.
 */
#include "native/kernel/calltrace.h"

#include "native/core/nc.h"
#include "native/dev/dev.h"

#include <stdio.h>
#include <stdlib.h>

#define NV2A_LINE 3
#define SITES 32

static struct {
    bool on;
    uint32_t eip;
    uint64_t v0, v;
    bool ticked;
    unsigned calls;
    struct {
        uint32_t ret;
        unsigned thread, n;
    } site[SITES];
    unsigned nsite;
} g_ct;

void nc_calltrace_init(void)
{
    const char *e = getenv("NC_TRACE_EIP");
    if (!e || !*e) {
        return;
    }
    g_ct.eip = (uint32_t)strtoul(e, NULL, 16);
    g_ct.on = g_ct.eip != 0;
    if (g_ct.on) {
        nc_log("hakuX-native-trace", "tracing the calls of %08x, a line each vblank it ran", g_ct.eip);
    }
}

bool nc_calltrace_hook_query(uint32_t eip)
{
    return g_ct.on && eip == g_ct.eip;
}

bool nc_calltrace_hook(NcKernel *k, NcFrame *f, uint32_t eip)
{
    if (!g_ct.on || eip != g_ct.eip) {
        return false;
    }
    const uint32_t esp = nc_frame_gpr(f, NC_ESP);
    const uint32_t ret = nc_vm_phys(k->vm, esp) != UINT32_MAX ? nc_vm_rd32(k->vm, esp) : 0;
    const unsigned thread = k->current ? k->current->id : 0;
    g_ct.calls++;
    unsigned i = 0;
    while (i < g_ct.nsite && !(g_ct.site[i].ret == ret && g_ct.site[i].thread == thread)) {
        i++;
    }
    if (i < SITES) {
        if (i == g_ct.nsite) {
            g_ct.site[i].ret = ret;
            g_ct.site[i].thread = thread;
            g_ct.site[i].n = 0;
            g_ct.nsite++;
        }
        g_ct.site[i].n++;
    }
    nc_frame_bypass_hook(f);    /* the instruction runs as it is */
    return true;
}

void nc_calltrace_irq(uint32_t lines)
{
    if (!g_ct.on || !(lines & (1u << NV2A_LINE))) {
        return;
    }
    const uint64_t v = nc_dev_nv2a_vblanks();
    if (!g_ct.ticked) {
        g_ct.ticked = true;
        g_ct.v0 = g_ct.v = v;
        return;
    }
    if (v == g_ct.v) {
        return;
    }
    if (g_ct.calls) {
        char line[1024];
        int n = snprintf(line, sizeof(line), "eip %08x vblank %llu: %u calls:", g_ct.eip,
                         (unsigned long long)(g_ct.v - g_ct.v0), g_ct.calls);
        for (unsigned i = 0; i < g_ct.nsite && n < (int)sizeof(line) - 40; i++) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " t%u %08xx%u", g_ct.site[i].thread, g_ct.site[i].ret,
                          g_ct.site[i].n);
        }
        nc_log("hakuX-native-trace", "%s", line);
    }
    g_ct.v = v;
    g_ct.calls = 0;
    g_ct.nsite = 0;
}
