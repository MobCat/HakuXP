/*
 * hakuX native core -- kernel queues: device queues (a driver's pending
 * requests, IoStartPacket), KQUEUE and the I/O completion objects built on
 * it (XAPI's CreateIoCompletionPort, GetQueuedCompletionStatus,
 * PostQueuedCompletionStatus).
 *
 * Both live in guest memory with the Xbox layouts, so a savestate holds
 * them as the guest left them:
 *   KDEVICE_QUEUE { CSHORT Type 0x14; UCHAR Size; BOOLEAN Busy;
 *                   LIST_ENTRY DeviceListHead +4 }            0x0C
 *   KDEVICE_QUEUE_ENTRY { LIST_ENTRY DeviceListEntry; ULONG SortKey +8;
 *                         BOOLEAN Inserted +0x0C }            0x10
 *   KQUEUE { DISPATCHER_HEADER (Type 4, SignalState: the entries);
 *            LIST_ENTRY EntryListHead +0x10; ULONG CurrentCount +0x18;
 *            ULONG MaximumCount +0x1C; LIST_ENTRY ThreadListHead +0x20 } 0x28
 *
 * A thread that waits in KeRemoveQueue waits on the queue as a dispatcher
 * object (signaled while it holds entries; the wait takes nothing) and
 * takes the head entry once woken, waiting again if another thread was
 * quicker: NT hands the entry to the waiter directly. The queue's
 * concurrency limit (MaximumCount, the threads it counts as running) is
 * not applied: every waiter may take an entry.
 */
#include "native/kernel/iomgr.h"

#define OBJ_QUEUE         4
#define KQ_SIGNAL_STATE   0x04u
#define KQ_WAIT_LIST      0x08u
#define KQ_ENTRIES        0x10u
#define KQ_CURRENT        0x18u
#define KQ_MAXIMUM        0x1Cu
#define KQ_THREADS        0x20u
#define KQUEUE_BYTES      0x28u

/* A completion packet: LIST_ENTRY, KeyContext, ApcContext, Status, Information. */
#define PKT_KEY     0x08u
#define PKT_APC     0x0Cu
#define PKT_STATUS  0x10u
#define PKT_INFO    0x14u
#define PKT_BYTES   0x18u

/* ------------------------------------------------------- device queues */

void nc_kdq_init(NcKernel *k, uint32_t q)
{
    uint8_t *p = nc_vm_host(k->vm, q);
    *(int16_t *)p = DEVICE_QUEUE_OBJECT;
    p[2] = 0x0C;
    p[KDQ_BUSY] = 0;
    nc_list_init(k, q + KDQ_HEAD);
}

bool nc_kdq_insert(NcKernel *k, uint32_t q, uint32_t entry, bool by_key, uint32_t key)
{
    uint8_t *busy = nc_vm_host(k->vm, q + KDQ_BUSY);
    uint8_t *inserted = nc_vm_host(k->vm, entry + KDQE_INSERTED);
    if (!*busy) {
        *busy = 1;
        *inserted = 0;
        return false;
    }
    const uint32_t head = q + KDQ_HEAD;
    uint32_t next = head;
    if (by_key) {
        nc_vm_wr32(k->vm, entry + KDQE_KEY, key);
        /* before the first entry with a greater key: equal keys keep their order */
        for (next = nc_vm_rd32(k->vm, head); next && next != head; next = nc_vm_rd32(k->vm, next)) {
            if (nc_vm_rd32(k->vm, next + KDQE_KEY) > key) {
                break;
            }
        }
        if (!next) {
            next = head;
        }
    }
    if (!nc_vm_rd32(k->vm, head)) {
        nc_list_init(k, head);
    }
    nc_list_link(k, nc_vm_rd32(k->vm, next + 4), entry, next);
    *inserted = 1;
    return true;
}

uint32_t nc_kdq_remove(NcKernel *k, uint32_t q, bool by_key, uint32_t key)
{
    const uint32_t head = q + KDQ_HEAD;
    if (nc_list_empty(k, head)) {
        *(uint8_t *)nc_vm_host(k->vm, q + KDQ_BUSY) = 0;
        return 0;
    }
    uint32_t entry = nc_vm_rd32(k->vm, head);
    if (by_key) {
        for (uint32_t e = entry; e && e != head; e = nc_vm_rd32(k->vm, e)) {
            if (nc_vm_rd32(k->vm, e + KDQE_KEY) >= key) {
                entry = e;
                break;
            }
        }
    }
    nc_list_remove(k, entry);
    *(uint8_t *)nc_vm_host(k->vm, entry + KDQE_INSERTED) = 0;
    return entry;
}

/* KeInitializeDeviceQueue(DeviceQueue) */
static uint64_t KeInitializeDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_kdq_init(k, a[0]);
    return 0;
}

/* KeInsertDeviceQueue(DeviceQueue, DeviceQueueEntry) -> queued */
static uint64_t KeInsertDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_kdq_insert(k, a[0], a[1], false, 0);
}

/* KeInsertByKeyDeviceQueue(DeviceQueue, DeviceQueueEntry, SortKey) -> queued */
static uint64_t KeInsertByKeyDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_kdq_insert(k, a[0], a[1], true, a[2]);
}

/* KeRemoveDeviceQueue(DeviceQueue) -> entry or NULL */
static uint64_t KeRemoveDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_kdq_remove(k, a[0], false, 0);
}

/* KeRemoveByKeyDeviceQueue(DeviceQueue, SortKey) -> entry or NULL */
static uint64_t KeRemoveByKeyDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_kdq_remove(k, a[0], true, a[1]);
}

/* KeRemoveEntryDeviceQueue(DeviceQueue, DeviceQueueEntry) -> it was queued */
static uint64_t KeRemoveEntryDeviceQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    uint8_t *inserted = nc_vm_host(k->vm, a[1] + KDQE_INSERTED);
    if (!*inserted) {
        return 0;
    }
    nc_list_remove(k, a[1]);
    *inserted = 0;
    return 1;
}

/* -------------------------------------------------------------- KQUEUE */

static void queue_init(NcKernel *k, uint32_t q, uint32_t count)
{
    uint8_t *p = nc_vm_host(k->vm, q);
    memset(p, 0, KQUEUE_BYTES);
    p[0] = OBJ_QUEUE;
    p[2] = KQUEUE_BYTES / 4;
    nc_list_init(k, q + KQ_WAIT_LIST);
    nc_list_init(k, q + KQ_ENTRIES);
    nc_vm_wr32(k->vm, q + KQ_MAXIMUM, count ? count : 1);
    nc_list_init(k, q + KQ_THREADS);
}

static bool is_queue(NcKernel *k, uint32_t q)
{
    return q && nc_vm_phys(k->vm, q) != UINT32_MAX && *(const uint8_t *)nc_vm_host(k->vm, q) == OBJ_QUEUE;
}

/* Adds an entry; returns the previous count (KeInsertQueue's value). */
static uint32_t queue_insert(NcKernel *k, uint32_t q, uint32_t entry, bool head)
{
    const int32_t was = (int32_t)nc_vm_rd32(k->vm, q + KQ_SIGNAL_STATE);
    if (head) {
        nc_list_insert_head(k, q + KQ_ENTRIES, entry);
    } else {
        nc_list_insert_tail(k, q + KQ_ENTRIES, entry);
    }
    nc_vm_wr32(k->vm, q + KQ_SIGNAL_STATE, (uint32_t)(was + 1));
    nc_ke_signaled(k);
    return (uint32_t)was;
}

/* The head entry, or 0. */
static uint32_t queue_take(NcKernel *k, uint32_t q)
{
    const uint32_t entry = nc_list_remove_head(k, q + KQ_ENTRIES);
    if (entry) {
        const int32_t n = (int32_t)nc_vm_rd32(k->vm, q + KQ_SIGNAL_STATE);
        nc_vm_wr32(k->vm, q + KQ_SIGNAL_STATE, (uint32_t)(n > 0 ? n - 1 : 0));
    }
    return entry;
}

/* KeInitializeQueue(Queue, Count) */
static uint64_t KeInitializeQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    queue_init(k, a[0], a[1]);
    return 0;
}

/* KeInsertQueue(Queue, Entry) -> the previous count */
static uint64_t KeInsertQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return queue_insert(k, a[0], a[1], false);
}

/* KeInsertHeadQueue(Queue, Entry) -> the previous count */
static uint64_t KeInsertHeadQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return queue_insert(k, a[0], a[1], true);
}

/* KeRundownQueue(Queue) -> the entries left, as a ring without its head, or NULL */
static uint64_t KeRundownQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t head = a[0] + KQ_ENTRIES;
    if (nc_list_empty(k, head)) {
        return 0;
    }
    const uint32_t first = nc_vm_rd32(k->vm, head), last = nc_vm_rd32(k->vm, head + 4);
    nc_vm_wr32(k->vm, last, first);
    nc_vm_wr32(k->vm, first + 4, last);
    nc_list_init(k, head);
    nc_vm_wr32(k->vm, a[0] + KQ_SIGNAL_STATE, 0);
    return first;
}

/* The removal that KeRemoveQueue and NtRemoveIoCompletion share: ctx is
 * { queue, timeout, nt, key out, apc context out, iosb out }. */
enum { RQ_QUEUE, RQ_TIMEOUT, RQ_NT, RQ_KEY, RQ_APC, RQ_IOSB, RQ_WORDS };

/* NtRemoveIoCompletion's outputs from a packet, which goes back to the pool. */
static uint32_t packet_out(NcKernel *k, uint32_t pkt, const uint32_t *ctx)
{
    if (ctx[RQ_KEY]) {
        nc_vm_wr32(k->vm, ctx[RQ_KEY], nc_vm_rd32(k->vm, pkt + PKT_KEY));
    }
    if (ctx[RQ_APC]) {
        nc_vm_wr32(k->vm, ctx[RQ_APC], nc_vm_rd32(k->vm, pkt + PKT_APC));
    }
    if (ctx[RQ_IOSB]) {
        nc_vm_wr32(k->vm, ctx[RQ_IOSB], nc_vm_rd32(k->vm, pkt + PKT_STATUS));
        nc_vm_wr32(k->vm, ctx[RQ_IOSB] + 4, nc_vm_rd32(k->vm, pkt + PKT_INFO));
    }
    nc_pool_free(k, pkt, PKT_BYTES);
    return NC_STATUS_SUCCESS;
}

/* Takes an entry, or waits for one (the wait's own trampoline, then
 * NC_KCONT_REMOVE_QUEUE). A zero timeout polls. */
static uint64_t remove_or_wait(NcKernel *k, NcFrame *f, const uint32_t *ctx)
{
    const uint32_t entry = queue_take(k, ctx[RQ_QUEUE]);
    if (entry) {
        return ctx[RQ_NT] ? packet_out(k, entry, ctx) : entry;
    }
    const uint32_t to = ctx[RQ_TIMEOUT];
    if (to && !nc_vm_rd32(k->vm, to) && !nc_vm_rd32(k->vm, to + 4)) {
        return NC_STATUS_TIMEOUT;
    }
    const uint32_t wait = nc_kernel_export_va(k, "KeWaitForSingleObject");
    /* KeWaitForSingleObject(Object, WrQueue 15, KernelMode, Alertable FALSE, Timeout) */
    const uint32_t args[5] = {ctx[RQ_QUEUE], 15, 0, 0, to};
    nc_kernel_run_guest(k, f, wait, args, 5, NC_KCONT_REMOVE_QUEUE, ctx, RQ_WORDS);
    return 0;
}

static uint64_t cont_remove_queue(NcKernel *k, NcFrame *f, uint32_t eax, const uint32_t *ctx, unsigned nctx)
{
    if (eax != NC_STATUS_SUCCESS) {
        return eax; /* timed out, or an APC ran (STATUS_USER_APC) */
    }
    return remove_or_wait(k, f, ctx);
}

/* KeRemoveQueue(Queue, WaitMode, Timeout) -> an entry, or the wait's status */
static uint64_t KeRemoveQueue(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t ctx[RQ_WORDS] = {a[0], a[2], 0, 0, 0, 0};
    return remove_or_wait(k, f, ctx);
}

/* ------------------------------------------------ I/O completion objects */

bool nc_kqueue_post(NcKernel *k, uint32_t port, uint32_t key, uint32_t apc_context, uint32_t status,
                    uint32_t information)
{
    if (!is_queue(k, port)) {
        return false;
    }
    const uint32_t pkt = nc_pool_alloc(k, PKT_BYTES);
    if (!pkt) {
        nc_log("hakuX-native-io", "completion packet for port %08x: no pool left", port);
        return false;
    }
    nc_vm_wr32(k->vm, pkt + PKT_KEY, key);
    nc_vm_wr32(k->vm, pkt + PKT_APC, apc_context);
    nc_vm_wr32(k->vm, pkt + PKT_STATUS, status);
    nc_vm_wr32(k->vm, pkt + PKT_INFO, information);
    queue_insert(k, port, pkt, false);
    if (nc_io_trace()) {
        nc_log("hakuX-native-io", "completion port %08x: key %08x context %08x status %08x info %u", port, key,
               apc_context, status, information);
    }
    return true;
}

static uint32_t port_of(NcKernel *k, uint32_t handle)
{
    enum NcObType type;
    const uint32_t obj = nc_ob_lookup(k, handle, &type);
    return obj && type == NC_OB_IOCOMPLETION ? obj : 0;
}

/* NtCreateIoCompletion(IoCompletionHandle, DesiredAccess, ObjectAttributes, Count) */
static uint64_t NtCreateIoCompletion(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char path[256];
    uint32_t existing = 0, st = 0;
    const int named = nc_obname_precreate(k, a[2], NC_OB_IOCOMPLETION, path, sizeof(path), &existing, &st);
    if (named < 0) {
        return st;
    }
    uint32_t q = existing;
    if (!named) {
        q = nc_ob_create(k, NC_OB_IOCOMPLETION, KQUEUE_BYTES);
        if (!q) {
            return NC_STATUS_INSUFFICIENT_RESOURCES;
        }
        queue_init(k, q, a[3]);
    }
    return nc_obname_created(k, a[0], q, NC_OB_IOCOMPLETION, path, named, st);
}

/* NtSetIoCompletion(IoCompletionHandle, KeyContext, ApcContext, IoStatus, IoStatusInformation) */
static uint64_t NtSetIoCompletion(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t q = port_of(k, a[0]);
    if (!q) {
        return NC_STATUS_INVALID_HANDLE;
    }
    return nc_kqueue_post(k, q, a[1], a[2], a[3], a[4]) ? NC_STATUS_SUCCESS : NC_STATUS_INSUFFICIENT_RESOURCES;
}

/* IoSetIoCompletion(IoCompletion, KeyContext, ApcContext, IoStatus, IoStatusInformation):
 * the same on the object */
static uint64_t IoSetIoCompletion(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return nc_kqueue_post(k, a[0], a[1], a[2], a[3], a[4]) ? NC_STATUS_SUCCESS : NC_STATUS_INSUFFICIENT_RESOURCES;
}

/* NtRemoveIoCompletion(IoCompletionHandle, KeyContext, ApcContext, IoStatusBlock, Timeout) */
static uint64_t NtRemoveIoCompletion(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t q = port_of(k, a[0]);
    if (!q) {
        return NC_STATUS_INVALID_HANDLE;
    }
    const uint32_t ctx[RQ_WORDS] = {q, a[4], 1, a[1], a[2], a[3]};
    return remove_or_wait(k, f, ctx);
}

/* NtQueryIoCompletion(IoCompletionHandle, IoCompletionInformation): its Depth */
static uint64_t NtQueryIoCompletion(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t q = port_of(k, a[0]);
    if (!q) {
        return NC_STATUS_INVALID_HANDLE;
    }
    nc_vm_wr32(k->vm, a[1], nc_vm_rd32(k->vm, q + KQ_SIGNAL_STATE));
    return NC_STATUS_SUCCESS;
}

void nc_kqueue_register(NcKernel *k)
{
    nc_kcont_register(NC_KCONT_REMOVE_QUEUE, cont_remove_queue);
    nc_kernel_impl(k, "KeInitializeDeviceQueue", KeInitializeDeviceQueue);
    nc_kernel_impl(k, "KeInsertDeviceQueue", KeInsertDeviceQueue);
    nc_kernel_impl(k, "KeInsertByKeyDeviceQueue", KeInsertByKeyDeviceQueue);
    nc_kernel_impl(k, "KeRemoveDeviceQueue", KeRemoveDeviceQueue);
    nc_kernel_impl(k, "KeRemoveByKeyDeviceQueue", KeRemoveByKeyDeviceQueue);
    nc_kernel_impl(k, "KeRemoveEntryDeviceQueue", KeRemoveEntryDeviceQueue);
    nc_kernel_impl(k, "KeInitializeQueue", KeInitializeQueue);
    nc_kernel_impl(k, "KeInsertQueue", KeInsertQueue);
    nc_kernel_impl(k, "KeInsertHeadQueue", KeInsertHeadQueue);
    nc_kernel_impl(k, "KeRemoveQueue", KeRemoveQueue);
    nc_kernel_impl(k, "KeRundownQueue", KeRundownQueue);
    nc_kernel_impl(k, "NtCreateIoCompletion", NtCreateIoCompletion);
    nc_kernel_impl(k, "NtSetIoCompletion", NtSetIoCompletion);
    nc_kernel_impl(k, "IoSetIoCompletion", IoSetIoCompletion);
    nc_kernel_impl(k, "NtRemoveIoCompletion", NtRemoveIoCompletion);
    nc_kernel_impl(k, "NtQueryIoCompletion", NtQueryIoCompletion);
}
