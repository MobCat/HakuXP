/*
 * hakuX native core -- device objects and the kernel's own drivers.
 *
 * The Xbox kernel drives the DVD drive (\Device\CdRom0) and the hard disk
 * (\Device\Harddisk0\Partition0-5) itself; a title's own drivers (XAPI's
 * memory unit driver, \Device\MU_n) create device objects with
 * IoCreateDevice and get their requests through IofCallDriver (irp.c).
 * Files on the kernel's devices are served by io.c without IRPs; IRPs sent
 * to the devices themselves come here: XOnline reads a raw disc sector so
 * for the Xbox Live logon's challenge (IoBuildSynchronousFsdRequest +
 * IofCallDriver on \Device\CdRom0, xonline.lib ip.obj), XAPI's memory unit
 * code asks a disk's geometry (IoSynchronousDeviceIoControlRequest).
 *
 * The kernel's driver objects are in the pool, their MajorFunction entries
 * all a stub (NC_KORDINAL_IO_DRIVER) that enters nc_iodrv_dispatch, which
 * reads or writes the device's sectors and completes the request at once.
 * A kernel device's extension holds { 'HXDV', unit } (unit -1 the disc,
 * 0-5 a partition). The driver objects' addresses are kept in the KPCR's
 * page (NC_KPCR_IODRV), guest memory a savestate holds.
 *
 * USB storage (the carve-out). A memory unit is a title's own device: XAPI's
 * driver (mu.obj, disk.obj) reaches the unit through its USB stack and the
 * emulated OHCI (native/dev), whatever stands behind that (an emulated unit,
 * or a real one passed through from the phone's USB OTG port later). Its
 * device objects and IRPs work here as on the console. What the kernel does
 * not do yet is mount FATX on such a device (the console's FATX reads the
 * unit's sectors through IRPs to that driver): an open of a path on one
 * ends in nc_iodrv_guest_volume as a volume nothing recognises, which
 * XMountMU reports as an unformatted unit. That function is the one place
 * a FATX-over-IRP mount goes.
 */
#include "native/kernel/iomgr.h"
#include "native/core/xdvdfs.h"

#include <stdio.h>
#include <strings.h>

#define KDEV_MAGIC     0x56445848u /* "HXDV" */
#define KDEV_EXT_BYTES 8u
#define DRV_CDROM      0u          /* NC_KPCR_IODRV + 4 * kind */
#define DRV_DISK       1u

#define IOCTL_DISK_GET_DRIVE_GEOMETRY   0x00070000u
#define IOCTL_DISK_GET_PARTITION_INFO   0x00074004u
#define IOCTL_DISK_VERIFY               0x00070014u
#define IOCTL_CDROM_GET_DRIVE_GEOMETRY  0x0002404Cu
#define IOCTL_CDROM_CHECK_VERIFY        0x00024800u
#define IOCTL_STORAGE_CHECK_VERIFY      0x002D4800u
#define IOCTL_SCSI_PASS_THROUGH_DIRECT  0x0004D014u

#define STATUS_MEDIA_WRITE_PROTECTED 0xC00000A2u

bool nc_iodrv_is_kernel_routine(uint32_t routine)
{
    return routine && routine == nc_kernel_stub_va(NC_KORDINAL_IO_DRIVER);
}

/* -------------------------------------------------------- device objects */

uint32_t nc_iodrv_device_new(NcKernel *k, uint32_t driver, uint32_t ext_size, uint32_t type)
{
    const uint32_t ext = nc_align_up(ext_size, 16);
    const uint32_t d = nc_ob_create(k, NC_OB_DEVICE, DEVOBJ_BYTES + ext);
    if (!d) {
        return 0;
    }
    uint8_t *p = nc_vm_host(k->vm, d);
    memset(p, 0, DEVOBJ_BYTES + ext);
    nc_wr16(p + DEV_TYPE, IO_TYPE_DEVICE);
    nc_wr16(p + DEV_SIZE, (uint16_t)(DEVOBJ_BYTES + ext));
    nc_vm_wr32(k->vm, d + DEV_DRIVER, driver);
    nc_vm_wr32(k->vm, d + DEV_MOUNTED, d);
    nc_vm_wr32(k->vm, d + DEV_FLAGS, DO_DEVICE_INITIALIZING);
    nc_vm_wr32(k->vm, d + DEV_EXTENSION, ext_size ? d + DEVOBJ_BYTES : 0);
    p[DEV_DEVICE_TYPE] = (uint8_t)type;
    p[DEV_STACK_SIZE] = 1;
    nc_kdq_init(k, d + DEV_QUEUE);
    nc_ke_init_event(k, d + DEV_LOCK, 1, true); /* a synchronization event, signaled */
    return d;
}

/* The kernel's driver object of a kind, made on first use. */
static uint32_t kernel_driver(NcKernel *k, unsigned kind)
{
    const uint32_t slot = k->kpcr_va + NC_KPCR_IODRV + 4 * kind;
    uint32_t drv = nc_vm_rd32(k->vm, slot);
    if (drv) {
        return drv;
    }
    drv = nc_pool_alloc(k, DRVOBJ_BYTES);
    if (!drv) {
        return 0;
    }
    memset(nc_vm_host(k->vm, drv), 0, DRVOBJ_BYTES);
    const uint32_t stub = nc_kernel_stub_va(NC_KORDINAL_IO_DRIVER);
    for (unsigned mj = 0; mj < IRP_MJ_COUNT; mj++) {
        nc_vm_wr32(k->vm, drv + DRV_MAJOR + 4 * mj, stub);
    }
    nc_vm_wr32(k->vm, slot, drv);
    return drv;
}

/* The kernel device a canonical path names: -1 the disc, 0-5 a partition,
 * -2 none. */
static int kernel_unit_of_path(const char *path)
{
    static const char hd[] = "\\Device\\Harddisk0\\Partition";
    if (!strcasecmp(path, "\\Device\\CdRom0")) {
        return -1;
    }
    if (!strncasecmp(path, hd, sizeof(hd) - 1) && path[sizeof(hd) - 1] >= '0' && path[sizeof(hd) - 1] <= '5' &&
        !path[sizeof(hd)]) {
        return path[sizeof(hd) - 1] - '0';
    }
    return -2;
}

uint32_t nc_iodrv_device_named(NcKernel *k, const char *path)
{
    uint32_t d = nc_obname_find(k, path);
    const int unit = kernel_unit_of_path(path);
    if (d || unit == -2) {
        return d;
    }
    const uint32_t drv = kernel_driver(k, unit < 0 ? DRV_CDROM : DRV_DISK);
    d = drv ? nc_iodrv_device_new(k, drv, KDEV_EXT_BYTES, unit < 0 ? FILE_DEVICE_CD_ROM : FILE_DEVICE_DISK) : 0;
    if (!d) {
        return 0;
    }
    const uint32_t ext = d + DEVOBJ_BYTES;
    nc_vm_wr32(k->vm, ext, KDEV_MAGIC);
    nc_vm_wr32(k->vm, ext + 4, (uint32_t)unit);
    nc_vm_wr32(k->vm, d + DEV_SECTOR_SIZE, unit < 0 ? 2048 : 512);
    nc_vm_wr32(k->vm, d + DEV_FLAGS, 0); /* set up */
    nc_obname_insert(k, path, d, true);
    return d;
}

int nc_iodrv_device_unit(NcKernel *k, uint32_t d)
{
    if (!d || nc_vm_phys(k->vm, d) == UINT32_MAX) {
        return -2;
    }
    const uint32_t drv = nc_vm_rd32(k->vm, d + DEV_DRIVER);
    const uint32_t ext = nc_vm_rd32(k->vm, d + DEV_EXTENSION);
    if (!drv || (drv != nc_vm_rd32(k->vm, k->kpcr_va + NC_KPCR_IODRV + 4 * DRV_CDROM) &&
                 drv != nc_vm_rd32(k->vm, k->kpcr_va + NC_KPCR_IODRV + 4 * DRV_DISK)) ||
        !ext || nc_vm_rd32(k->vm, ext) != KDEV_MAGIC) {
        return -2;
    }
    const int unit = (int32_t)nc_vm_rd32(k->vm, ext + 4);
    return unit >= -1 && unit <= 5 ? unit : -2;
}

uint32_t nc_iodrv_guest_volume(NcKernel *k, const char *path)
{
    char dev[256];
    snprintf(dev, sizeof(dev), "%s", path);
    /* the longest named device the path starts with: \Device\MU_0\x -> \Device\MU_0 */
    for (;;) {
        const uint32_t d = nc_obname_find(k, dev);
        if (d && nc_ob_type_of(k, d) == NC_OB_DEVICE && nc_iodrv_device_unit(k, d) == -2) {
            static unsigned told;
            if (told++ < 8) {
                nc_log("hakuX-native-io", "open %s: %s is a title driver's device (type %02x); its file system "
                       "isn't mounted (FATX over a title's driver, the USB storage carve-out)",
                       path, dev, *(const uint8_t *)nc_vm_host(k->vm, d + DEV_DEVICE_TYPE));
            }
            return NC_STATUS_UNRECOGNIZED_VOLUME;
        }
        char *cut = strrchr(dev, '\\');
        if (!cut || cut == dev) {
            return 0;
        }
        *cut = 0;
    }
}

/* IoCreateDevice(DriverObject, DeviceExtensionSize, DeviceName, DeviceType, Exclusive, DeviceObject) */
static uint64_t IoCreateDevice(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char name[128] = "", path[256] = "";
    if (a[2]) {
        nc_io_ansi(k, a[2], name, sizeof(name));
        if (!nc_io_canon(name, path, sizeof(path))) {
            return NC_STATUS_OBJECT_PATH_NOT_FOUND;
        }
        if (nc_obname_find(k, path)) {
            return NC_STATUS_OBJECT_NAME_COLLISION;
        }
    }
    const uint32_t d = nc_iodrv_device_new(k, a[0], a[1], a[3]);
    if (!d) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    if (a[4] & 0xFF) {
        nc_vm_wr32(k->vm, d + DEV_FLAGS, nc_vm_rd32(k->vm, d + DEV_FLAGS) | DO_EXCLUSIVE);
    }
    if (path[0] && !nc_obname_insert(k, path, d, true)) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, a[5], d);
    nc_log("hakuX-native-io", "device %s created: type %02x, driver %08x, extension %u", path[0] ? path : "(unnamed)",
           a[3], a[0], a[1]);
    return NC_STATUS_SUCCESS;
}

/* IoDeleteDevice(DeviceObject): its name goes; the object stays, delete pending */
static uint64_t IoDeleteDevice(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_obname_remove_object(k, a[0]);
    *((uint8_t *)nc_vm_host(k->vm, a[0]) + DEV_DELETE) = 1;
    return 0;
}

/* IoDismountVolume(DeviceObject): no file system keeps state of a volume here */
static uint64_t IoDismountVolume(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    return NC_STATUS_SUCCESS;
}

/* -------------------------------------------------------- the dispatch */

static bool mapped(NcKernel *k, uint32_t va, uint32_t len)
{
    for (uint32_t p = va & ~(NC_PAGE - 1); len && p < va + len; p += NC_PAGE) {
        if (nc_vm_phys(k->vm, p) == UINT32_MAX) {
            return false;
        }
        if (p + NC_PAGE < p) {
            break;
        }
    }
    return true;
}

/* A read or write of a kernel device's sectors. */
static uint32_t rw(NcKernel *k, int unit, bool write, uint32_t buf, uint32_t len, uint64_t off, uint32_t *info)
{
    *info = 0;
    if (!len) {
        return NC_STATUS_SUCCESS;
    }
    if (!buf || !mapped(k, buf, len)) {
        return NC_STATUS_INVALID_PARAMETER;
    }
    if (unit == -1) {
        if (write) {
            return STATUS_MEDIA_WRITE_PROTECTED;
        }
        if (k->disc->fd < 0) {
            return NC_STATUS_NO_MEDIA_IN_DEVICE;
        }
        if ((off | len) & (NC_XDVDFS_SECTOR - 1)) {
            return NC_STATUS_INVALID_PARAMETER;
        }
        /* the partition's sectors as the drive reads them, the mods' files aside */
        const NcDiscFile df = {.sector = (uint32_t)(off / NC_XDVDFS_SECTOR), .size = len};
        nc_vm_will_write(buf, len);
        if (!nc_disc_read_orig(k->disc, &df, 0, nc_vm_host(k->vm, buf), len)) {
            return NC_STATUS_DEVICE_DATA_ERROR;
        }
        *info = len;
        return NC_STATUS_SUCCESS;
    }
    uint64_t start, size;
    if (!nc_io_partition(unit, &start, &size) || off + len > size || ((off | len) & 511)) {
        return NC_STATUS_INVALID_PARAMETER;
    }
    if (unit == 1 || unit == 2) {
        /* The data and system drives are host folders: no FATX sectors to
         * give. Reads see zeros, writes go nowhere, told once. */
        static bool told;
        if (!told) {
            told = true;
            nc_log("hakuX-native-io", "raw %s of Partition%d +%llx (%u): its FATX sectors aren't kept (zeros)",
                   write ? "write" : "read", unit, (unsigned long long)off, len);
        }
        if (!write) {
            nc_vm_will_write(buf, len);
            memset(nc_vm_host(k->vm, buf), 0, len);
        }
    } else if (write) {
        nc_vm_will_read(buf, len);
        nc_io_raw(unit, off, nc_vm_host(k->vm, buf), len, true);
    } else {
        nc_vm_will_write(buf, len);
        nc_io_raw(unit, off, nc_vm_host(k->vm, buf), len, false);
    }
    *info = len;
    return NC_STATUS_SUCCESS;
}

static void put64(uint8_t *p, uint64_t v)
{
    nc_wr32(p, (uint32_t)v);
    nc_wr32(p + 4, (uint32_t)(v >> 32));
}

/* A device control on a kernel device: the output in out (out_len bytes). */
static uint32_t ioctl(NcKernel *k, int unit, uint32_t code, uint32_t in, uint32_t in_len, uint32_t out,
                      uint32_t out_len, uint32_t *info)
{
    *info = 0;
    uint8_t o[32] = {0};
    uint32_t n = 0;
    if (unit >= 0) {
        uint64_t start, size;
        nc_io_partition(unit, &start, &size);
        switch (code) {
        case IOCTL_DISK_GET_DRIVE_GEOMETRY: /* Cylinders, MediaType, Tracks, Sectors, BytesPerSector */
            put64(o, size / 512);
            nc_wr32(o + 8, 12); /* FixedMedia */
            nc_wr32(o + 12, 1);
            nc_wr32(o + 16, 1);
            nc_wr32(o + 20, 512);
            n = 24;
            break;
        case IOCTL_DISK_GET_PARTITION_INFO: /* StartingOffset, PartitionLength, HiddenSectors,
                                               PartitionNumber, Type, Boot, Recognized, Rewrite */
            put64(o, start);
            put64(o + 8, size);
            nc_wr32(o + 16, (uint32_t)(start / 512));
            nc_wr32(o + 20, (uint32_t)unit);
            o[26] = 1;
            n = 32;
            break;
        case IOCTL_DISK_VERIFY:
            return NC_STATUS_SUCCESS;
        default:
            return NC_STATUS_INVALID_DEVICE_REQUEST;
        }
    } else {
        switch (code) {
        case IOCTL_CDROM_GET_DRIVE_GEOMETRY: {
            const uint64_t bytes = k->disc->image_size > k->disc->part_off ? k->disc->image_size - k->disc->part_off
                                                                              : 0;
            put64(o, bytes / NC_XDVDFS_SECTOR);
            nc_wr32(o + 8, 11); /* RemovableMedia */
            nc_wr32(o + 12, 1);
            nc_wr32(o + 16, 1);
            nc_wr32(o + 20, NC_XDVDFS_SECTOR);
            n = 24;
            break;
        }
        case IOCTL_CDROM_CHECK_VERIFY:
        case IOCTL_STORAGE_CHECK_VERIFY:
            return k->disc->fd >= 0 || k->disc->ov ? NC_STATUS_SUCCESS : NC_STATUS_NO_MEDIA_IN_DEVICE;
        case IOCTL_SCSI_PASS_THROUGH_DIRECT:
            nc_io_scsi(k, in, in_len);
            return NC_STATUS_SUCCESS;
        default:
            return NC_STATUS_INVALID_DEVICE_REQUEST;
        }
    }
    if (out_len < n || !out || !mapped(k, out, n)) {
        return NC_STATUS_BUFFER_TOO_SMALL;
    }
    nc_vm_will_write(out, n);
    memcpy(nc_vm_host(k->vm, out), o, n);
    *info = n;
    return NC_STATUS_SUCCESS;
}

/* DriverObject->MajorFunction[n](DeviceObject, Irp) of a kernel driver: the
 * request is done in the call and completed (its completion routines may run
 * first, nc_irp_complete); the status. */
uint64_t nc_iodrv_dispatch(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const uint32_t dev = a[0], irp = a[1];
    const uint32_t sl = nc_vm_rd32(k->vm, irp + IRP_CURRENT_STACK);
    const uint8_t major = *(const uint8_t *)nc_vm_host(k->vm, sl + SL_MAJOR);
    const int unit = nc_iodrv_device_unit(k, dev);
    uint32_t st = NC_STATUS_INVALID_DEVICE_REQUEST, info = 0;
    if (unit == -2) {
        nc_log("hakuX-native-io", "kernel driver: request %02x for device %08x, not one of the kernel's", major, dev);
    } else if (major == IRP_MJ_READ || major == IRP_MJ_WRITE) {
        const uint8_t flags = *(const uint8_t *)nc_vm_host(k->vm, sl + SL_FLAGS);
        const uint32_t where = nc_vm_rd32(k->vm, sl + SL_READ_BUFFER);
        const uint32_t buf = flags & SL_FSCACHE_REQUEST ? where : nc_vm_rd32(k->vm, irp + IRP_USER_BUFFER) + where;
        const uint64_t off = nc_vm_rd32(k->vm, sl + SL_READ_OFFSET) |
                             ((uint64_t)nc_vm_rd32(k->vm, sl + SL_READ_OFFSET + 4) << 32);
        st = rw(k, unit, major == IRP_MJ_WRITE, buf, nc_vm_rd32(k->vm, sl + SL_READ_LENGTH), off, &info);
    } else if (major == IRP_MJ_DEVICE_CONTROL || major == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
        st = ioctl(k, unit, nc_vm_rd32(k->vm, sl + SL_IOCTL_CODE), nc_vm_rd32(k->vm, sl + SL_IOCTL_IN_BUF),
                   nc_vm_rd32(k->vm, sl + SL_IOCTL_IN_LEN), nc_vm_rd32(k->vm, irp + IRP_USER_BUFFER),
                   nc_vm_rd32(k->vm, sl + SL_IOCTL_OUT_LEN), &info);
    } else if (major == IRP_MJ_CREATE || major == IRP_MJ_CLOSE || major == IRP_MJ_CLEANUP ||
               major == IRP_MJ_FLUSH_BUFFERS || major == IRP_MJ_SHUTDOWN) {
        st = NC_STATUS_SUCCESS;
    }
    if (nc_io_trace() || (st >= 0xC0000000u && st != NC_STATUS_INVALID_DEVICE_REQUEST)) {
        nc_log("hakuX-native-io", "kernel driver: %s request %02x -> %08x (%u bytes)",
               unit == -1 ? "CdRom0" : unit >= 0 ? "Harddisk0" : "?", major, st, info);
    }
    nc_vm_wr32(k->vm, irp + IRP_STATUS, st);
    nc_vm_wr32(k->vm, irp + IRP_INFORMATION, info);
    nc_irp_complete(k, f, irp, st);
    return st;
}

void nc_iodrv_register(NcKernel *k)
{
    nc_kernel_impl(k, "IoCreateDevice", IoCreateDevice);
    nc_kernel_impl(k, "IoDeleteDevice", IoDeleteDevice);
    nc_kernel_impl(k, "IoDismountVolume", IoDismountVolume);
}
