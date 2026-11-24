/*
 * HakuXP native core - debug: page hashes of a guest range each vblank
 * (memtrace.h). The guest CPU's thread only.
 */
#include "native/kernel/memtrace.h"

#include "native/core/nc.h"
#include "native/core/vm.h"
#include "native/dev/dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV2A_LINE 3
#define PAGE 4096u
#define MAX_PAGES 4096u     /* 16 MiB */

static struct {
    FILE *f;
    uint32_t lo, hi;
    uint64_t v0, v;
    bool ticked;
    uint32_t *rec;
    uint32_t dump_at;   /* NC_TRACE_MEMDUMP: that vblank's bytes too; 0 none */
    char path[480];
} g_mt;

void nc_memtrace_init(void)
{
    const char *e = getenv("NC_TRACE_MEM");
    const char *at = e ? strchr(e, '@') : NULL;
    if (!at) {
        return;
    }
    char *end = NULL;
    const uint32_t lo = (uint32_t)strtoul(e, &end, 16) & ~(PAGE - 1);
    const uint32_t hi = end && *end == '-' ? (uint32_t)strtoul(end + 1, NULL, 16) : 0;
    if (hi <= lo || (hi - lo) / PAGE > MAX_PAGES) {
        nc_log("hakuX-native-trace", "NC_TRACE_MEM: a range lo-hi of up to %u pages, then @file", MAX_PAGES);
        return;
    }
    g_mt.rec = malloc(((hi - lo + PAGE - 1) / PAGE + 2) * sizeof(uint32_t));
    g_mt.f = g_mt.rec ? fopen(at + 1, "wb") : NULL;
    if (!g_mt.f) {
        nc_log("hakuX-native-trace", "NC_TRACE_MEM: can't write %s", at + 1);
        free(g_mt.rec);
        g_mt.rec = NULL;
        return;
    }
    g_mt.lo = lo;
    g_mt.hi = hi;
    snprintf(g_mt.path, sizeof(g_mt.path), "%s", at + 1);
    const char *d = getenv("NC_TRACE_MEMDUMP");
    g_mt.dump_at = d ? (uint32_t)strtoul(d, NULL, 10) : 0;
    const uint32_t head[3] = {0x544D5848u /* "HXMT" */, lo, hi};
    fwrite(head, sizeof(head), 1, g_mt.f);
    nc_log("hakuX-native-trace", "page hashes of %08x-%08x each vblank to %s", lo, hi, at + 1);
}

static uint32_t page_hash(const uint8_t *p)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < PAGE; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    const uint32_t x = (uint32_t)(h ^ (h >> 32));
    return x ? x : 1;   /* 0: unmapped */
}

static void write_record(NcKernel *k, uint32_t vblank)
{
    uint32_t n = 0;
    for (uint32_t va = g_mt.lo; va < g_mt.hi; va += PAGE) {
        const bool mapped = nc_vm_phys(k->vm, va) != UINT32_MAX;
        g_mt.rec[2 + n++] = mapped ? page_hash(nc_vm_host(k->vm, va)) : 0;
    }
    g_mt.rec[0] = vblank;
    g_mt.rec[1] = n;
    fwrite(g_mt.rec, sizeof(uint32_t), 2 + n, g_mt.f);
    fflush(g_mt.f);     /* the app may be closed at any time */
}

/* The range's bytes as they are (unmapped pages as zeros), to <file>.v<N> */
static void write_dump(NcKernel *k, uint32_t vblank)
{
    char p[512];
    snprintf(p, sizeof(p), "%s.v%u", g_mt.path, vblank);
    FILE *f = fopen(p, "wb");
    if (!f) {
        return;
    }
    static const uint8_t zero[PAGE];
    for (uint32_t va = g_mt.lo; va < g_mt.hi; va += PAGE) {
        const bool mapped = nc_vm_phys(k->vm, va) != UINT32_MAX;
        fwrite(mapped ? (const uint8_t *)nc_vm_host(k->vm, va) : zero, 1, PAGE, f);
    }
    fclose(f);
    nc_log("hakuX-native-trace", "vblank %u's %08x-%08x to %s", vblank, g_mt.lo, g_mt.hi, p);
}

void nc_memtrace_irq(NcKernel *k, uint32_t lines)
{
    if (!g_mt.f || !(lines & (1u << NV2A_LINE))) {
        return;
    }
    const uint64_t v = nc_dev_nv2a_vblanks();
    if (g_mt.ticked && v == g_mt.v) {
        return;
    }
    if (!g_mt.ticked) {
        g_mt.ticked = true;
        g_mt.v0 = v;
    }
    g_mt.v = v;
    write_record(k, (uint32_t)(v - g_mt.v0));
    if (g_mt.dump_at && v - g_mt.v0 == g_mt.dump_at) {
        write_dump(k, g_mt.dump_at);
    }
}
