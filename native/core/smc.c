/*
 * hakuX native core - self-modifying / overwritten code (SMC) tracking.
 */
#include "native/core/smc.h"

#include <pthread.h>
#include <stdlib.h>
#include <ucontext.h>

#define SMC_HIT_SLOTS     256u
#define SMC_HOT_THRESHOLD 16u

uint64_t g_nc_faults_smc;

static NcVm *g_vm;
static NcCpu *g_cpu;
static pthread_t g_vcpu_thread;
static bool g_enabled;

/* Per physical page: NC_SMC_* views invalidated by a non-vCPU thread and
 * awaiting nc_cpu_invalidate on the vCPU thread. */
static uint8_t *g_pending;
static uint32_t g_pending_any;
static uint32_t g_pending_full;

/* Per 4 KiB guest VA page (2^20 pages = 128 KiB): promoted to inline CRC32. */
static uint8_t g_hot_bits[1u << 17];

static struct {
    uint32_t page;
    uint16_t hits;
} g_hits[SMC_HIT_SLOTS];

static bool on_vcpu_thread(void)
{
    return pthread_equal(pthread_self(), g_vcpu_thread) != 0;
}

static uint8_t view_bit_for(const NcVm *vm, uint32_t va)
{
    if (va >= NC_CONTIG_WINDOW && va - NC_CONTIG_WINDOW < vm->ram_size) {
        return NC_SMC_CONTIG;
    }
    if (va >= NC_GPU_WINDOW && va - NC_GPU_WINDOW < vm->ram_size) {
        return NC_SMC_GPU;
    }
    return NC_SMC_USER;
}

static void hot_set(uint32_t va_page)
{
    const uint32_t idx = va_page >> 12;
    __atomic_fetch_or(&g_hot_bits[idx >> 3], (uint8_t)(1u << (idx & 7u)), __ATOMIC_RELEASE);
}

static void hot_clear(uint32_t va_page)
{
    const uint32_t idx = va_page >> 12;
    __atomic_fetch_and(&g_hot_bits[idx >> 3], (uint8_t)~(1u << (idx & 7u)), __ATOMIC_RELEASE);
}

bool nc_smc_is_hot_page(uint32_t va_page)
{
    if (!g_enabled) {
        return false;
    }
    const uint32_t idx = va_page >> 12;
    return (__atomic_load_n(&g_hot_bits[idx >> 3], __ATOMIC_RELAXED) & (1u << (idx & 7u))) != 0;
}

static void note_hit(uint32_t va_page)
{
    const uint32_t slot = ((va_page >> 12) * 2654435761u) >> 24;
    if (g_hits[slot].page == va_page) {
        if (g_hits[slot].hits < UINT16_MAX) {
            g_hits[slot].hits++;
        }
        if (g_hits[slot].hits == SMC_HOT_THRESHOLD && !nc_smc_is_hot_page(va_page)) {
            hot_set(va_page);
            nc_log("hakuX-native", "smc: hot code+data page %08x (%u faults) -> inline CRC checks",
                   va_page, g_hits[slot].hits);
        }
    } else {
        g_hits[slot].page = va_page;
        g_hits[slot].hits = 1;
    }
}

static void invalidate_views(uint32_t pa_page, uint8_t bits, uint32_t extra_va)
{
    if (!g_cpu || !g_vm) {
        return;
    }
    if (bits & NC_SMC_USER) {
        const uint32_t rva = g_vm->rmap[pa_page / NC_PAGE];
        if (rva) {
            nc_cpu_invalidate(g_cpu, rva, NC_PAGE);
        }
        if (extra_va != UINT32_MAX && extra_va < NC_CONTIG_WINDOW && extra_va != rva) {
            nc_cpu_invalidate(g_cpu, extra_va, NC_PAGE);
        }
    }
    if (bits & NC_SMC_CONTIG) {
        nc_cpu_invalidate(g_cpu, NC_CONTIG_WINDOW + pa_page, NC_PAGE);
    }
    if (bits & NC_SMC_GPU) {
        nc_cpu_invalidate(g_cpu, NC_GPU_WINDOW + pa_page, NC_PAGE);
    }
}

static void queue_pending(uint32_t page, uint8_t bits)
{
    if (!g_pending || !g_cpu || !bits) {
        return;
    }
    __atomic_fetch_or(&g_pending[page], bits, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pending_any, 1u, __ATOMIC_RELEASE);
    nc_cpu_request_interrupt(g_cpu);
}

void nc_smc_init(NcVm *vm, NcCpu *cpu)
{
    free(g_pending);
    g_pending = NULL;
    g_vm = vm;
    g_cpu = cpu;
    g_vcpu_thread = pthread_self();
    g_enabled = vm && cpu && nc_cpu_smc_enabled();
    __atomic_store_n(&g_pending_any, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pending_full, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_nc_faults_smc, 0u, __ATOMIC_RELAXED);
    memset(g_hot_bits, 0, sizeof(g_hot_bits));
    memset(g_hits, 0, sizeof(g_hits));
    if (g_enabled) {
        g_pending = calloc(vm->ram_size / NC_PAGE, 1);
    }
}

void nc_smc_mark(uint32_t va, uint32_t len)
{
    if (!g_enabled || !g_vm || !len) {
        return;
    }
    const uint64_t end = (uint64_t)va + len > (1ull << 32) ? (1ull << 32) : (uint64_t)va + len;
    for (uint64_t a = va & ~(uint64_t)(NC_PAGE - 1); a < end; a += NC_PAGE) {
        const uint32_t pva = (uint32_t)a;
        if (nc_smc_is_hot_page(pva)) {
            continue;
        }
        const uint32_t pa = nc_vm_phys(g_vm, pva);
        if (pa == UINT32_MAX || pa >= g_vm->ram_size) {
            continue;
        }
        nc_vm_smc_protect(g_vm, pa & ~(NC_PAGE - 1), view_bit_for(g_vm, pva));
    }
}

bool nc_smc_fault(const siginfo_t *info, void *ucontext, bool *want_dirty)
{
    if (want_dirty) {
        *want_dirty = false;
    }
    if (!g_enabled || !g_vm || !g_cpu) {
        return false;
    }
    const uint64_t gva = nc_vm_guest_of(g_vm, info->si_addr);
    if (gva == UINT64_MAX) {
        return false;
    }
    const uint32_t pa = nc_vm_phys(g_vm, (uint32_t)gva);
    if (pa == UINT32_MAX || pa >= g_vm->ram_size) {
        return false;
    }
    /* Let HLE noaccess (PROT_NONE) restore the page first. */
    if (nc_vm_noaccess_has(g_vm, pa)) {
        return false;
    }
    if (!nc_cpu_fault_is_write(ucontext)) {
        return false;
    }
    const uint32_t page = pa / NC_PAGE;
    const uint8_t bits = __atomic_exchange_n(&g_vm->smc[page], 0, __ATOMIC_ACQ_REL);
    if (!bits) {
        return false;
    }
    __atomic_fetch_add(&g_nc_faults_smc, 1, __ATOMIC_RELAXED);
    const uint32_t va_page = (uint32_t)gva & ~(NC_PAGE - 1);
    const uint32_t pa_page = page * NC_PAGE;
    const bool vcpu = on_vcpu_thread();
    if (vcpu) {
        note_hit(va_page);
        invalidate_views(pa_page, bits, va_page);
    } else {
        queue_pending(page, bits);
    }
    const uint8_t wp = __atomic_load_n(&g_vm->wp[page], __ATOMIC_ACQUIRE);
    if (wp == NC_WP_CLEAN && want_dirty) {
        *want_dirty = true;
    } else {
        nc_vm_smc_sync(g_vm, pa_page);
    }
    if (vcpu) {
        nc_cpu_smc_self_mod_step(g_cpu, ucontext, (uint32_t)gva, 16u);
    }
    return true;
}

void nc_smc_will_write_va(uint32_t va, uint32_t len)
{
    if (!g_enabled || !g_vm || !len) {
        return;
    }
    const uint64_t end = (uint64_t)va + len > (1ull << 32) ? (1ull << 32) : (uint64_t)va + len;
    const bool vcpu = on_vcpu_thread();
    for (uint64_t a = va & ~(uint64_t)(NC_PAGE - 1); a < end; a += NC_PAGE) {
        const uint32_t pva = (uint32_t)a;
        const uint32_t pa = nc_vm_phys(g_vm, pva);
        if (pa == UINT32_MAX || pa >= g_vm->ram_size) {
            continue;
        }
        const uint32_t page = pa / NC_PAGE;
        if (!__atomic_load_n(&g_vm->smc[page], __ATOMIC_RELAXED)) {
            continue;
        }
        const uint8_t bits = __atomic_exchange_n(&g_vm->smc[page], 0, __ATOMIC_ACQ_REL);
        if (!bits) {
            continue;
        }
        const uint32_t pa_page = page * NC_PAGE;
        if (vcpu) {
            invalidate_views(pa_page, bits, pva);
        } else {
            queue_pending(page, bits);
        }
        nc_vm_smc_sync(g_vm, pa_page);
    }
}

void nc_smc_host_wrote_pa(uint32_t pa, uint32_t len)
{
    if (!g_enabled || !g_vm || !len) {
        return;
    }
    const uint64_t end = (uint64_t)pa + len > g_vm->ram_size ? g_vm->ram_size : (uint64_t)pa + len;
    const bool vcpu = on_vcpu_thread();
    for (uint64_t a = pa & ~(uint64_t)(NC_PAGE - 1); a < end; a += NC_PAGE) {
        const uint32_t page = (uint32_t)(a / NC_PAGE);
        if (!__atomic_load_n(&g_vm->smc[page], __ATOMIC_RELAXED)) {
            continue;
        }
        const uint8_t bits = __atomic_exchange_n(&g_vm->smc[page], 0, __ATOMIC_ACQ_REL);
        if (!bits) {
            continue;
        }
        const uint32_t pa_page = page * NC_PAGE;
        if (vcpu) {
            invalidate_views(pa_page, bits, UINT32_MAX);
        } else {
            queue_pending(page, bits);
        }
        nc_vm_smc_sync(g_vm, pa_page);
    }
}

void nc_smc_invalidate_va(uint32_t va, uint32_t len)
{
    if (!g_enabled || !g_vm || !g_cpu || !len) {
        return;
    }
    const uint64_t end = (uint64_t)va + len > (1ull << 32) ? (1ull << 32) : (uint64_t)va + len;
    for (uint64_t a = va & ~(uint64_t)(NC_PAGE - 1); a < end; a += NC_PAGE) {
        const uint32_t pva = (uint32_t)a;
        const bool was_hot = nc_smc_is_hot_page(pva);
        if (was_hot) {
            hot_clear(pva);
        }
        const uint32_t pa = nc_vm_phys(g_vm, pva);
        uint8_t bits = 0;
        if (pa != UINT32_MAX && pa < g_vm->ram_size) {
            bits = __atomic_exchange_n(&g_vm->smc[pa / NC_PAGE], 0, __ATOMIC_ACQ_REL);
            if (bits) {
                nc_vm_smc_sync(g_vm, pa & ~(NC_PAGE - 1));
            }
        }
        if (bits || was_hot) {
            if (bits) {
                invalidate_views(pa & ~(NC_PAGE - 1), bits, pva);
            } else {
                nc_cpu_invalidate(g_cpu, pva, NC_PAGE);
            }
        }
    }
}

void nc_smc_flush_pending(void)
{
    if (!g_enabled || !g_vm || !g_cpu) {
        return;
    }
    if (__atomic_load_n(&g_pending_full, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&g_pending_full, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pending_any, 0u, __ATOMIC_RELEASE);
        if (g_pending) {
            memset(g_pending, 0, g_vm->ram_size / NC_PAGE);
        }
        nc_cpu_invalidate(g_cpu, 0, 0xFFFFF000u);
        return;
    }
    if (!g_pending || !__atomic_load_n(&g_pending_any, __ATOMIC_ACQUIRE)) {
        return;
    }
    __atomic_store_n(&g_pending_any, 0u, __ATOMIC_RELEASE);
    const uint32_t pages = g_vm->ram_size / NC_PAGE;
    for (uint32_t p = 0; p < pages; p++) {
        if (!__atomic_load_n(&g_pending[p], __ATOMIC_RELAXED)) {
            continue;
        }
        const uint8_t bits = __atomic_exchange_n(&g_pending[p], 0, __ATOMIC_ACQ_REL);
        if (bits) {
            invalidate_views(p * NC_PAGE, bits, UINT32_MAX);
        }
    }
}

void nc_smc_reset_all(void)
{
    if (!g_enabled || !g_vm || !g_cpu) {
        return;
    }
    const uint32_t pages = g_vm->ram_size / NC_PAGE;
    for (uint32_t p = 0; p < pages; p++) {
        if (__atomic_exchange_n(&g_vm->smc[p], 0, __ATOMIC_ACQ_REL) != 0) {
            nc_vm_smc_sync(g_vm, p * NC_PAGE);
        }
    }
    if (g_pending) {
        memset(g_pending, 0, pages);
    }
    __atomic_store_n(&g_pending_any, 0u, __ATOMIC_RELEASE);
    memset(g_hot_bits, 0, sizeof(g_hot_bits));
    memset(g_hits, 0, sizeof(g_hits));
    if (on_vcpu_thread()) {
        __atomic_store_n(&g_pending_full, 0u, __ATOMIC_RELEASE);
        nc_cpu_invalidate(g_cpu, 0, 0xFFFFF000u);
    } else {
        __atomic_store_n(&g_pending_full, 1u, __ATOMIC_RELEASE);
        nc_cpu_request_interrupt(g_cpu);
    }
}
