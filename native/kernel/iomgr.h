/*
 * hakuX native core -- the I/O manager's guest structures and the calls the
 * kernel's I/O modules make to each other.
 *
 *   io.c      files on the disc and the hard disk, symbolic links
 *   irp.c     IRPs: building, IofCallDriver, IofCompleteRequest, device
 *             queues and StartIo, the synchronous requests
 *   iodrv.c   the kernel's own drivers (the disc and the hard disk as
 *             devices), device objects, and where a USB backend's devices
 *             plug in later
 *   kqueue.c  KQUEUE, I/O completion objects (completion ports)
 *   obname.c  the object namespace: names of devices, events, semaphores,
 *             mutants, timers; object directories
 *
 * The layouts are the Xbox kernel's (x86, MSVC alignment), checked against
 * XAPI's own drivers in the XDK's xapilib.lib: the memory unit driver
 * (disk.obj, mu.obj) reads the current stack location at IRP+0x5C, the
 * user buffer at IRP+0x30, a stack location's DeviceObject at +0x14 and a
 * read's offset at +0x0C; CancelIo (cancelio.obj) finds a queued IRP's
 * DeviceQueueEntry at IRP+0x3C and its OriginalFileObject at IRP+0x60.
 */
#ifndef NATIVE_KERNEL_IOMGR_H
#define NATIVE_KERNEL_IOMGR_H

#include "native/kernel/kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ statuses */

#define NC_STATUS_MORE_PROCESSING_REQUIRED 0xC0000016u
#define NC_STATUS_INVALID_DEVICE_REQUEST   0xC0000010u
#define NC_STATUS_OBJECT_NAME_NOT_FOUND    0xC0000034u
#define NC_STATUS_OBJECT_NAME_COLLISION    0xC0000035u
#define NC_STATUS_OBJECT_NAME_EXISTS       0x40000000u
#define NC_STATUS_OBJECT_PATH_NOT_FOUND    0xC000003Au
#define NC_STATUS_OBJECT_NAME_INVALID      0xC0000033u
#define NC_STATUS_BUFFER_TOO_SMALL         0xC0000023u
#define NC_STATUS_NO_MORE_ENTRIES          0x8000001Au
#define NC_STATUS_CANCELLED                0xC0000120u
#define NC_STATUS_DEVICE_DATA_ERROR        0xC000009Cu
#define NC_STATUS_DEVICE_NOT_CONNECTED     0xC000009Du
#define NC_STATUS_NO_MEDIA_IN_DEVICE       0xC0000013u
#define NC_STATUS_UNRECOGNIZED_VOLUME      0xC000014Fu
#define NC_STATUS_SHARING_VIOLATION        0xC0000043u
#define NC_STATUS_ACCESS_DENIED            0xC0000022u
#define NC_STATUS_USER_APC                 0x000000C0u

/* --------------------------------------------------------------- IRPs */

#define IO_TYPE_DEVICE 3
#define IO_TYPE_DRIVER 4
#define IO_TYPE_FILE   5
#define IO_TYPE_IRP    6

#define IRP_MJ_CREATE                   0x00
#define IRP_MJ_CLOSE                    0x01
#define IRP_MJ_READ                     0x02
#define IRP_MJ_WRITE                    0x03
#define IRP_MJ_QUERY_INFORMATION        0x04
#define IRP_MJ_SET_INFORMATION          0x05
#define IRP_MJ_FLUSH_BUFFERS            0x06
#define IRP_MJ_QUERY_VOLUME_INFORMATION 0x07
#define IRP_MJ_DIRECTORY_CONTROL        0x08
#define IRP_MJ_FILE_SYSTEM_CONTROL      0x09
#define IRP_MJ_DEVICE_CONTROL           0x0A
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0B
#define IRP_MJ_SHUTDOWN                 0x0C
#define IRP_MJ_CLEANUP                  0x0D
#define IRP_MJ_COUNT                    0x0E

/* IRP (IRP_BYTES; its stack locations follow it) */
#define IRP_TYPE             0x00u /* CSHORT IO_TYPE_IRP */
#define IRP_SIZE             0x02u /* USHORT, the stack locations included */
#define IRP_FLAGS            0x04u
#define IRP_THREAD_LIST      0x08u /* LIST_ENTRY ThreadListEntry */
#define IRP_STATUS           0x10u /* IoStatus.Status */
#define IRP_INFORMATION      0x14u /* IoStatus.Information */
#define IRP_STACK_COUNT      0x18u /* CHAR */
#define IRP_CURRENT_LOCATION 0x19u /* CHAR: StackCount + 1 before the first IofCallDriver */
#define IRP_PENDING_RETURNED 0x1Au
#define IRP_CANCEL           0x1Bu
#define IRP_USER_IOSB        0x1Cu
#define IRP_USER_EVENT       0x20u
#define IRP_APC_ROUTINE      0x28u /* Overlay (8-aligned for its LARGE_INTEGER): UserApcRoutine */
#define IRP_APC_CONTEXT      0x2Cu /* UserApcContext */
#define IRP_USER_BUFFER      0x30u
#define IRP_SEGMENT_ARRAY    0x34u
#define IRP_LOCKED_LENGTH    0x38u
#define IRP_QUEUE_ENTRY      0x3Cu /* Tail.Overlay.DeviceQueueEntry (KDEVICE_QUEUE_ENTRY) */
#define IRP_THREAD           0x50u /* Tail.Overlay.Thread */
#define IRP_LIST_ENTRY       0x54u /* Tail.Overlay.ListEntry (completion packets) */
#define IRP_CURRENT_STACK    0x5Cu /* Tail.Overlay.CurrentStackLocation */
#define IRP_ORIGINAL_FILE    0x60u /* Tail.Overlay.OriginalFileObject */
#define IRP_BYTES            0x68u

/* IRP flags (Xbox): CancelIo leaves one marked must-complete alone. */
#define IRP_SYNCHRONOUS_API     0x00000004u
#define IRP_INPUT_OPERATION     0x00000040u
#define IRP_MUST_COMPLETE       0x00001000u
#define IRP_KERNEL_ALLOCATED    0x00010000u /* HakuXP's: from the kernel's pool, freed at completion */

/* IO_STACK_LOCATION (STACK_BYTES) */
#define SL_MAJOR          0x00u
#define SL_MINOR          0x01u
#define SL_FLAGS          0x02u
#define SL_CONTROL        0x03u
#define SL_PARAM          0x04u /* Parameters: four dwords */
#define SL_READ_LENGTH    0x04u
#define SL_READ_BUFFER    0x08u /* BufferOffset, or CacheBuffer with SL_FSCACHE_REQUEST */
#define SL_READ_OFFSET    0x0Cu /* LARGE_INTEGER ByteOffset */
#define SL_IOCTL_OUT_LEN  0x04u
#define SL_IOCTL_IN_BUF   0x08u
#define SL_IOCTL_IN_LEN   0x0Cu
#define SL_IOCTL_CODE     0x10u
#define SL_DEVICE         0x14u
#define SL_FILE           0x18u
#define SL_COMPLETION     0x1Cu
#define SL_CONTEXT        0x20u
#define STACK_BYTES       0x24u

#define SL_FSCACHE_REQUEST  0x80u /* Flags: SL_READ_BUFFER is the buffer itself */
#define SL_PENDING_RETURNED 0x01u /* Control */
#define SL_INVOKE_ON_CANCEL 0x20u
#define SL_INVOKE_ON_SUCCESS 0x40u
#define SL_INVOKE_ON_ERROR  0x80u

/* DEVICE_OBJECT (DEVOBJ_BYTES; the extension follows it) */
#define DEV_TYPE        0x00u
#define DEV_SIZE        0x02u
#define DEV_REFCOUNT    0x04u
#define DEV_DRIVER      0x08u
#define DEV_MOUNTED     0x0Cu /* MountedOrSelfDevice */
#define DEV_CURRENT_IRP 0x10u
#define DEV_FLAGS       0x14u
#define DEV_EXTENSION   0x18u
#define DEV_DEVICE_TYPE 0x1Cu /* UCHAR */
#define DEV_STARTIO_FL  0x1Du
#define DEV_STACK_SIZE  0x1Eu /* CCHAR */
#define DEV_DELETE      0x1Fu
#define DEV_SECTOR_SIZE 0x20u
#define DEV_ALIGNMENT   0x24u
#define DEV_QUEUE       0x28u /* KDEVICE_QUEUE */
#define DEV_LOCK        0x34u /* KEVENT DeviceLock */
#define DEV_STARTIO_KEY 0x44u
#define DEVOBJ_BYTES    0x48u

/* Xbox device flags: XAPI's memory unit driver clears 0x10 when it has set
 * its device up (mu.obj MU_Init). */
#define DO_EXCLUSIVE           0x00000008u
#define DO_DEVICE_INITIALIZING 0x00000010u

#define FILE_DEVICE_CD_ROM       0x02u
#define FILE_DEVICE_DISK         0x07u
#define FILE_DEVICE_MEMORY_UNIT  0x3Au /* XAPI's \Device\MU_n */

/* DRIVER_OBJECT: DriverStartIo, DriverDeleteDevice, DriverDismountVolume,
 * then MajorFunction[IRP_MJ_COUNT] (mu.obj fills MU_DriverObject so). */
#define DRV_STARTIO     0x00u
#define DRV_DELETE      0x04u
#define DRV_DISMOUNT    0x08u
#define DRV_MAJOR       0x0Cu
#define DRVOBJ_BYTES    (DRV_MAJOR + 4u * IRP_MJ_COUNT)

/* KDEVICE_QUEUE { Type, Size, Busy, DeviceListHead }, KDEVICE_QUEUE_ENTRY
 * { DeviceListEntry, SortKey, Inserted } */
#define KDQ_BUSY        0x03u
#define KDQ_HEAD        0x04u
#define KDQE_LIST       0x00u
#define KDQE_KEY        0x08u
#define KDQE_INSERTED   0x0Cu
#define DEVICE_QUEUE_OBJECT 0x14

/* FILE_OBJECT: DeviceObject, FsContext, CompletionContext (a pool block
 * { Port, Key }: an I/O completion object and the file's key) */
#define FO_DEVICE      0x04u
#define FO_COMPLETION  0x24u

/* ------------------------------------------- LIST_ENTRY in guest memory */

static inline void nc_list_init(NcKernel *k, uint32_t head)
{
    nc_vm_wr32(k->vm, head, head);
    nc_vm_wr32(k->vm, head + 4, head);
}

static inline bool nc_list_empty(NcKernel *k, uint32_t head)
{
    const uint32_t flink = nc_vm_rd32(k->vm, head);
    return flink == head || flink == 0; /* 0: never initialized (a zeroed object) */
}

/* entry between prev and next */
static inline void nc_list_link(NcKernel *k, uint32_t prev, uint32_t entry, uint32_t next)
{
    nc_vm_wr32(k->vm, entry, next);
    nc_vm_wr32(k->vm, entry + 4, prev);
    nc_vm_wr32(k->vm, prev, entry);
    nc_vm_wr32(k->vm, next + 4, entry);
}

static inline void nc_list_insert_tail(NcKernel *k, uint32_t head, uint32_t entry)
{
    if (!nc_vm_rd32(k->vm, head)) {
        nc_list_init(k, head);
    }
    nc_list_link(k, nc_vm_rd32(k->vm, head + 4), entry, head);
}

static inline void nc_list_insert_head(NcKernel *k, uint32_t head, uint32_t entry)
{
    if (!nc_vm_rd32(k->vm, head)) {
        nc_list_init(k, head);
    }
    nc_list_link(k, head, entry, nc_vm_rd32(k->vm, head));
}

/* Unlinks entry (one pointing to itself, or never linked, stays as it is). */
static inline void nc_list_remove(NcKernel *k, uint32_t entry)
{
    const uint32_t flink = nc_vm_rd32(k->vm, entry), blink = nc_vm_rd32(k->vm, entry + 4);
    if (!flink || !blink || flink == entry) {
        return;
    }
    nc_vm_wr32(k->vm, blink, flink);
    nc_vm_wr32(k->vm, flink + 4, blink);
    nc_list_init(k, entry);
}

/* The first entry, unlinked, or 0 when the list is empty. */
static inline uint32_t nc_list_remove_head(NcKernel *k, uint32_t head)
{
    if (nc_list_empty(k, head)) {
        return 0;
    }
    const uint32_t entry = nc_vm_rd32(k->vm, head);
    nc_list_remove(k, entry);
    return entry;
}

/* ---------------------------------------------------------- functions */

/* io.c: a path canonical as the namespace keeps it (\ separators, . and ..
 * folded, \?? drive links resolved); an ANSI_STRING's text. */
bool nc_io_canon(const char *in, char *out, size_t cap);
void nc_io_ansi(NcKernel *k, uint32_t str, char *out, size_t cap);
/* io.c: the raw sectors of the hard disk's config area (partition 0) and
 * the heads of the cache partitions, as files on the host. */
void nc_io_raw(int part, uint64_t off, uint8_t *buf, uint32_t len, bool write);
/* io.c: a partition's offset and length on the 8 GB retail disk. */
bool nc_io_partition(int part, uint64_t *off, uint64_t *len);
/* io.c: IOCTL_SCSI_PASS_THROUGH_DIRECT to the DVD drive (its SCSI_PASS_THROUGH_DIRECT). */
void nc_io_scsi(NcKernel *k, uint32_t sptd, uint32_t len);
/* io.c: the file object's completion context's port and key (0: none). */
uint32_t nc_io_file_port(NcKernel *k, uint32_t file_object, uint32_t *key);
/* io.c: each symbolic link's name ("\??\D:"). */
void nc_io_links_each(void (*fn)(void *ctx, const char *name), void *ctx);
/* io.c: opens the symbolic link at a canonical path as NtOpenSymbolicLinkObject
 * does; the status. */
uint32_t nc_io_open_link(NcKernel *k, const char *name, uint32_t *handle);

/* irp.c */
void nc_irp_register(NcKernel *k);
/* IofCompleteRequest's work from C (a kernel driver ends its request): the
 * completion routines may run (guest code, k->redirected), then the
 * requester is told. The kernel call returns ret once that is done. */
void nc_irp_complete(NcKernel *k, NcFrame *f, uint32_t irp, uint32_t ret);
/* A new IRP from the pool for stack_size stack locations, or 0. */
uint32_t nc_irp_alloc(NcKernel *k, unsigned stack_size);
void nc_irp_free(NcKernel *k, uint32_t irp);

/* iodrv.c */
void nc_iodrv_register(NcKernel *k);
uint64_t nc_iodrv_dispatch(NcKernel *k, NcFrame *f, const uint32_t *a);
/* True if the routine is a kernel driver's (no guest code behind it). */
bool nc_iodrv_is_kernel_routine(uint32_t routine);
/* A device object of type, driver and extension size (zeroed), or 0. */
uint32_t nc_iodrv_device_new(NcKernel *k, uint32_t driver, uint32_t ext_size, uint32_t type);
/* The device object at a canonical path (a kernel one made on first use),
 * or 0. */
uint32_t nc_iodrv_device_named(NcKernel *k, const char *path);
/* The kernel's disc and hard disk devices: -1 the disc, 0-5 a partition,
 * -2 another. */
int nc_iodrv_device_unit(NcKernel *k, uint32_t device);
/* A path on a title driver's device (a memory unit): the status its open
 * ends with while no file system mounts on such a device (the USB storage
 * carve-out); 0 when the path is on no such device. */
uint32_t nc_iodrv_guest_volume(NcKernel *k, const char *path);

/* kqueue.c */
void nc_kqueue_register(NcKernel *k);
/* KDEVICE_QUEUE: true if the entry was queued (the device was busy), false
 * if the device is now busy with it; the next entry (by key: the first at
 * or past it), or 0 when none is left and the device is idle. */
void nc_kdq_init(NcKernel *k, uint32_t queue);
bool nc_kdq_insert(NcKernel *k, uint32_t queue, uint32_t entry, bool by_key, uint32_t key);
uint32_t nc_kdq_remove(NcKernel *k, uint32_t queue, bool by_key, uint32_t key);
/* A completion packet to an I/O completion object (KQUEUE). */
bool nc_kqueue_post(NcKernel *k, uint32_t port, uint32_t key, uint32_t apc_context, uint32_t status,
                    uint32_t information);

/* obname.c: names in the object namespace, kept in guest memory (a
 * savestate holds them). Paths are canonical ("\Device\MU_0",
 * "\Win32NamedObjects\x"). */
void nc_obname_register(NcKernel *k);
/* permanent: the name stays when the last handle closes (devices, the
 * kernel's directories, OBJ_PERMANENT) */
bool nc_obname_insert(NcKernel *k, const char *path, uint32_t object, bool permanent);
uint32_t nc_obname_find(NcKernel *k, const char *path);
/* The object lost its last handle (ob.c): its name goes unless permanent. */
void nc_obname_object_closed(NcKernel *k, uint32_t object);
void nc_obname_remove_object(NcKernel *k, uint32_t object);
/* OBJECT_ATTRIBUTES' name as a canonical path, with its RootDirectory
 * (ObDosDevicesDirectory, ObWin32NamedObjectsDirectory, a directory
 * handle); false when the root is no directory. */
bool nc_obname_path(NcKernel *k, uint32_t attrs, char *out, size_t cap);
/* Before a Create* with ObjectAttributes (0: none): 0 when the object is
 * to be made (named path afterwards with nc_obname_insert; "" unnamed);
 * 1 when the name is taken by an object to open instead (*existing, *st
 * NC_STATUS_OBJECT_NAME_EXISTS: OBJ_OPENIF and the same type); -1 when the
 * create fails with *st. */
int nc_obname_precreate(NcKernel *k, uint32_t attrs, enum NcObType type, char *path, size_t cap,
                        uint32_t *existing, uint32_t *st);
/* A Create*'s end, after nc_obname_precreate returned named (0 or 1): a
 * handle to obj written to out, the new object named path (when it is new
 * and named); the status (st). */
uint32_t nc_obname_created(NcKernel *k, uint32_t out, uint32_t obj, enum NcObType type, const char *path, int named,
                           uint32_t st);
/* ob.c: the type of an object's header (NC_OB_TYPES: none of the kernel's). */
enum NcObType nc_ob_type_of(NcKernel *k, uint32_t object);
/* ob.c: the type whose type object is at va (NC_OB_TYPES: none). */
enum NcObType nc_ob_type_at(NcKernel *k, uint32_t va);

#ifdef __cplusplus
}
#endif

#endif
