/*
 * hakuX native core -- kernel calls that run guest routines and go on.
 *
 * Some kernel exports have to run guest code in the middle of their work:
 * IofCompleteRequest calls each stack location's completion routine,
 * IoStartPacket a driver's StartIo, IoSynchronousFsdRequest a driver's
 * dispatch routine and then a wait. Such a call stops at that point: the
 * routine runs on the caller's stack and returns into a stub
 * (NC_KORDINAL_KCONT), and the continuation a record above the routine's
 * arguments names goes on with the routine's EAX, as if still inside the
 * original call (its ESP, the bytes it pops).
 *
 * Everything a continuation needs is in that record, in guest memory: a
 * savestate taken while the routine runs resumes it; the nesting has no
 * limit of its own (a completion routine that starts the next packet,
 * whose StartIo completes another request, and so on); and an exception
 * that unwinds past the routine only leaves the record behind. The
 * per-thread callouts (nc_kernel_callout) stay for single routines.
 *
 * Record (REC_BYTES, dword aligned):
 *   +0x00 KCONT_MAGIC
 *   +0x04 the record's own address (told apart from data that matches)
 *   +0x08 the continuation (enum NcKContId: numbers kept in savestates)
 *   +0x0C the kernel call's ESP (its return address there)
 *   +0x10 the bytes the kernel call pops, its return address included
 *   +0x14 the count of context words
 *   +0x18 the context words (NC_KCONT_CTX)
 */
#include "native/kernel/kernel.h"

#define KCONT_MAGIC 0x544E434Bu /* "KCNT" */
#define REC_SELF    0x04u
#define REC_ID      0x08u
#define REC_ESP     0x0Cu
#define REC_POP     0x10u
#define REC_NCTX    0x14u
#define REC_CTX     0x18u
#define REC_BYTES   (REC_CTX + 4u * NC_KCONT_CTX)
/* Below the kernel call's ESP, as nc_kernel_callout leaves it. */
#define REC_GAP     16u
/* A cdecl routine leaves its arguments: the record is this many dwords up. */
#define REC_SEARCH  12u

static NcKContFn g_cont[NC_KCONT_MAX];

void nc_kcont_register(enum NcKContId id, NcKContFn fn)
{
    if ((unsigned)id < NC_KCONT_MAX) {
        g_cont[id] = fn;
    }
}

void nc_kernel_run_guest(NcKernel *k, NcFrame *f, uint32_t routine, const uint32_t *args, unsigned nargs,
                         enum NcKContId cont, const uint32_t *ctx, unsigned nctx)
{
    if (nctx > NC_KCONT_CTX) {
        nc_kernel_request_stop(k, f, "kernel continuation %u: %u context words", (unsigned)cont, nctx);
        return;
    }
    const uint32_t esp = nc_frame_gpr(f, NC_ESP);
    const uint32_t rec = (esp - REC_GAP - REC_BYTES) & ~3u;
    nc_vm_wr32(k->vm, rec, KCONT_MAGIC);
    nc_vm_wr32(k->vm, rec + REC_SELF, rec);
    nc_vm_wr32(k->vm, rec + REC_ID, (uint32_t)cont);
    nc_vm_wr32(k->vm, rec + REC_ESP, esp);
    nc_vm_wr32(k->vm, rec + REC_POP, k->call_pop);
    nc_vm_wr32(k->vm, rec + REC_NCTX, nctx);
    for (unsigned i = 0; i < NC_KCONT_CTX; i++) {
        nc_vm_wr32(k->vm, rec + REC_CTX + 4 * i, i < nctx ? ctx[i] : 0);
    }
    const uint32_t sp = rec - 4 - 4 * nargs;
    nc_vm_wr32(k->vm, sp, nc_kernel_stub_va(NC_KORDINAL_KCONT));
    for (unsigned i = 0; i < nargs; i++) {
        nc_vm_wr32(k->vm, sp + 4 + 4 * i, args[i]);
    }
    nc_frame_set_gpr(f, NC_ESP, sp);
    nc_frame_set_eip(f, routine);
    k->redirected = true;
}

/* The record the routine that returned to the stub left at or above esp, or 0. */
static uint32_t find_record(NcKernel *k, uint32_t esp)
{
    for (unsigned i = 0; i <= REC_SEARCH; i++) {
        const uint32_t p = esp + 4 * i;
        if (nc_vm_phys(k->vm, p) == UINT32_MAX || nc_vm_phys(k->vm, p + REC_BYTES - 4) == UINT32_MAX) {
            return 0;
        }
        if (nc_vm_rd32(k->vm, p) == KCONT_MAGIC && nc_vm_rd32(k->vm, p + REC_SELF) == p) {
            return p;
        }
    }
    return 0;
}

void nc_kernel_stub_finish(NcKernel *k, NcFrame *f, uint32_t esp, uint32_t pop, uint64_t r)
{
    const NcThread *t = k->current;
    if (k->redirected || (t && t->state == NC_T_TERMINATED)) {
        return; /* it runs guest code first, or the thread ended */
    }
    nc_frame_set_gpr(f, NC_EAX, (uint32_t)r);
    nc_frame_set_gpr(f, NC_EDX, (uint32_t)(r >> 32));
    nc_frame_set_eip(f, nc_vm_rd32(k->vm, esp));
    nc_frame_set_gpr(f, NC_ESP, esp + pop);
}

void nc_kcont_resume(NcKernel *k, NcFrame *f)
{
    const uint32_t rec = find_record(k, nc_frame_gpr(f, NC_ESP));
    if (!rec) {
        nc_kernel_request_stop(k, f, "kernel continuation: no record at %08x (a routine popped too much?)",
                               nc_frame_gpr(f, NC_ESP));
        return;
    }
    const uint32_t id = nc_vm_rd32(k->vm, rec + REC_ID);
    const uint32_t esp = nc_vm_rd32(k->vm, rec + REC_ESP);
    const uint32_t pop = nc_vm_rd32(k->vm, rec + REC_POP);
    uint32_t nctx = nc_vm_rd32(k->vm, rec + REC_NCTX);
    uint32_t ctx[NC_KCONT_CTX];
    nctx = nctx < NC_KCONT_CTX ? nctx : NC_KCONT_CTX;
    for (unsigned i = 0; i < NC_KCONT_CTX; i++) {
        ctx[i] = nc_vm_rd32(k->vm, rec + REC_CTX + 4 * i);
    }
    nc_vm_wr32(k->vm, rec, 0); /* used: a stale copy never matches again */
    if (id >= NC_KCONT_MAX || !g_cont[id]) {
        nc_kernel_request_stop(k, f, "kernel continuation %u unknown (record at %08x)", id, rec);
        return;
    }
    const uint32_t eax = nc_frame_gpr(f, NC_EAX);
    nc_frame_set_gpr(f, NC_ESP, esp);
    k->frame = f;
    k->call_pop = pop;
    k->redirected = false;
    k->in_call = true;
    const uint64_t r = g_cont[id](k, f, eax, ctx, nctx);
    k->in_call = false;
    nc_kernel_stub_finish(k, f, esp, pop, r);
}

void nc_kernel_stub_call(NcKernel *k, NcFrame *f, NcKFn fn, unsigned nargs)
{
    const uint32_t esp = nc_frame_gpr(f, NC_ESP);
    uint32_t args[8] = {0};
    nargs = nargs < 8 ? nargs : 8;
    for (unsigned i = 0; i < nargs; i++) {
        args[i] = nc_vm_rd32(k->vm, esp + 4 + 4 * i);
    }
    k->frame = f;
    k->call_pop = 4 + 4 * nargs;
    k->redirected = false;
    k->in_call = true;
    const uint64_t r = fn(k, f, args);
    k->in_call = false;
    nc_kernel_stub_finish(k, f, esp, 4 + 4 * nargs, r);
}

/* NC_KCONT_RETURN: the routine's EAX is the call's result. */
static uint64_t cont_return(NcKernel *k, NcFrame *f, uint32_t eax, const uint32_t *ctx, unsigned nctx)
{
    return eax;
}

void nc_kcont_init(void)
{
    nc_kcont_register(NC_KCONT_RETURN, cont_return);
}
