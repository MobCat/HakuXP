/*
 * hakuX native core - self-modifying / overwritten code (SMC) tracking.
 *
 * FEX compiles guest blocks and notifies us of newly executed guest pages
 * through MarkGuestExecutableRange -> nc_smc_mark. Those pages are
 * write-protected via NcVm (unified with HLE dirty tracking and noaccess).
 * Writes from guest JIT code, host syscalls, or cross-thread device DMA
 * invalidate the affected JIT translations and restore write access.
 *
 * Pages that mix hot code and frequently written data (such as Xbox [XWP]
 * section boundary pages or busy heap pages) promote after a threshold to
 * per-page inline CRC32 validation so they never incur a fault storm.
 */
#ifndef NATIVE_CORE_SMC_H
#define NATIVE_CORE_SMC_H

#include "native/core/nc.h"
#include "native/core/vm.h"
#include "native/cpu/cpu.h"

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t g_nc_faults_smc;

/* Bind SMC tracking to vm and cpu; must be called on the vCPU thread. */
void nc_smc_init(NcVm *vm, NcCpu *cpu);

/* Called from FEX MarkGuestExecutableRange on the vCPU thread. */
void nc_smc_mark(uint32_t va, uint32_t len);

/* Called from FEX at translation time: true for hot mixed code+data pages. */
bool nc_smc_is_hot_page(uint32_t va_page);

/* Handle a SIGSEGV write fault on an SMC-protected page. Returns true if the
 * fault was an SMC write; sets *want_dirty if dirty tracking also held the
 * page clean (NC_WP_CLEAN) and still needs to run its first-write handler. */
bool nc_smc_fault(const siginfo_t *info, void *ucontext, bool *want_dirty);

/* Called before a host syscall (read/pread) writes into vm->base + [va, va+len). */
void nc_smc_will_write_va(uint32_t va, uint32_t len);

/* Called after any host thread or device DMA writes physical RAM [pa, pa+len). */
void nc_smc_host_wrote_pa(uint32_t pa, uint32_t len);

/* Called on the vCPU thread when guest memory is unmapped or freed. */
void nc_smc_invalidate_va(uint32_t va, uint32_t len);

/* Drain deferred cross-thread SMC invalidations on the vCPU thread. */
void nc_smc_flush_pending(void);

/* Drop all SMC state and schedule/run a full JIT invalidation (savestate load). */
void nc_smc_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif
