/*
 * hakuX native core -- the object namespace: names of objects, object
 * directories, and the Ob exports that find or hand out objects.
 *
 * A name is a record in the kernel's pool, linked from a word in the KPCR's
 * page (NC_KPCR_OBNAMES): guest memory, so a savestate holds the names as
 * it holds the objects. Records: Next +0, Object +4, Flags +8 (bit 0
 * permanent), their bytes +0x0C, the canonical path +0x10 (NUL ended).
 * Names compare without case, as the Xbox's object manager compares them.
 *
 * What is named: devices (IoCreateDevice, the kernel's disc and hard disk),
 * XAPI's named events, semaphores, mutants and timers (CreateEvent("x")
 * passes RootDirectory ObWin32NamedObjectsDirectory and OBJ_OPENIF, so a
 * second CreateEvent of a name opens the first), I/O completion objects,
 * and directories. A name that isn't permanent goes when its object's last
 * handle closes (ob.c), as NT's temporary names do. The \?? drive letters
 * are io.c's symbolic links; they show in a listing of \?? here.
 */
#include "native/kernel/iomgr.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>

#define OB_DOS_DEVICES   0xFFFFFFFDu /* ObDosDevicesDirectory() */
#define OB_WIN32_OBJECTS 0xFFFFFFFCu /* ObWin32NamedObjectsDirectory() */
#define WIN32_DIR        "\\Win32NamedObjects"
#define OBJ_PERMANENT    0x10u
#define OBJ_OPENIF       0x80u

#define NR_NEXT      0x00u
#define NR_OBJECT    0x04u
#define NR_FLAGS     0x08u
#define NR_BYTES     0x0Cu
#define NR_NAME      0x10u
#define NR_PERMANENT 0x01u

#define PATH_MAX_LEN 255u
#define DIR_BODY     0x2Cu /* OBJECT_DIRECTORY: eleven hash buckets */
#define OB_HEADER    0x10u

/* The types' tags in a directory listing (OBJECT_DIRECTORY_INFORMATION.Type):
 * the pool tags the Xbox's type objects carry, as far as known. */
static uint32_t type_tag(enum NcObType t)
{
    switch (t) {
    case NC_OB_DIRECTORY: return 0x65726944u;   /* "Dire" */
    case NC_OB_SYMLINK: return 0x626D7953u;     /* "Symb" */
    case NC_OB_DEVICE: return 0x69766544u;      /* "Devi" */
    case NC_OB_EVENT: return 0x6E657645u;       /* "Even" */
    case NC_OB_SEMAPHORE: return 0x616D6553u;   /* "Sema" */
    case NC_OB_MUTANT: return 0x6174754Du;      /* "Muta" */
    case NC_OB_TIMER: return 0x656D6954u;       /* "Time" */
    case NC_OB_THREAD: return 0x65726854u;      /* "Thre" */
    case NC_OB_FILE: return 0x656C6946u;        /* "File" */
    case NC_OB_IOCOMPLETION: return 0x706D6F43u; /* "Comp" */
    default: return 0;
    }
}

static uint32_t head_va(NcKernel *k)
{
    return k->kpcr_va + NC_KPCR_OBNAMES;
}

static void rec_path(NcKernel *k, uint32_t r, char *out, size_t cap)
{
    size_t i = 0;
    for (; i + 1 < cap && i < PATH_MAX_LEN; i++) {
        const char c = (char)*(const uint8_t *)nc_vm_host(k->vm, r + NR_NAME + (uint32_t)i);
        if (!c) {
            break;
        }
        out[i] = c;
    }
    out[i] = 0;
}

/* '/' as '\', no doubled or trailing separators, one leading separator. */
static void fold(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    out[n++] = '\\';
    for (const char *p = in; *p && n + 1 < cap; p++) {
        const char c = *p == '/' ? '\\' : *p;
        if (c == '\\' && out[n - 1] == '\\') {
            continue;
        }
        out[n++] = c;
    }
    if (n > 1 && out[n - 1] == '\\') {
        n--;
    }
    out[n] = 0;
}

uint32_t nc_obname_find(NcKernel *k, const char *path)
{
    char name[PATH_MAX_LEN + 1];
    for (uint32_t r = nc_vm_rd32(k->vm, head_va(k)); r; r = nc_vm_rd32(k->vm, r + NR_NEXT)) {
        rec_path(k, r, name, sizeof(name));
        if (!strcasecmp(name, path)) {
            return nc_vm_rd32(k->vm, r + NR_OBJECT);
        }
    }
    return 0;
}

/* The path an object is named by (the first record), false if none. */
static bool path_of(NcKernel *k, uint32_t object, char *out, size_t cap)
{
    for (uint32_t r = nc_vm_rd32(k->vm, head_va(k)); r; r = nc_vm_rd32(k->vm, r + NR_NEXT)) {
        if (nc_vm_rd32(k->vm, r + NR_OBJECT) == object) {
            rec_path(k, r, out, cap);
            return true;
        }
    }
    return false;
}

bool nc_obname_insert(NcKernel *k, const char *path, uint32_t object, bool permanent)
{
    const size_t len = strlen(path);
    if (!len || len > PATH_MAX_LEN || nc_obname_find(k, path)) {
        return false;
    }
    const uint32_t bytes = nc_align_up((uint32_t)(NR_NAME + len + 1), 8);
    const uint32_t r = nc_pool_alloc(k, bytes);
    if (!r) {
        nc_log("hakuX-native", "ob: no pool for the name %s", path);
        return false;
    }
    nc_vm_wr32(k->vm, r + NR_OBJECT, object);
    nc_vm_wr32(k->vm, r + NR_FLAGS, permanent ? NR_PERMANENT : 0);
    nc_vm_wr32(k->vm, r + NR_BYTES, bytes);
    memcpy(nc_vm_host(k->vm, r + NR_NAME), path, len + 1);
    nc_vm_wr32(k->vm, r + NR_NEXT, nc_vm_rd32(k->vm, head_va(k)));
    nc_vm_wr32(k->vm, head_va(k), r);
    return true;
}

/* Drops the object's names (all, or the temporary ones). */
static void remove_names(NcKernel *k, uint32_t object, bool all)
{
    uint32_t link = head_va(k);
    for (uint32_t r = nc_vm_rd32(k->vm, link); r; r = nc_vm_rd32(k->vm, link)) {
        if (nc_vm_rd32(k->vm, r + NR_OBJECT) == object && (all || !(nc_vm_rd32(k->vm, r + NR_FLAGS) & NR_PERMANENT))) {
            nc_vm_wr32(k->vm, link, nc_vm_rd32(k->vm, r + NR_NEXT));
            nc_pool_free(k, r, nc_vm_rd32(k->vm, r + NR_BYTES));
            continue;
        }
        link = r + NR_NEXT;
    }
}

void nc_obname_object_closed(NcKernel *k, uint32_t object)
{
    remove_names(k, object, false);
}

void nc_obname_remove_object(NcKernel *k, uint32_t object)
{
    remove_names(k, object, true);
}

bool nc_obname_path(NcKernel *k, uint32_t attrs, char *out, size_t cap)
{
    char name[PATH_MAX_LEN + 1], joined[2 * PATH_MAX_LEN + 4], dir[PATH_MAX_LEN + 1];
    nc_io_ansi(k, nc_vm_rd32(k->vm, attrs + 4), name, sizeof(name));
    const uint32_t root = nc_vm_rd32(k->vm, attrs);
    if (root == OB_WIN32_OBJECTS) {
        snprintf(joined, sizeof(joined), "%s\\%s", WIN32_DIR, name);
    } else if (root == OB_DOS_DEVICES) {
        snprintf(joined, sizeof(joined), "\\??\\%s", name);
    } else if (root) {
        enum NcObType type;
        const uint32_t d = nc_ob_lookup(k, root, &type);
        if (!d || type != NC_OB_DIRECTORY || !path_of(k, d, dir, sizeof(dir))) {
            return false;
        }
        snprintf(joined, sizeof(joined), "%s\\%s", dir, name);
    } else {
        snprintf(joined, sizeof(joined), "%s", name);
    }
    fold(joined, out, cap);
    return true;
}

int nc_obname_precreate(NcKernel *k, uint32_t attrs, enum NcObType type, char *path, size_t cap,
                        uint32_t *existing, uint32_t *st)
{
    path[0] = 0;
    *existing = 0;
    *st = NC_STATUS_SUCCESS;
    const uint32_t str = attrs ? nc_vm_rd32(k->vm, attrs + 4) : 0;
    if (!str || !*(const uint16_t *)nc_vm_host(k->vm, str)) {
        return 0; /* unnamed */
    }
    if (!nc_obname_path(k, attrs, path, cap)) {
        *st = NC_STATUS_INVALID_HANDLE;
        return -1;
    }
    const uint32_t obj = nc_obname_find(k, path);
    if (!obj) {
        return 0;
    }
    if (!(nc_vm_rd32(k->vm, attrs + 8) & OBJ_OPENIF)) {
        *st = NC_STATUS_OBJECT_NAME_COLLISION;
        return -1;
    }
    if (nc_ob_type_of(k, obj) != type) {
        *st = NC_STATUS_OBJECT_TYPE_MISMATCH;
        return -1;
    }
    *existing = obj;
    *st = NC_STATUS_OBJECT_NAME_EXISTS;
    return 1;
}

uint32_t nc_obname_created(NcKernel *k, uint32_t out, uint32_t obj, enum NcObType type, const char *path, int named,
                           uint32_t st)
{
    const uint32_t h = nc_ob_handle(k, obj, type);
    if (!h) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!named && path[0] && !nc_obname_insert(k, path, obj, false)) {
        nc_log("hakuX-native", "ob: %s couldn't be named", path);
    }
    nc_vm_wr32(k->vm, out, h);
    return st;
}

/* ---------------------------------------------------------- directories */

typedef struct DirList {
    char name[32][64];
    uint32_t tag[32];
    unsigned n;
} DirList;

static void dir_add(DirList *l, const char *name, uint32_t tag)
{
    for (unsigned i = 0; i < l->n; i++) {
        if (!strcasecmp(l->name[i], name)) {
            return;
        }
    }
    if (l->n < 32 && strlen(name) < sizeof(l->name[0])) {
        snprintf(l->name[l->n], sizeof(l->name[0]), "%s", name);
        l->tag[l->n++] = tag;
    }
}

/* The parent of path ("\" for a top-level name) and its last component. */
static const char *split(const char *path, char *parent, size_t cap)
{
    const char *last = strrchr(path, '\\');
    const size_t at = last ? (size_t)(last - path) : 0;
    snprintf(parent, cap, "%.*s", (int)(at ? at : 1), at ? path : "\\");
    return last ? last + 1 : path;
}

typedef struct LinkCtx {
    DirList *l;
} LinkCtx;

static void link_entry(void *ctx, const char *name)
{
    char parent[PATH_MAX_LEN + 1];
    const char *base = split(name, parent, sizeof(parent));
    if (!strcasecmp(parent, "\\??")) {
        dir_add(((LinkCtx *)ctx)->l, base, type_tag(NC_OB_SYMLINK));
    }
}

static void dir_list(NcKernel *k, const char *dir, DirList *l)
{
    char name[PATH_MAX_LEN + 1], parent[PATH_MAX_LEN + 1];
    l->n = 0;
    if (!strcmp(dir, "\\")) {
        dir_add(l, "??", type_tag(NC_OB_DIRECTORY));
        dir_add(l, "Device", type_tag(NC_OB_DIRECTORY));
        dir_add(l, "Win32NamedObjects", type_tag(NC_OB_DIRECTORY));
    } else if (!strcasecmp(dir, "\\Device")) {
        dir_add(l, "CdRom0", type_tag(NC_OB_DEVICE));
        dir_add(l, "Harddisk0", type_tag(NC_OB_DIRECTORY));
    } else if (!strcasecmp(dir, "\\Device\\Harddisk0")) {
        for (int p = 0; p <= 5; p++) {
            snprintf(name, sizeof(name), "Partition%d", p);
            dir_add(l, name, type_tag(NC_OB_DEVICE));
        }
    } else if (!strcasecmp(dir, "\\??")) {
        LinkCtx c = {l};
        nc_io_links_each(link_entry, &c);
    }
    for (uint32_t r = nc_vm_rd32(k->vm, head_va(k)); r; r = nc_vm_rd32(k->vm, r + NR_NEXT)) {
        rec_path(k, r, name, sizeof(name));
        const char *base = split(name, parent, sizeof(parent));
        if (*base && !strcasecmp(parent, dir)) {
            dir_add(l, base, type_tag(nc_ob_type_of(k, nc_vm_rd32(k->vm, r + NR_OBJECT))));
        }
    }
}

/* A directory at path: the kernel's own, one a name was made in, or one
 * NtCreateDirectoryObject made. */
static bool is_dir_path(NcKernel *k, const char *path)
{
    static const char *const known[] = {"\\", "\\??", "\\Device", "\\Device\\Harddisk0", WIN32_DIR};
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (!strcasecmp(path, known[i])) {
            return true;
        }
    }
    char name[PATH_MAX_LEN + 1];
    const size_t len = strlen(path);
    for (uint32_t r = nc_vm_rd32(k->vm, head_va(k)); r; r = nc_vm_rd32(k->vm, r + NR_NEXT)) {
        rec_path(k, r, name, sizeof(name));
        if (!strncasecmp(name, path, len) && name[len] == '\\') {
            return true;
        }
    }
    return false;
}

static uint32_t dir_object_new(NcKernel *k, const char *path, bool permanent)
{
    const uint32_t d = nc_ob_create(k, NC_OB_DIRECTORY, DIR_BODY);
    if (d && !nc_obname_insert(k, path, d, permanent)) {
        nc_ob_dereference(k, d);
        return 0;
    }
    return d;
}

/* NtOpenDirectoryObject(DirectoryHandle, ObjectAttributes) */
static uint64_t NtOpenDirectoryObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char path[PATH_MAX_LEN + 1];
    if (!nc_obname_path(k, a[1], path, sizeof(path))) {
        return NC_STATUS_INVALID_HANDLE;
    }
    uint32_t d = nc_obname_find(k, path);
    if (d && nc_ob_type_of(k, d) != NC_OB_DIRECTORY) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (!d) {
        if (!is_dir_path(k, path)) {
            return NC_STATUS_OBJECT_NAME_NOT_FOUND;
        }
        d = dir_object_new(k, path, true);
        if (!d) {
            return NC_STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    const uint32_t h = nc_ob_handle(k, d, NC_OB_DIRECTORY);
    if (!h) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, a[0], h);
    return NC_STATUS_SUCCESS;
}

/* NtCreateDirectoryObject(DirectoryHandle, ObjectAttributes) */
static uint64_t NtCreateDirectoryObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char path[PATH_MAX_LEN + 1];
    uint32_t existing, st;
    const int named = nc_obname_precreate(k, a[1], NC_OB_DIRECTORY, path, sizeof(path), &existing, &st);
    if (named < 0) {
        return st;
    }
    uint32_t d = existing;
    if (!d) {
        if (path[0]) {
            d = dir_object_new(k, path, a[1] && (nc_vm_rd32(k->vm, a[1] + 8) & OBJ_PERMANENT));
        } else {
            d = nc_ob_create(k, NC_OB_DIRECTORY, DIR_BODY);
        }
        if (!d) {
            return NC_STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    const uint32_t h = nc_ob_handle(k, d, NC_OB_DIRECTORY);
    if (!h) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, a[0], h);
    return st;
}

/* NtQueryDirectoryObject(DirectoryHandle, Buffer, Length, RestartScan, Context, ReturnLength):
 * one entry a call, OBJECT_DIRECTORY_INFORMATION { OBJECT_STRING Name; ULONG Type } and
 * the name (NUL ended) after it; *Context the next entry's index. */
static uint64_t NtQueryDirectoryObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    enum NcObType type;
    const uint32_t d = nc_ob_lookup(k, a[0], &type);
    char path[PATH_MAX_LEN + 1];
    if (!d || type != NC_OB_DIRECTORY) {
        return NC_STATUS_INVALID_HANDLE;
    }
    if (!path_of(k, d, path, sizeof(path))) {
        snprintf(path, sizeof(path), "\\"); /* unnamed: as empty as a new directory is */
    }
    static DirList l; /* the guest CPU's thread only */
    dir_list(k, path, &l);
    const uint32_t idx = (a[3] & 0xFF) || !a[4] ? 0 : nc_vm_rd32(k->vm, a[4]);
    if (idx >= l.n) {
        return NC_STATUS_NO_MORE_ENTRIES;
    }
    const uint32_t len = (uint32_t)strlen(l.name[idx]), need = 12 + len + 1;
    if (a[5]) {
        nc_vm_wr32(k->vm, a[5], need);
    }
    if (a[2] < need) {
        return NC_STATUS_BUFFER_TOO_SMALL;
    }
    uint8_t *o = nc_vm_host(k->vm, a[1]);
    nc_wr16(o, (uint16_t)len);
    nc_wr16(o + 2, (uint16_t)(len + 1));
    nc_wr32(o + 4, a[1] + 12);
    nc_wr32(o + 8, l.tag[idx]);
    memcpy(o + 12, l.name[idx], len + 1);
    if (a[4]) {
        nc_vm_wr32(k->vm, a[4], idx + 1);
    }
    return NC_STATUS_SUCCESS;
}

/* ------------------------------------------------------------ Ob exports */

/* ObjectType (a type object, NULL: any) fits the object. */
static bool type_fits(NcKernel *k, uint32_t object, uint32_t type_va)
{
    return !type_va || nc_vm_rd32(k->vm, object - OB_HEADER + 0x8) == type_va;
}

/* The object a canonical path names: in the namespace, then (drive links
 * followed) a kernel device made on first use. */
static uint32_t find_any(NcKernel *k, const char *path)
{
    uint32_t obj = nc_obname_find(k, path);
    char resolved[512];
    if (!obj && nc_io_canon(path, resolved, sizeof(resolved))) {
        obj = nc_obname_find(k, resolved);
        if (!obj) {
            obj = nc_iodrv_device_named(k, resolved);
        }
    }
    return obj;
}

/* ObReferenceObjectByName(ObjectName, Attributes, ObjectType, ParseContext, Object) */
static uint64_t ObReferenceObjectByName(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char name[PATH_MAX_LEN + 1], path[PATH_MAX_LEN + 1];
    nc_io_ansi(k, a[0], name, sizeof(name));
    fold(name, path, sizeof(path));
    const uint32_t obj = find_any(k, path);
    if (!obj) {
        nc_log("hakuX-native-io", "reference by name %s: not found", name);
        return NC_STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!type_fits(k, obj, a[2])) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    nc_ob_reference(k, obj, 1);
    nc_vm_wr32(k->vm, a[4], obj);
    return NC_STATUS_SUCCESS;
}

/* ObOpenObjectByName(ObjectAttributes, ObjectType, ParseContext, Handle): XAPI's
 * OpenEvent, OpenSemaphore, OpenMutex, OpenWaitableTimer. */
static uint64_t ObOpenObjectByName(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    char path[PATH_MAX_LEN + 1];
    if (!nc_obname_path(k, a[0], path, sizeof(path))) {
        return NC_STATUS_INVALID_HANDLE;
    }
    uint32_t obj = find_any(k, path);
    if (!obj) {
        if (!a[1] || nc_ob_type_at(k, a[1]) == NC_OB_SYMLINK) {
            uint32_t h = 0;
            const uint32_t st = nc_io_open_link(k, path, &h);
            if (st == NC_STATUS_SUCCESS) {
                nc_vm_wr32(k->vm, a[3], h);
                return st;
            }
        }
        if (nc_io_trace()) {
            nc_log("hakuX-native-io", "open by name %s: not found", path);
        }
        return NC_STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!type_fits(k, obj, a[1])) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    const enum NcObType type = nc_ob_type_of(k, obj);
    const uint32_t h = type < NC_OB_TYPES ? nc_ob_handle(k, obj, type) : 0;
    if (!h) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, a[3], h);
    return NC_STATUS_SUCCESS;
}

/* ObOpenObjectByPointer(Object, ObjectType, Handle) */
static uint64_t ObOpenObjectByPointer(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    if (!type_fits(k, a[0], a[1])) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    const enum NcObType type = nc_ob_type_of(k, a[0]);
    const uint32_t h = type < NC_OB_TYPES ? nc_ob_handle(k, a[0], type) : 0;
    if (!h) {
        return type < NC_OB_TYPES ? NC_STATUS_INSUFFICIENT_RESOURCES : NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    nc_vm_wr32(k->vm, a[2], h);
    return NC_STATUS_SUCCESS;
}

/* ObReferenceObjectByPointer(Object, ObjectType) */
static uint64_t ObReferenceObjectByPointer(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    if (!type_fits(k, a[0], a[1])) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    nc_ob_reference(k, a[0], 1);
    return NC_STATUS_SUCCESS;
}

/* ObfReferenceObject(Object), fastcall: the new count */
static uint64_t ObfReferenceObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    nc_ob_reference(k, a[0], 1);
    return nc_vm_rd32(k->vm, a[0] - OB_HEADER);
}

/* ObCreateObject(ObjectType, ObjectAttributes, ObjectBodySize, Object): an object
 * of one of the kernel's types. A file object isn't made so: this kernel's
 * carry its own fields past the Xbox's (io.c). */
static uint64_t ObCreateObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const enum NcObType type = nc_ob_type_at(k, a[0]);
    if (type >= NC_OB_TYPES || type == NC_OB_FILE || type == NC_OB_SYMLINK) {
        nc_log("hakuX-native", "ob: ObCreateObject of type %08x (%u bytes): not a type made so here", a[0], a[2]);
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    const uint32_t obj = nc_ob_create(k, type, a[2]);
    if (!obj) {
        return NC_STATUS_INSUFFICIENT_RESOURCES;
    }
    nc_vm_wr32(k->vm, a[3], obj);
    return NC_STATUS_SUCCESS;
}

/* ObInsertObject(Object, ObjectAttributes, ObjectPointerBias, Handle): its name, a handle */
static uint64_t ObInsertObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    const enum NcObType type = nc_ob_type_of(k, a[0]);
    if (type >= NC_OB_TYPES) {
        return NC_STATUS_OBJECT_TYPE_MISMATCH;
    }
    char path[PATH_MAX_LEN + 1];
    uint32_t existing, st;
    const int named = nc_obname_precreate(k, a[1], type, path, sizeof(path), &existing, &st);
    if (named < 0) {
        return st;
    }
    const uint32_t obj = named ? existing : a[0];
    if (!named && path[0]) {
        nc_obname_insert(k, path, obj, a[1] && (nc_vm_rd32(k->vm, a[1] + 8) & OBJ_PERMANENT));
    }
    nc_ob_reference(k, obj, a[2]);
    if (a[3]) {
        const uint32_t h = nc_ob_handle(k, obj, type);
        if (!h) {
            return NC_STATUS_INSUFFICIENT_RESOURCES;
        }
        nc_vm_wr32(k->vm, a[3], h);
    }
    return st;
}

/* ObMakeTemporaryObject(Object): its names go with its last handle (now, if none) */
static uint64_t ObMakeTemporaryObject(NcKernel *k, NcFrame *f, const uint32_t *a)
{
    for (uint32_t r = nc_vm_rd32(k->vm, head_va(k)); r; r = nc_vm_rd32(k->vm, r + NR_NEXT)) {
        if (nc_vm_rd32(k->vm, r + NR_OBJECT) == a[0]) {
            nc_vm_wr32(k->vm, r + NR_FLAGS, nc_vm_rd32(k->vm, r + NR_FLAGS) & ~NR_PERMANENT);
        }
    }
    if (!nc_vm_rd32(k->vm, a[0] - OB_HEADER + 0x4)) {
        remove_names(k, a[0], false);
    }
    return 0;
}

void nc_obname_register(NcKernel *k)
{
    nc_kernel_impl(k, "NtOpenDirectoryObject", NtOpenDirectoryObject);
    nc_kernel_impl(k, "NtCreateDirectoryObject", NtCreateDirectoryObject);
    nc_kernel_impl(k, "NtQueryDirectoryObject", NtQueryDirectoryObject);
    nc_kernel_impl(k, "ObReferenceObjectByName", ObReferenceObjectByName);
    nc_kernel_impl(k, "ObOpenObjectByName", ObOpenObjectByName);
    nc_kernel_impl(k, "ObOpenObjectByPointer", ObOpenObjectByPointer);
    nc_kernel_impl(k, "ObReferenceObjectByPointer", ObReferenceObjectByPointer);
    nc_kernel_impl(k, "ObfReferenceObject", ObfReferenceObject);
    nc_kernel_impl(k, "ObCreateObject", ObCreateObject);
    nc_kernel_impl(k, "ObInsertObject", ObInsertObject);
    nc_kernel_impl(k, "ObMakeTemporaryObject", ObMakeTemporaryObject);
}
