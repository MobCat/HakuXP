/*
 * hakuX native core -- I/O request packets: the driver model's half of the
 * I/O manager.
 *
 * A request is an IRP in the kernel's pool (IoAllocateIrp, IoBuild*Request)
 * or in a driver's own memory (IoInitializeIrp), with one stack location per
 * driver it passes through (iomgr.h has the layouts). IofCallDriver moves it
 * to the next location and calls that driver's dispatch routine: a title's
 * routine as a tail call (it returns straight to IofCallDriver's caller), a
 * kernel driver's in C (iodrv.c). IofCompleteRequest walks back up the
 * locations calling the completion routines (guest code, one at a time with
 * a continuation, kcont.c) until one asks for more processing or none is
 * left, then tells the requester: its IoStatusBlock, its event (or the file
 * object's), its APC or its file's completion port, and frees a packet the
 * kernel built. NT queues that last part as a kernel APC to the requesting
 * thread; here it runs at once, where the request completes (the memory it
 * writes is the same: the Xbox has one address space).
 *
 * Device queues (IoStartPacket, IoStartNextPacket) are KDEVICE_QUEUEs
 * (kqueue.c); StartIo runs at DISPATCH_LEVEL as on the console. The
 * synchronous requests (IoSynchronousFsdRequest,
 * IoSynchronousDeviceIoControlRequest) build an IRP with a kernel event,
 * call the driver and wait for that event if the request pends.
 *
 * XAPI's memory unit driver (xapilib.lib mu.obj, disk.obj) is the title
 * code this serves: IoCreateDevice, IoStartPacket from its dispatch,
 * IoMarkIrpMustComplete for writes, IofCompleteRequest and IoStartNextPacket
 * from its USB completions; XMountMU and XReadMUMetaData call it through
 * the synchronous requests (mountmu.obj).
 */
#include "native/kernel/iomgr.h"

#define ET_IRP_LIST   0x134u      /* ETHREAD.IrpList */
#define IRP_HX_POOL   0x40000000u /* HakuXP's mark: the kernel's pool holds it, freed at its completion */
#define PCR_IRQL      0x24u
#define DISPATCH_LEVEL 2u

/* the synchronous requests' block: a KEVENT, an IO_STATUS_BLOCK, where the
 * output length goes */
#define SB_EVENT  0x00u
#define SB_IOSB   0x10u
#define SB_RETLEN 0x18u
#define SB_BYTES  0x20u

static uint8_t rd8(NcKernel *k, uint32_t va)
{
    return *(const uint8_t *)nc_vm_host(k->vm, va);
}

static void wr8(NcKernel *k, uint32_t va, uint8_t v)
{
    *(uint8_t *)nc_vm_host(k->vm, va) = v;
}

static bool nt_success(uint32_t st)
{
    return (int32_t)st >= 0;
}

static uint8_t *irql(NcKernel *k)
{
    return (uint8_t *)nc_vm_host(k->vm, k->kpcr_va + PCR_IRQL);
}

/* KeLowerIrql's part that matters here: DPCs queued meanwhile run once below
 * DISPATCH_LEVEL. */
static void lower_irql(NcKernel *k, uint8_t to)
{
    *irql(k) = to;
    if (to < DISPATCH_LEVEL && nc_dpc_pending()) {
        k->resched = true;
    }
}

/* ------------------------------------------------------------ packets */

static void irp_init(NcKernel *k, uint32_t irp, uint32_t bytes, unsigned stack)
{
    memset(nc_vm_host(k->vm, irp), 0, bytes);
    nc_wr16(nc_vm_host(k->vm, irp + IRP_TYPE), IO_TYPE_IRP);
    nc_wr16(nc_vm_host(k->vm, irp + IRP_SIZE), (uint16_t)bytes);
    wr8(k, irp + IRP_STACK_COUNT, (uint8_t)stack);
    wr8(k, irp + IRP_CURRENT_LOCATION, (uint8_t)(stack + 1));
    nc_list_init(k, irp + IRP_THREAD_LIST);
    nc_vm_wr32(k->vm, irp + IRP_CURRENT_STACK, irp + IRP_BYTES + stack * STACK_BYTES);
}

uint32_t nc_irp_alloc(NcKernel *k, unsigned stack)
{
    stack = stack ? stack : 1;
    const uint32_t bytes = IRP_BYTES + stack * STACK_BYTES;
    const uint32_t irp = nc_pool_alloc(k, bytes);
    if (!irp) {
        nc_log("hakuX-native-io", "IRP of %u stack locations: no pool left", stack);
        return 0;
    }
    irp_init(k, irp, bytes, stack);
    nc_vm_wr32(k->vm, irp + IRP_FLAGS, IRP_HX_POOL);
    return irp;
}

void nc_irp_free(NcKernel *k, uint32_t irp)
{
    if (irp && (nc_vm_rd32(k->vm, irp + IRP_FLAGS) & IRP_HX_POOL)) {
        nc_list_remove(k, irp + IRP_THREAD_LIST); /* off its thread's list, if a driver freed it there */
        nc_vm_wr32(k->vm, irp + IRP_FLAGS, 0);  /* a second free finds no mark */
        nc_pool_free(k, irp, nc_rd16(nc_vm_host(k->vm, irp + IRP_SIZE)));
    }
}

static uint32_t next_location(NcKernel *k, uint32_t irp)
{
    return nc_vm_rd32(k->vm, irp + IRP_CURRENT_STACK) - STACK_BYTES;
}

/* On the requesting thread's IRP list (NT's IopQueueThreadIrp). */
static void thread_queue(NcKernel *k, uint32_t irp)
{
    const uint32_t thread = nc_vm_rd32(k->vm, irp + IRP_THREAD);
    if (thread) {
        nc_list_insert_head(k, thread + ET_IRP_LIST, irp + IRP_THREAD_LIST);
    }
}

/* IoBuildAsynchronousFsdRequest's packet: a read or write of len bytes at the
 * offset, or another request with no parameters. */
static uint32_t build_fsd(NcKernel *k, uint32_t major, uint32_t dev, uint32_t buf, uint32_t len, uint32_t offset,
                          uint32_t iosb)
{
    const uint32_t irp = nc_irp_alloc(k, dev ? rd8(k, dev + DEV_STACK_SIZE) : 1);
    if (!irp) {
        return 0;
    }
    nc_vm_wr32(k->vm, irp + IRP_THREAD, k->current ? k->current->ethread : 0);
    const uint32_t sl = next_location(k, irp);
    wr8(k, sl + SL_MAJOR, (uint8_t)major);
    if (major == IRP_MJ_READ || major == IRP_MJ_WRITE) {
        nc_vm_wr32(k->vm, irp + IRP_USER_BUFFER, buf);
        nc_vm_wr32(k->vm, sl + SL_READ_LENGTH, len);
        if (offset) {
            nc_vm_wr32(k->vm, sl + SL_READ_OFFSET, nc_vm_rd32(k->vm, offset));
            nc_vm_wr32(k->vm, sl + SL_READ_OFFSET + 4, nc_vm_rd32(k->vm, offset + 4));
        }
    }
    nc_vm_wr32(k->vm, irp + IRP_USER_IOSB, iosb);
    return irp;
}

static uint32_t build_ioctl(NcKernel *k, uint32_t code, uint32_t dev, uint32_t in, uint32_t in_len, uint32_t out,
                            uint32_t out_len, bool internal, uint32_t event, uint32_t iosb)
{
    const uint32_t irp = nc_irp_alloc(k, dev ? rd8(k, dev + DEV_STACK_SIZE) : 1);
    if (!irp) {
        return 0;
    }
    nc_vm_wr32(k->vm, irp + IRP_THREAD, k->current ? k->current->ethread : 0);
    const uint32_t sl = next_location(k, irp);
    wr8(k, sl + SL_MAJOR, internal ? IRP_MJ_INTERNAL_DEVICE_CONTROL : IRP_MJ_DEVICE_CONTROL);
    nc_vm_wr32(k->vm, sl + SL_IOCTL_OUT_LEN, out_len);
    nc_vm_wr32(k->vm, sl + SL_IOCTL_IN_BUF, in);
    nc_vm_wr32(k->vm, sl + SL_IOCTL_IN_LEN, in_len);
    nc_vm_wr32(k->vm, sl + SL_IOCTL_CODE, code);
    nc_vm_wr32(k->vm, irp + IRP_USER_BUFFER, out);
    nc_vm_wr32(k->vm, irp + IRP_USER_IOSB, iosb);
    nc_vm_wr32(k->vm, irp + IRP_USER_EVENT, event);
    thread_queue(k, irp);
    return irp;
}

/* IoAllocateIrp(StackSize) */
static uint64_t IoAllocateIrp(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_irp_alloc(k, a[0] & 0xFF);
}

/* IoInitializeIrp(Irp, PacketSize, StackSize): a packet in the caller's memory */
static uint64_t IoInitializeIrp(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    irp_init(k, a[0], a[1] & 0xFFFF, a[2] & 0xFF);
    return 0;
}

/* IoFreeIrp(Irp) */
static uint64_t IoFreeIrp(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_irp_free(k, a[0]);
    return 0;
}

/* IoBuildAsynchronousFsdRequest(MajorFunction, DeviceObject, Buffer, Length, StartingOffset,
 *   IoStatusBlock) */
static uint64_t IoBuildAsynchronousFsdRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return build_fsd(k, a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* IoBuildSynchronousFsdRequest(MajorFunction, DeviceObject, Buffer, Length, StartingOffset,
 *   Event, IoStatusBlock) */
static uint64_t IoBuildSynchronousFsdRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t irp = build_fsd(k, a[0], a[1], a[2], a[3], a[4], a[6]);
    if (irp) {
        nc_vm_wr32(k->vm, irp + IRP_USER_EVENT, a[5]);
        thread_queue(k, irp);
    }
    return irp;
}

/* IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputBufferLength,
 *   OutputBuffer, OutputBufferLength, InternalDeviceIoControl, Event, IoStatusBlock) */
static uint64_t IoBuildDeviceIoControlRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return build_ioctl(k, a[0], a[1], a[2], a[3], a[4], a[5], a[6] & 0xFF, a[7], a[8]);
}

/* IoQueueThreadIrp(Irp) */
static uint64_t IoQueueThreadIrp(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    thread_queue(k, a[0]);
    return 0;
}

/* IoMarkIrpMustComplete(Irp): XAPI's CancelIo leaves the request alone (the
 * memory unit driver marks its writes). No shutdown waits for it here. */
static uint64_t IoMarkIrpMustComplete(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_vm_wr32(k->vm, a[0] + IRP_FLAGS, nc_vm_rd32(k->vm, a[0] + IRP_FLAGS) | IRP_MUST_COMPLETE);
    return 0;
}

/* --------------------------------------------------------- completion */

/* As NT's ZeroIrpStackLocation: MajorFunction, DeviceObject and the
 * completion routine stay. */
static void clear_location(NcKernel *k, uint32_t sl)
{
    uint8_t *p = nc_vm_host(k->vm, sl);
    p[SL_MINOR] = p[SL_FLAGS] = p[SL_CONTROL] = 0;
    memset(p + SL_PARAM, 0, 16);
    nc_vm_wr32(k->vm, sl + SL_FILE, 0);
}

/* The requester's part, once no location is left: its IoStatusBlock, its
 * event, its APC or its file's completion port; the file object's reference
 * goes, and a packet the kernel built. A request that failed without having
 * pended tells a file's requester nothing (NT's rule; the system service
 * returned the error). */
static void finish(NcKernel *k, uint32_t irp)
{
    const uint32_t st = nc_vm_rd32(k->vm, irp + IRP_STATUS), info = nc_vm_rd32(k->vm, irp + IRP_INFORMATION);
    const uint32_t iosb = nc_vm_rd32(k->vm, irp + IRP_USER_IOSB), event = nc_vm_rd32(k->vm, irp + IRP_USER_EVENT);
    const uint32_t file = nc_vm_rd32(k->vm, irp + IRP_ORIGINAL_FILE), thread = nc_vm_rd32(k->vm, irp + IRP_THREAD);
    const uint32_t apc = nc_vm_rd32(k->vm, irp + IRP_APC_ROUTINE), apc_ctx = nc_vm_rd32(k->vm, irp + IRP_APC_CONTEXT);
    const bool tell = st < 0xC0000000u || rd8(k, irp + IRP_PENDING_RETURNED) || !file; /* !NT_ERROR */
    nc_list_remove(k, irp + IRP_THREAD_LIST);
    if (tell && iosb && nc_vm_phys(k->vm, iosb) != UINT32_MAX) {
        nc_vm_wr32(k->vm, iosb, st);
        nc_vm_wr32(k->vm, iosb + 4, info);
    }
    if (tell && event) {
        nc_ke_set_event(k, event);
    } else if (tell && file) {
        nc_ke_set_event(k, file + NC_FO_EVENT);
    }
    if (tell && apc && thread) {
        NcThread *t = nc_ps_thread_of(k, thread);
        if (t && t->state != NC_T_FREE && t->state != NC_T_TERMINATED) {
            const NcApc user = {.normal = apc, .ctx = apc_ctx, .a1 = iosb, .a2 = 0};
            nc_apc_queue(k, t, &user);
        }
    } else if (tell && file && apc_ctx) {
        uint32_t key = 0;
        const uint32_t port = nc_io_file_port(k, file, &key);
        if (port) {
            nc_kqueue_post(k, port, key, apc_ctx, st, info);
        }
    }
    if (file) {
        nc_ob_dereference(k, file);
    }
    if (nc_io_trace()) {
        nc_log("hakuX-native-io", "IRP %08x done: %08x, %u bytes%s%s", irp, st, info, event ? ", event set" : "",
               apc ? ", APC queued" : "");
    }
    nc_irp_free(k, irp);
}

/* Up the stack locations from the current one; true once none is left,
 * false while a completion routine runs (NC_KCONT_IRP_COMPLETE goes on). */
static bool complete_up(NcKernel *k, NcFrame *f, uint32_t irp, uint32_t ret)
{
    for (;;) {
        const int8_t loc = (int8_t)rd8(k, irp + IRP_CURRENT_LOCATION);
        const int8_t count = (int8_t)rd8(k, irp + IRP_STACK_COUNT);
        if (loc > count) {
            return true;
        }
        const uint32_t sl = nc_vm_rd32(k->vm, irp + IRP_CURRENT_STACK);
        wr8(k, irp + IRP_CURRENT_LOCATION, (uint8_t)(loc + 1));
        nc_vm_wr32(k->vm, irp + IRP_CURRENT_STACK, sl + STACK_BYTES);
        const uint8_t control = rd8(k, sl + SL_CONTROL);
        const bool pending = control & SL_PENDING_RETURNED;
        wr8(k, irp + IRP_PENDING_RETURNED, pending);
        const uint32_t st = nc_vm_rd32(k->vm, irp + IRP_STATUS);
        const uint32_t routine = nc_vm_rd32(k->vm, sl + SL_COMPLETION), context = nc_vm_rd32(k->vm, sl + SL_CONTEXT);
        const bool invoke = routine && ((nt_success(st) && (control & SL_INVOKE_ON_SUCCESS)) ||
                                        (!nt_success(st) && (control & SL_INVOKE_ON_ERROR)) ||
                                        (rd8(k, irp + IRP_CANCEL) && (control & SL_INVOKE_ON_CANCEL)));
        clear_location(k, sl);
        if (invoke) {
            /* the device of the driver that set the routine: the location now current's */
            const uint32_t dev = loc >= count ? 0 : nc_vm_rd32(k->vm, sl + STACK_BYTES + SL_DEVICE);
            const uint32_t args[3] = {dev, irp, context};
            const uint32_t ctx[2] = {irp, ret};
            nc_kernel_run_guest(k, f, routine, args, 3, NC_KCONT_IRP_COMPLETE, ctx, 2);
            return false;
        }
        if (pending && loc < count) {
            wr8(k, sl + STACK_BYTES + SL_CONTROL, rd8(k, sl + STACK_BYTES + SL_CONTROL) | SL_PENDING_RETURNED);
        }
    }
}

void nc_irp_complete(NcKernel *k, NcFrame *f, uint32_t irp, uint32_t ret)
{
    if (complete_up(k, f, irp, ret)) {
        finish(k, irp);
    }
}

/* NC_KCONT_IRP_COMPLETE: ctx { irp, the call's result } */
static uint64_t cont_irp_complete(NcKernel *k, NcFrame *f, uint32_t eax, const uint32_t *ctx, unsigned nctx)
{
    if (eax != NC_STATUS_MORE_PROCESSING_REQUIRED) {
        nc_irp_complete(k, f, ctx[0], ctx[1]);
    }
    return ctx[1];
}

/* IofCompleteRequest(Irp, PriorityBoost), fastcall */
static uint64_t IofCompleteRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_irp_complete(k, f, a[0], 0);
    return 0;
}

/* IoInvalidDeviceRequest(DeviceObject, Irp): the dispatch routine of what a driver doesn't do */
static uint64_t IoInvalidDeviceRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_vm_wr32(k->vm, a[1] + IRP_STATUS, NC_STATUS_INVALID_DEVICE_REQUEST);
    nc_vm_wr32(k->vm, a[1] + IRP_INFORMATION, 0);
    nc_irp_complete(k, f, a[1], NC_STATUS_INVALID_DEVICE_REQUEST);
    return NC_STATUS_INVALID_DEVICE_REQUEST;
}

/* ------------------------------------------------------------- calling */

/* Moves irp to its next stack location, for dev; the dispatch routine (0:
 * none; the packet is completed with an invalid device request then, *st). */
static uint32_t pass_down(NcKernel *k, NcFrame *f, uint32_t dev, uint32_t irp, uint32_t *st)
{
    const int8_t loc = (int8_t)(rd8(k, irp + IRP_CURRENT_LOCATION) - 1);
    *st = NC_STATUS_SUCCESS;
    if (loc <= 0) {
        nc_kernel_request_stop(k, f, "KeBugCheckEx NO_MORE_IRP_STACK_LOCATIONS (0x35): IRP %08x to device %08x",
                               irp, dev);
        *st = NC_STATUS_INVALID_PARAMETER;
        return 0;
    }
    wr8(k, irp + IRP_CURRENT_LOCATION, (uint8_t)loc);
    const uint32_t sl = next_location(k, irp);
    nc_vm_wr32(k->vm, irp + IRP_CURRENT_STACK, sl);
    nc_vm_wr32(k->vm, sl + SL_DEVICE, dev);
    const uint8_t major = rd8(k, sl + SL_MAJOR);
    const uint32_t drv = dev ? nc_vm_rd32(k->vm, dev + DEV_DRIVER) : 0;
    const uint32_t routine = drv && major < IRP_MJ_COUNT ? nc_vm_rd32(k->vm, drv + DRV_MAJOR + 4 * major) : 0;
    if (nc_io_trace()) {
        nc_log("hakuX-native-io", "IRP %08x: request %02x to device %08x, routine %08x", irp, major, dev, routine);
    }
    if (!routine) {
        static unsigned told;
        if (told++ < 8) {
            nc_log("hakuX-native-io", "IRP %08x: request %02x to device %08x, which has no routine for it", irp,
                   major, dev);
        }
        *st = NC_STATUS_INVALID_DEVICE_REQUEST;
        nc_vm_wr32(k->vm, irp + IRP_STATUS, *st);
        nc_vm_wr32(k->vm, irp + IRP_INFORMATION, 0);
    }
    return routine;
}

/* IofCallDriver(DeviceObject, Irp), fastcall: the driver's status. A title's
 * routine is a tail call, returning to IofCallDriver's caller with EAX. */
static uint64_t IofCallDriver(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    uint32_t st;
    const uint32_t routine = pass_down(k, f, a[0], a[1], &st);
    if (!routine) {
        if (!k->stop) {
            nc_irp_complete(k, f, a[1], st);
        }
        return st;
    }
    if (nc_iodrv_is_kernel_routine(routine)) {
        return nc_iodrv_dispatch(k, f, a);
    }
    const uint32_t esp = nc_frame_gpr(f, NC_ESP), ret = nc_vm_rd32(k->vm, esp);
    const uint32_t sp = esp - 8; /* routine(dev, irp) pops 12: the caller's ESP after a fastcall */
    nc_vm_wr32(k->vm, sp, ret);
    nc_vm_wr32(k->vm, sp + 4, a[0]);
    nc_vm_wr32(k->vm, sp + 8, a[1]);
    nc_frame_set_gpr(f, NC_ESP, sp);
    nc_frame_set_eip(f, routine);
    k->redirected = true;
    return 0;
}

/* ------------------------------------------------- synchronous requests */

static uint32_t sync_done(NcKernel *k, uint32_t blk, uint32_t st)
{
    const uint32_t retlen = nc_vm_rd32(k->vm, blk + SB_RETLEN);
    if (retlen) {
        nc_vm_wr32(k->vm, retlen, nc_vm_rd32(k->vm, blk + SB_IOSB + 4));
    }
    nc_pool_free(k, blk, SB_BYTES);
    return st;
}

/* NC_KCONT_SYNC_CALLED: the dispatch returned; ctx { block } */
static uint64_t cont_sync_called(NcKernel *k, NcFrame *f, uint32_t st, const uint32_t *ctx, unsigned nctx)
{
    if (st != NC_STATUS_PENDING) {
        return sync_done(k, ctx[0], st);
    }
    /* KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL) */
    const uint32_t args[5] = {ctx[0] + SB_EVENT, 0, 0, 0, 0};
    nc_kernel_run_guest(k, f, nc_kernel_export_va(k, "KeWaitForSingleObject"), args, 5, NC_KCONT_SYNC_WAITED, ctx, 1);
    return 0;
}

/* NC_KCONT_SYNC_WAITED: the request completed */
static uint64_t cont_sync_waited(NcKernel *k, NcFrame *f, uint32_t eax, const uint32_t *ctx, unsigned nctx)
{
    return sync_done(k, ctx[0], nc_vm_rd32(k->vm, ctx[0] + SB_IOSB));
}

static uint32_t sync_block(NcKernel *k, uint32_t retlen)
{
    const uint32_t blk = nc_pool_alloc(k, SB_BYTES);
    if (blk) {
        memset(nc_vm_host(k->vm, blk), 0, SB_BYTES);
        nc_ke_init_event(k, blk + SB_EVENT, 0, false);
        nc_vm_wr32(k->vm, blk + SB_RETLEN, retlen);
    }
    return blk;
}

/* Calls dev's driver with irp and, if it pends, waits for the block's event. */
static uint64_t sync_call(NcKernel *k, NcFrame *f, uint32_t dev, uint32_t irp, uint32_t blk)
{
    uint32_t st;
    const uint32_t routine = pass_down(k, f, dev, irp, &st);
    const uint32_t ctx[1] = {blk};
    if (!routine) {
        if (k->stop) {
            return st;
        }
        nc_irp_complete(k, f, irp, st);
        return sync_done(k, blk, st);
    }
    if (nc_iodrv_is_kernel_routine(routine)) {
        const uint32_t args[2] = {dev, irp};
        st = (uint32_t)nc_iodrv_dispatch(k, f, args);
        return k->redirected ? 0 : cont_sync_called(k, f, st, ctx, 1);
    }
    const uint32_t args[2] = {dev, irp};
    nc_kernel_run_guest(k, f, routine, args, 2, NC_KCONT_SYNC_CALLED, ctx, 1);
    return 0;
}

/* IoSynchronousFsdRequest(MajorFunction, DeviceObject, Buffer, Length, StartingOffset) */
static uint64_t IoSynchronousFsdRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t blk = sync_block(k, 0);
    const uint32_t irp = blk ? build_fsd(k, a[0], a[1], a[2], a[3], a[4], blk + SB_IOSB) : 0;
    if (!irp) {
        if (blk) {
            nc_pool_free(k, blk, SB_BYTES);
        }
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, irp + IRP_USER_EVENT, blk + SB_EVENT);
    thread_queue(k, irp);
    return sync_call(k, f, a[1], irp, blk);
}

/* IoSynchronousDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputBufferLength,
 *   OutputBuffer, OutputBufferLength, ReturnedOutputBufferLength, InternalDeviceIoControl) */
static uint64_t IoSynchronousDeviceIoControlRequest(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t blk = sync_block(k, a[6]);
    const uint32_t irp = blk ? build_ioctl(k, a[0], a[1], a[2], a[3], a[4], a[5], a[7] & 0xFF, blk + SB_EVENT,
                                           blk + SB_IOSB)
                             : 0;
    if (!irp) {
        if (blk) {
            nc_pool_free(k, blk, SB_BYTES);
        }
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    return sync_call(k, f, a[1], irp, blk);
}

/* ------------------------------------------------------- device queues */

static uint64_t start_io(NcKernel *k, NcFrame *f, uint32_t dev, uint32_t irp, enum NcKContId cont,
                         const uint32_t *ctx, unsigned nctx)
{
    nc_vm_wr32(k->vm, dev + DEV_CURRENT_IRP, irp);
    const uint32_t drv = nc_vm_rd32(k->vm, dev + DEV_DRIVER);
    const uint32_t routine = drv ? nc_vm_rd32(k->vm, drv + DRV_STARTIO) : 0;
    if (!routine) {
        nc_log("hakuX-native-io", "IRP %08x queued to device %08x, whose driver has no StartIo", irp, dev);
        return 0;
    }
    const uint32_t args[2] = {dev, irp};
    nc_kernel_run_guest(k, f, routine, args, 2, cont, ctx, nctx);
    return 0;
}

/* NC_KCONT_STARTIO: ctx { the IRQL IoStartPacket raised from } */
static uint64_t cont_startio(NcKernel *k, NcFrame *f, uint32_t eax, const uint32_t *ctx, unsigned nctx)
{
    lower_irql(k, (uint8_t)ctx[0]);
    return 0;
}

/* IoStartPacket(DeviceObject, Irp, Key): StartIo now if the device is idle,
 * else queued (by key when there is one), at DISPATCH_LEVEL */
static uint64_t IoStartPacket(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint8_t old = *irql(k);
    if (old < DISPATCH_LEVEL) {
        *irql(k) = DISPATCH_LEVEL;
    }
    const uint32_t entry = a[1] + IRP_QUEUE_ENTRY;
    const bool queued = nc_kdq_insert(k, a[0] + DEV_QUEUE, entry, a[2] != 0, a[2] ? nc_vm_rd32(k->vm, a[2]) : 0);
    if (nc_io_trace()) {
        nc_log("hakuX-native-io", "IRP %08x to device %08x: %s", a[1], a[0], queued ? "queued" : "started");
    }
    if (queued) {
        lower_irql(k, old);
        return 0;
    }
    const uint32_t ctx[1] = {old};
    start_io(k, f, a[0], a[1], NC_KCONT_STARTIO, ctx, 1);
    if (!k->redirected) {
        lower_irql(k, old);
    }
    return 0;
}

static uint64_t start_next(NcKernel *k, NcFrame *f, uint32_t dev, bool by_key, uint32_t key)
{
    nc_vm_wr32(k->vm, dev + DEV_CURRENT_IRP, 0);
    const uint32_t entry = nc_kdq_remove(k, dev + DEV_QUEUE, by_key, key);
    if (!entry) {
        return 0;
    }
    return start_io(k, f, dev, entry - IRP_QUEUE_ENTRY, NC_KCONT_RETURN, NULL, 0);
}

/* IoStartNextPacket(DeviceObject) */
static uint64_t IoStartNextPacket(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return start_next(k, f, a[0], false, 0);
}

/* IoStartNextPacketByKey(DeviceObject, Key) */
static uint64_t IoStartNextPacketByKey(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return start_next(k, f, a[0], true, a[1]);
}

/* -------------------------------------------------------- share access */

/* SHARE_ACCESS { UCHAR OpenCount, Readers, Writers, Deleters, SharedRead,
 * SharedWrite, SharedDelete }; FILE_OBJECT's byte +2 holds DeletePending,
 * ReadAccess, WriteAccess, DeleteAccess, SharedRead, SharedWrite and
 * SharedDelete from bit 0 up (nxdk's layout; no XDK library calls these). */
enum { SA_OPEN, SA_READERS, SA_WRITERS, SA_DELETERS, SA_SH_READ, SA_SH_WRITE, SA_SH_DELETE };
#define FO_ACCESS 0x02u
#define FA_READ   0x02u
#define FA_WRITE  0x04u
#define FA_DELETE 0x08u
#define FA_SH_READ   0x10u
#define FA_SH_WRITE  0x20u
#define FA_SH_DELETE 0x40u

static uint8_t access_bits(uint32_t desired, uint32_t share)
{
    if (desired & 0x80000000u) {
        desired |= 0x21u;           /* GENERIC_READ: read, execute */
    }
    if (desired & 0x40000000u) {
        desired |= 0x06u;           /* GENERIC_WRITE: write, append */
    }
    if (desired & 0x10000000u) {
        desired |= 0x10027u;        /* GENERIC_ALL */
    }
    uint8_t b = 0;
    b |= desired & 0x21u ? FA_READ : 0;         /* FILE_READ_DATA, FILE_EXECUTE */
    b |= desired & 0x06u ? FA_WRITE : 0;        /* FILE_WRITE_DATA, FILE_APPEND_DATA */
    b |= desired & 0x10000u ? FA_DELETE : 0;    /* DELETE */
    if (b) {
        b |= share & 1 ? FA_SH_READ : 0;
        b |= share & 2 ? FA_SH_WRITE : 0;
        b |= share & 4 ? FA_SH_DELETE : 0;
    }
    return b;
}

static void share_add(uint8_t *s, uint8_t b, int d)
{
    s[SA_OPEN] = (uint8_t)(s[SA_OPEN] + d);
    s[SA_READERS] = (uint8_t)(s[SA_READERS] + (b & FA_READ ? d : 0));
    s[SA_WRITERS] = (uint8_t)(s[SA_WRITERS] + (b & FA_WRITE ? d : 0));
    s[SA_DELETERS] = (uint8_t)(s[SA_DELETERS] + (b & FA_DELETE ? d : 0));
    s[SA_SH_READ] = (uint8_t)(s[SA_SH_READ] + (b & FA_SH_READ ? d : 0));
    s[SA_SH_WRITE] = (uint8_t)(s[SA_SH_WRITE] + (b & FA_SH_WRITE ? d : 0));
    s[SA_SH_DELETE] = (uint8_t)(s[SA_SH_DELETE] + (b & FA_SH_DELETE ? d : 0));
}

static void set_access(NcKernel *k, uint32_t file, uint8_t b)
{
    uint8_t *p = nc_vm_host(k->vm, file + FO_ACCESS);
    *p = (uint8_t)((*p & 0x81u) | b);
}

/* IoCheckShareAccess(DesiredAccess, DesiredShareAccess, FileObject, ShareAccess, Update) */
static uint64_t IoCheckShareAccess(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint8_t b = access_bits(a[0], a[1]);
    set_access(k, a[2], b);
    if (!(b & (FA_READ | FA_WRITE | FA_DELETE))) {
        return NC_STATUS_SUCCESS;
    }
    uint8_t *s = nc_vm_host(k->vm, a[3]);
    if (((b & FA_READ) && s[SA_SH_READ] < s[SA_OPEN]) || ((b & FA_WRITE) && s[SA_SH_WRITE] < s[SA_OPEN]) ||
        ((b & FA_DELETE) && s[SA_SH_DELETE] < s[SA_OPEN]) || (s[SA_READERS] && !(b & FA_SH_READ)) ||
        (s[SA_WRITERS] && !(b & FA_SH_WRITE)) || (s[SA_DELETERS] && !(b & FA_SH_DELETE))) {
        return NC_STATUS_SHARING_VIOLATION;
    }
    if (a[4] & 0xFF) {
        share_add(s, b, 1);
    }
    return NC_STATUS_SUCCESS;
}

/* IoSetShareAccess(DesiredAccess, DesiredShareAccess, FileObject, ShareAccess): the first open */
static uint64_t IoSetShareAccess(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint8_t b = access_bits(a[0], a[1]);
    set_access(k, a[2], b);
    uint8_t *s = nc_vm_host(k->vm, a[3]);
    memset(s, 0, 7);
    if (b & (FA_READ | FA_WRITE | FA_DELETE)) {
        share_add(s, b, 1);
    }
    return 0;
}

/* IoRemoveShareAccess(FileObject, ShareAccess) */
static uint64_t IoRemoveShareAccess(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint8_t b = *(const uint8_t *)nc_vm_host(k->vm, a[0] + FO_ACCESS) & 0x7Eu;
    if (b & (FA_READ | FA_WRITE | FA_DELETE)) {
        share_add(nc_vm_host(k->vm, a[1]), b, -1);
    }
    return 0;
}

void nc_irp_register(NcKernel *k)
{
    nc_kcont_register(NC_KCONT_IRP_COMPLETE, cont_irp_complete);
    nc_kcont_register(NC_KCONT_STARTIO, cont_startio);
    nc_kcont_register(NC_KCONT_SYNC_CALLED, cont_sync_called);
    nc_kcont_register(NC_KCONT_SYNC_WAITED, cont_sync_waited);
    nc_kernel_impl(k, "IoAllocateIrp", IoAllocateIrp);
    nc_kernel_impl(k, "IoInitializeIrp", IoInitializeIrp);
    nc_kernel_impl(k, "IoFreeIrp", IoFreeIrp);
    nc_kernel_impl(k, "IoBuildAsynchronousFsdRequest", IoBuildAsynchronousFsdRequest);
    nc_kernel_impl(k, "IoBuildSynchronousFsdRequest", IoBuildSynchronousFsdRequest);
    nc_kernel_impl(k, "IoBuildDeviceIoControlRequest", IoBuildDeviceIoControlRequest);
    nc_kernel_impl(k, "IoQueueThreadIrp", IoQueueThreadIrp);
    nc_kernel_impl(k, "IoMarkIrpMustComplete", IoMarkIrpMustComplete);
    nc_kernel_impl(k, "IofCallDriver", IofCallDriver);
    nc_kernel_impl(k, "IofCompleteRequest", IofCompleteRequest);
    nc_kernel_impl(k, "IoInvalidDeviceRequest", IoInvalidDeviceRequest);
    nc_kernel_impl(k, "IoSynchronousFsdRequest", IoSynchronousFsdRequest);
    nc_kernel_impl(k, "IoSynchronousDeviceIoControlRequest", IoSynchronousDeviceIoControlRequest);
    nc_kernel_impl(k, "IoStartPacket", IoStartPacket);
    nc_kernel_impl(k, "IoStartNextPacket", IoStartNextPacket);
    nc_kernel_impl(k, "IoStartNextPacketByKey", IoStartNextPacketByKey);
    nc_kernel_impl(k, "IoCheckShareAccess", IoCheckShareAccess);
    nc_kernel_impl(k, "IoSetShareAccess", IoSetShareAccess);
    nc_kernel_impl(k, "IoRemoveShareAccess", IoRemoveShareAccess);
}
