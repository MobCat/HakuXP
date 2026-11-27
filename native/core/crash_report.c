/*
 * HakuXP native core - crash report builder.
 */
#include "native/core/crash_report.h"
#include "native/core/mmio.h"
#include "native/core/smc.h"
#include "native/hle/hle_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct ReportBuf {
    char *data;
    size_t len;
    size_t cap;
    bool oom;
} ReportBuf;

static bool buf_reserve(ReportBuf *b, size_t extra)
{
    if (b->oom) {
        return false;
    }
    if (b->len + extra + 1 <= b->cap) {
        return true;
    }
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + extra + 1) {
        cap *= 2;
    }
    char *p = (char *)realloc(b->data, cap);
    if (!p) {
        b->oom = true;
        return false;
    }
    b->data = p;
    b->cap = cap;
    return true;
}

static void buf_append(ReportBuf *b, const char *s, size_t n)
{
    if (!n || !buf_reserve(b, n)) {
        return;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_printf(ReportBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void buf_printf(ReportBuf *b, const char *fmt, ...)
{
    if (b->oom) {
        return;
    }
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0 || !buf_reserve(b, (size_t)need)) {
        va_end(ap2);
        return;
    }
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
}

static bool vm_ready(const NcVm *vm)
{
    return vm && vm->base && vm->pte && vm->phys && vm->ram_size > 0;
}

/* Safe byte read through vm->phys (never triggers guest page protections). */
static bool safe_rd8(const NcVm *vm, uint32_t va, uint8_t *out)
{
    if (!vm_ready(vm)) {
        return false;
    }
    const uint32_t pa = nc_vm_phys(vm, va);
    if (pa == UINT32_MAX || pa >= vm->ram_size) {
        return false;
    }
    *out = vm->phys[pa];
    return true;
}

static bool safe_rd32(const NcVm *vm, uint32_t va, uint32_t *out)
{
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i++) {
        if (!safe_rd8(vm, va + i, &bytes[i])) {
            return false;
        }
    }
    *out = nc_rd32(bytes);
    return true;
}

static const char *fatal_kind_name(int kind)
{
    switch (kind) {
    case NC_FATAL_NONE:     return "NONE";
    case NC_FATAL_DRIVER:   return "DRIVER";
    case NC_FATAL_TITLE:    return "TITLE";
    case NC_FATAL_STATE:    return "STATE";
    case NC_FATAL_GPU_LOST: return "GPU_LOST";
    case NC_FATAL_BOOT:     return "BOOT";
    case NC_FATAL_STOPPED:  return "STOPPED";
    case NC_FATAL_DASHBOARD: return "DASHBOARD";
    case NC_FATAL_MISSING_XBE: return "MISSING_XBE";
    default:                return "UNKNOWN";
    }
}

static void format_va_symbol(const HleXbeInfo *x, uint32_t va, char *out, size_t n)
{
    if (!x || !x->sizeof_image) {
        out[0] = '\0';
        return;
    }
    const HleXbeSection *sec = hle_xbe_section_at(x, va);
    if (sec) {
        snprintf(out, n, " (%s+0x%X, image+0x%X)", sec->name, va - sec->va, va - x->base);
    } else if (va >= x->base && va < x->base + x->sizeof_image) {
        snprintf(out, n, " (image+0x%X)", va - x->base);
    } else {
        out[0] = '\0';
    }
}

static void append_crash_summary(ReportBuf *b, const NcCore *c, bool core_stopped,
                                 const char *fatal_why, int fatal_kind)
{
    buf_printf(b, "[Native Crash Summary]\n");
    buf_printf(b, "Fatal kind: %s (%d)\n", fatal_kind_name(fatal_kind), fatal_kind);
    if (fatal_why && fatal_why[0]) {
        buf_printf(b, "Fatal reason: %s\n", fatal_why);
    }
    if (c && c->kernel.stop && c->kernel.stop_reason[0]) {
        buf_printf(b, "Kernel stop reason: %s\n", c->kernel.stop_reason);
    }
    uint64_t ec = 0;
    if (nc_log_count(&ec)) {
        buf_printf(b, "Emulation count (EC): 0x%08llX (%llu)\n",
                   (unsigned long long)ec, (unsigned long long)ec);
    }
    if (c && core_stopped) {
        const NcKernel *k = &c->kernel;
        unsigned threads = 0;
        for (unsigned i = 0; i < NC_MAX_THREADS; i++) {
            if (k->threads[i].state != NC_T_FREE) {
                threads++;
            }
        }
        const NcKExport *first = nc_kernel_export(k->first_ordinal);
        buf_printf(b, "Kernel stage: %s | calls=%llu | first_call=%s (ord %u) | interrupts=%llu | page_faults=%llu | threads=%u\n",
                   k->first_ordinal ? "S2" : "S1",
                   (unsigned long long)k->calls,
                   first ? first->name : "-", k->first_ordinal,
                   (unsigned long long)k->interrupts,
                   (unsigned long long)k->page_faults,
                   threads);
    }
    buf_printf(b, "Fault counters: callret=%llu dirty=%llu smc=%llu mmio=%llu unaligned=%llu\n",
               (unsigned long long)__atomic_load_n(&g_nc_faults_callret, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&g_nc_faults_dirty, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&g_nc_faults_smc, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&g_nc_faults_mmio, __ATOMIC_RELAXED),
               (unsigned long long)__atomic_load_n(&g_nc_faults_unaligned, __ATOMIC_RELAXED));
    if (c && core_stopped && vm_ready(&c->vm)) {
        const uint32_t free_bytes = nc_vm_free_bytes(&c->vm);
        char runs[256] = "";
        nc_vm_free_runs(&c->vm, runs, sizeof(runs));
        buf_printf(b, "Guest RAM: total=%u MiB, free=%u KiB (%.1f MiB)",
                   c->vm.ram_size >> 20, free_bytes >> 10, (double)free_bytes / (1024.0 * 1024.0));
        if (runs[0]) {
            buf_printf(b, " | free runs: %s", runs);
        }
        buf_printf(b, "\n");
    }
    if (c && c->disc.image_size > 0) {
        buf_printf(b, "Disc image: %.1f MiB (%llu bytes)",
                   (double)c->disc.image_size / (1024.0 * 1024.0),
                   (unsigned long long)c->disc.image_size);
        if (c->disc.truncated) {
            buf_printf(b, " [TRUNCATED: needed >= %.1f MiB]",
                       (double)c->disc.truncated_need / (1024.0 * 1024.0));
        }
        buf_printf(b, "\n");
    }
    buf_printf(b, "\n");
}

static void append_code_bytes(ReportBuf *b, const NcVm *vm, uint32_t eip)
{
    uint8_t b0;
    if (!safe_rd8(vm, eip, &b0)) {
        buf_printf(b, "Code at EIP: <unmapped>\n");
        return;
    }
    buf_printf(b, "Code around EIP (0x%08X - 16 .. +31):\n", eip);
    const uint32_t start = eip >= 16 ? eip - 16 : 0;
    for (uint32_t row = 0; row < 3; row++) {
        const uint32_t base = start + row * 16;
        buf_printf(b, "  %08X:", base);
        for (uint32_t i = 0; i < 16; i++) {
            const uint32_t va = base + i;
            uint8_t byte = 0;
            const char mark = (va == eip) ? '>' : ' ';
            if (safe_rd8(vm, va, &byte)) {
                buf_printf(b, "%c%02X", mark, byte);
            } else {
                buf_printf(b, "%c??", mark);
            }
        }
        buf_printf(b, "\n");
    }
}

static void append_stack_info(ReportBuf *b, const NcVm *vm, const HleXbeInfo *x,
                              uint32_t ebp, uint32_t esp, uint32_t xbe_base, uint32_t xbe_size)
{
    /* EBP frame pointer chain (best effort; titles with FPO may stop early). */
    buf_printf(b, "EBP backtrace:\n");
    uint32_t fp = ebp;
    unsigned frames = 0;
    while (frames < 16 && fp >= 0x1000 && (fp & 3u) == 0) {
        uint32_t next_fp = 0, ret_va = 0;
        if (!safe_rd32(vm, fp, &next_fp) || !safe_rd32(vm, fp + 4, &ret_va)) {
            break;
        }
        char sym[64];
        format_va_symbol(x, ret_va, sym, sizeof(sym));
        buf_printf(b, "  #%02u ebp=%08X ret=%08X%s\n", frames, fp, ret_va, sym);
        frames++;
        if (next_fp <= fp || next_fp - fp > 0x100000u) {
            break;
        }
        fp = next_fp;
    }
    if (!frames) {
        buf_printf(b, "  (unavailable)\n");
    }

    /* Raw stack dwords at ESP. */
    uint32_t word = 0;
    if (safe_rd32(vm, esp, &word)) {
        buf_printf(b, "Stack at ESP (0x%08X):\n", esp);
        for (uint32_t row = 0; row < 8; row++) {
            const uint32_t row_va = esp + row * 16;
            if (!safe_rd32(vm, row_va, &word)) {
                break;
            }
            buf_printf(b, "  +%02X (%08X):", row * 16, row_va);
            for (uint32_t col = 0; col < 4; col++) {
                if (safe_rd32(vm, row_va + col * 4, &word)) {
                    buf_printf(b, " %08X", word);
                } else {
                    buf_printf(b, " ????????");
                }
            }
            buf_printf(b, "\n");
        }

        /* Return address candidates inside the title XBE image. */
        if (xbe_size > 0) {
            buf_printf(b, "Stack XBE code candidates:\n");
            unsigned found = 0;
            for (uint32_t w = 0; w < 512 && found < 24; w++) {
                if (!safe_rd32(vm, esp + 4 * w, &word)) {
                    break;
                }
                if (word >= xbe_base && word < xbe_base + xbe_size) {
                    const HleXbeSection *sec = hle_xbe_section_at(x, word);
                    if (!sec || (sec->flags & HLE_XBE_SEC_EXECUTABLE)) {
                        char sym[64];
                        format_va_symbol(x, word, sym, sizeof(sym));
                        buf_printf(b, "  [esp+0x%03X] %08X%s\n", w * 4, word, sym);
                        found++;
                    }
                }
            }
            if (!found) {
                buf_printf(b, "  (none)\n");
            }
        }
    } else {
        buf_printf(b, "Stack at ESP (0x%08X): <unmapped>\n", esp);
    }
}

static void append_cpu_state(ReportBuf *b, const NcCore *c, bool core_stopped)
{
    if (!c || !core_stopped) {
        return;
    }
    const NcKernel *k = &c->kernel;
    if (!k->stop_info.valid) {
        return;
    }
    const HleXbeInfo *x = &c->xbe.info;
    char sym[64];
    format_va_symbol(x, k->stop_info.eip, sym, sizeof(sym));

    buf_printf(b, "[Guest CPU & Fault State]\n");
    buf_printf(b, "Thread: t%u\n", k->stop_info.thread_id);
    if (k->stop_info.exception_code) {
        buf_printf(b, "Exception code: 0x%08X\n", k->stop_info.exception_code);
    }
    if (k->stop_info.has_fault) {
        buf_printf(b, "Fault: %s at VA 0x%08X (CR2=0x%08X)\n",
                   k->stop_info.fault_write ? "write" : "read",
                   k->stop_info.fault_va, k->cr2);
    }
    buf_printf(b, "EIP: 0x%08X%s\n", k->stop_info.eip, sym);
    buf_printf(b, "EFLAGS: 0x%08X  CS: 0x%04X  SS: 0x%04X\n",
               k->stop_info.eflags, k->stop_info.cs, k->stop_info.ss);
    buf_printf(b, "EAX: %08X  EBX: %08X  ECX: %08X  EDX: %08X\n",
               k->stop_info.gpr[NC_EAX], k->stop_info.gpr[NC_EBX],
               k->stop_info.gpr[NC_ECX], k->stop_info.gpr[NC_EDX]);
    buf_printf(b, "ESI: %08X  EDI: %08X  EBP: %08X  ESP: %08X\n",
               k->stop_info.gpr[NC_ESI], k->stop_info.gpr[NC_EDI],
               k->stop_info.gpr[NC_EBP], k->stop_info.gpr[NC_ESP]);

    if (vm_ready(&c->vm)) {
        append_code_bytes(b, &c->vm, k->stop_info.eip);
        append_stack_info(b, &c->vm, x, k->stop_info.gpr[NC_EBP], k->stop_info.gpr[NC_ESP],
                          k->xbe_base, k->xbe_size);
    }
    buf_printf(b, "\n");
}

static const char *region_name(uint32_t r)
{
    switch (r & 0x80000007u) {
    case 0x1:         return "NA";
    case 0x2:         return "Japan";
    case 0x3:         return "NA+Japan";
    case 0x4:         return "RestOfWorld";
    case 0x5:         return "NA+RoW";
    case 0x7:         return "RegionFree";
    case 0x80000000u: return "Manufacturing";
    default:          return "Other";
    }
}

static void append_xbe_info(ReportBuf *b, const NcCore *c)
{
    if (!c) {
        return;
    }
    const HleXbeInfo *x = &c->xbe.info;
    if (!x->title_id && !x->sizeof_image) {
        return;
    }
    char xmid[HLE_XBE_ID_MAX] = "", serial[HLE_XBE_ID_MAX] = "";
    hle_xbe_xmid(x, xmid);
    hle_xbe_serial(x, serial);
    buf_printf(b, "[XBE / Title]\n");
    buf_printf(b, "Title ID: %08X | XMID: %s | Serial: %s\n", x->title_id, xmid, serial);
    buf_printf(b, "Version: 0x%08X | Region: 0x%08X (%s) | Timedate: 0x%08X | Keys: %s\n",
               x->version, x->game_region, region_name(x->game_region),
               x->timedate, x->debug_keys ? "debug" : "retail");
    buf_printf(b, "Image path: %s\n",
               c->kernel.image_path[0] ? c->kernel.image_path : "\\Device\\CdRom0\\default.xbe");
    buf_printf(b, "Layout: base=0x%08X size=0x%X headers=0x%X entry=0x%08X thunk=0x%08X resolved_thunks=%u\n",
               x->base, x->sizeof_image, x->sizeof_headers, x->entry, x->kernel_thunk, c->xbe.thunks);
    if (x->nlibs > 0) {
        buf_printf(b, "Libraries (%u):\n", x->nlibs);
        for (unsigned i = 0; i < x->nlibs; i++) {
            const HleXbeLib *lib = &x->libs[i];
            buf_printf(b, "  %-8s %u.%u.%u (qfe %u)%s\n",
                       lib->name, lib->major, lib->minor, lib->build, lib->qfe,
                       lib->debug_build ? " [debug]" : "");
        }
    }
    if (x->nsections > 0) {
        buf_printf(b, "Sections (%u):\n", x->nsections);
        for (unsigned i = 0; i < x->nsections; i++) {
            const HleXbeSection *s = &x->sections[i];
            buf_printf(b, "  %-8s %08X-%08X (vsize=0x%X) [%c%c%c]\n",
                       s->name, s->va, s->va + s->vsize, s->vsize,
                       (s->flags & HLE_XBE_SEC_EXECUTABLE) ? 'X' : '-',
                       (s->flags & HLE_XBE_SEC_WRITABLE) ? 'W' : 'R',
                       (s->flags & HLE_XBE_SEC_PRELOAD) ? 'P' : '-');
        }
    }
    buf_printf(b, "\n");
}

static void emit_strip_tag(void *ctx, const char *line, size_t len)
{
    ReportBuf *b = (ReportBuf *)ctx;
    static const char prefix[] = "hakuX-native-frontier: ";
    const size_t plen = sizeof(prefix) - 1;
    if (len >= plen && !memcmp(line, prefix, plen)) {
        line += plen;
        len -= plen;
    }
    buf_append(b, line, len);
}

static void append_recent_and_threads(ReportBuf *b, const NcCore *c, bool core_stopped)
{
    buf_printf(b, "[Recent Kernel Calls]\n");
    const size_t before_calls = b->len;
    nc_kernel_dump_recent_cb(emit_strip_tag, b);
    if (b->len == before_calls) {
        buf_printf(b, "  (none)\n");
    }
    buf_printf(b, "\n");

    if (c && core_stopped && vm_ready(&c->vm) && c->kernel.vm == &c->vm) {
        buf_printf(b, "[Guest Threads]\n");
        const size_t before_threads = b->len;
        nc_kernel_dump_threads_cb(&c->kernel, emit_strip_tag, b);
        if (b->len == before_threads) {
            buf_printf(b, "  (none)\n");
        }
        buf_printf(b, "\n");
    }
}

static void append_device_and_gpu(ReportBuf *b)
{
    char dev[1024] = "";
    char gpu[2048] = "";
    const size_t dlen = nc_device_summary(dev, sizeof(dev));
    const size_t glen = nc_hle_gpu_summary(gpu, sizeof(gpu));
    if (!dlen && !glen) {
        return;
    }
    buf_printf(b, "[Native Host & GPU]\n");
    if (dlen) {
        buf_append(b, dev, dlen);
    }
    if (glen) {
        buf_append(b, gpu, glen);
    }
    buf_printf(b, "\n");
}

char *nc_crash_report_build(const NcCore *c, bool core_stopped, const char *fatal_why, int fatal_kind)
{
    ReportBuf b = {0};
    append_crash_summary(&b, c, core_stopped, fatal_why, fatal_kind);
    append_cpu_state(&b, c, core_stopped);
    append_xbe_info(&b, c);
    append_recent_and_threads(&b, c, core_stopped);
    append_device_and_gpu(&b);
    if (b.oom) {
        free(b.data);
        return NULL;
    }
    return b.data ? b.data : strdup("");
}
