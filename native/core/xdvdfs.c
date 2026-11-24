/*
 * hakuX native core — XDVDFS reader.
 *
 * Volume descriptor at sector 32 of the game partition: "MICROSOFT*XBOX*MEDIA",
 * root directory sector and size. A directory is a binary tree of entries:
 *   u16 left, u16 right   (child offsets in dwords; 0 = none: the root is
 *                          nobody's child; 0xFFFF pads a sector's end)
 *   u32 sector, u32 size
 *   u8 attributes, u8 name length, name
 */
#include "native/core/xdvdfs.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char k_magic[] = "MICROSOFT*XBOX*MEDIA";

/* Game partition offsets: extracted XISO, then full XGD1/XGD2/XGD3 dumps. */
static const uint64_t k_part_offsets[] = {0, 0x18300000ull, 0xFD90000ull, 0x2080000ull};

static bool pread_all(int fd, void *buf, size_t len, uint64_t off)
{
    uint8_t *p = buf;
    while (len) {
        ssize_t n = pread(fd, p, len, (off_t)off);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            errno = EIO; /* unexpected EOF: never report errno 0 ("Success") */
            return false;
        }
        p += n;
        len -= (size_t)n;
        off += (uint64_t)n;
    }
    return true;
}

static void note_truncated(const NcDisc *d, uint64_t need)
{
    NcDisc *m = (NcDisc *)d;
    const bool first = !__atomic_exchange_n(&m->truncated, true, __ATOMIC_RELAXED);
    uint64_t prev = __atomic_load_n(&m->truncated_need, __ATOMIC_RELAXED);
    while (need > prev &&
           !__atomic_compare_exchange_n(&m->truncated_need, &prev, need, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
    if (first) {
        if (d->image_size) {
            nc_log("hakuX-native", "disc: read past end of image (need %.1f MiB, file is %.1f MiB)",
                   (double)need / (1024.0 * 1024.0), (double)d->image_size / (1024.0 * 1024.0));
        } else {
            nc_log("hakuX-native", "disc: read past end of image at offset 0x%llx", (unsigned long long)need);
        }
    }
}

/* In-memory cache of read-only XDVDFS directory tables. Avoids synchronous
 * pread() syscalls on the guest CPU thread when streaming titles reopen files
 * under nested directories on SD cards. */
enum {
    DIR_CACHE_MAX = 256,
    DIR_CACHE_BYTES = 4u << 20,
};

typedef struct DirCacheEntry {
    uint32_t sector;
    uint32_t size;
    uint8_t *data;
} DirCacheEntry;

struct NcDiscDirCache {
    pthread_mutex_t mu;
    DirCacheEntry e[DIR_CACHE_MAX];
    unsigned n;
    size_t bytes;
};

static NcDiscDirCache *dir_cache_open(void)
{
    NcDiscDirCache *c = calloc(1, sizeof(*c));
    if (c && pthread_mutex_init(&c->mu, NULL) != 0) {
        free(c);
        return NULL;
    }
    return c;
}

static void dir_cache_close(NcDisc *d)
{
    NcDiscDirCache *c = d->dir_cache;
    if (!c) {
        return;
    }
    for (unsigned i = 0; i < c->n; i++) {
        free(c->e[i].data);
    }
    pthread_mutex_destroy(&c->mu);
    free(c);
    d->dir_cache = NULL;
}

/* Returns a directory table's bytes from cache or disc. When *owned is true
 * the caller must free() the returned buffer; cached buffers stay valid until
 * nc_disc_close(). */
static const uint8_t *dir_read(const NcDisc *d, uint32_t sector, uint32_t size, bool *owned)
{
    *owned = false;
    NcDiscDirCache *c = d->dir_cache;
    if (c) {
        pthread_mutex_lock(&c->mu);
        for (unsigned i = 0; i < c->n; i++) {
            if (c->e[i].sector == sector && c->e[i].size == size) {
                const uint8_t *hit = c->e[i].data;
                pthread_mutex_unlock(&c->mu);
                return hit;
            }
        }
        pthread_mutex_unlock(&c->mu);
    }
    uint8_t *buf = malloc(size);
    if (!buf) {
        return NULL;
    }
    const uint64_t dir_off = d->part_off + (uint64_t)sector * NC_XDVDFS_SECTOR;
    if (!pread_all(d->fd, buf, size, dir_off)) {
        if (errno == EIO || (d->image_size && dir_off + size > d->image_size)) {
            note_truncated(d, dir_off + size);
        }
        free(buf);
        return NULL;
    }
    if (c) {
        pthread_mutex_lock(&c->mu);
        for (unsigned i = 0; i < c->n; i++) {
            if (c->e[i].sector == sector && c->e[i].size == size) {
                const uint8_t *hit = c->e[i].data;
                pthread_mutex_unlock(&c->mu);
                free(buf);
                return hit;
            }
        }
        if (c->n < DIR_CACHE_MAX && c->bytes + size <= DIR_CACHE_BYTES) {
            c->e[c->n++] = (DirCacheEntry){sector, size, buf};
            c->bytes += size;
            pthread_mutex_unlock(&c->mu);
            return buf;
        }
        pthread_mutex_unlock(&c->mu);
    }
    *owned = true;
    return buf;
}

bool nc_disc_open(NcDisc *d, int fd)
{
    memset(d, 0, sizeof(*d));
    d->fd = fd;
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        d->image_size = (uint64_t)st.st_size;
    }
    for (size_t i = 0; i < sizeof(k_part_offsets) / sizeof(k_part_offsets[0]); i++) {
        uint8_t vd[NC_XDVDFS_SECTOR];
        uint64_t off = k_part_offsets[i];
        if (!pread_all(fd, vd, sizeof(vd), off + 32ull * NC_XDVDFS_SECTOR)) {
            continue;
        }
        if (memcmp(vd, k_magic, 20) == 0 && memcmp(vd + 0x7EC, k_magic, 20) == 0) {
            d->part_off = off;
            d->root_sector = nc_rd32(vd + 20);
            d->root_size = nc_rd32(vd + 24);
            d->dir_cache = dir_cache_open();
            nc_log("hakuX-native", "disc: XDVDFS at +0x%llx, root sector %u (%u bytes), image %.1f MiB",
                   (unsigned long long)off, d->root_sector, d->root_size,
                   (double)d->image_size / (1024.0 * 1024.0));
            return true;
        }
    }
    nc_log("hakuX-native", "disc: no XDVDFS volume found");
    return false;
}

typedef struct CheckDir {
    uint32_t sector;
    uint32_t size;
} CheckDir;

typedef struct CheckCtx {
    NcDisc *d;
    CheckDir *q;
    unsigned qlen;
    unsigned qcap;
    const uint8_t *dir;
    uint32_t dir_size;
    uint8_t *visited;
} CheckCtx;

static void check_entry(CheckCtx *c, uint32_t off, unsigned depth)
{
    if (off + 14 > c->dir_size || depth > 64) {
        return;
    }
    const uint32_t slot = off >> 2;
    if (c->visited[slot >> 3] & (1u << (slot & 7))) {
        return;
    }
    c->visited[slot >> 3] |= (uint8_t)(1u << (slot & 7));

    const uint8_t *e = c->dir + off;
    const uint16_t left = nc_rd16(e), right = nc_rd16(e + 2);
    if (left == 0xFFFF && right == 0xFFFF && e[12] == 0xFF) {
        return;
    }
    const uint32_t fsec = nc_rd32(e + 4);
    const uint32_t fsize = nc_rd32(e + 8);
    const uint8_t attr = e[12];
    if (fsize > 0) {
        const uint64_t end = c->d->part_off + (uint64_t)fsec * NC_XDVDFS_SECTOR + fsize;
        if (end > c->d->image_size) {
            c->d->truncated = true;
            if (end > c->d->truncated_need) {
                c->d->truncated_need = end;
            }
        } else if ((attr & 0x10) && c->qlen < c->qcap) {
            c->q[c->qlen++] = (CheckDir){fsec, fsize};
        }
    }
    if (left && left != 0xFFFF) {
        check_entry(c, left * 4u, depth + 1);
    }
    if (right && right != 0xFFFF) {
        check_entry(c, right * 4u, depth + 1);
    }
}

bool nc_disc_check_intact(NcDisc *d)
{
    if (!d || d->fd < 0 || !d->image_size || !d->root_size) {
        return true;
    }
    const uint64_t root_end = d->part_off + (uint64_t)d->root_sector * NC_XDVDFS_SECTOR + d->root_size;
    if (root_end > d->image_size) {
        d->truncated = true;
        d->truncated_need = root_end;
        nc_log("hakuX-native", "disc: image is truncated (%.1f MiB, needs at least %.1f MiB)",
               (double)d->image_size / (1024.0 * 1024.0), (double)d->truncated_need / (1024.0 * 1024.0));
        return false;
    }
    /* Walk up to 256 directories breadth-first (~10 ms on SD card) to catch
     * truncated downloads or interrupted copies before the title boots. */
    enum { MAX_DIRS = 256 };
    CheckDir q[MAX_DIRS];
    q[0] = (CheckDir){d->root_sector, d->root_size};
    CheckCtx ctx = {.d = d, .q = q, .qlen = 1, .qcap = MAX_DIRS};
    for (unsigned i = 0; i < ctx.qlen; i++) {
        const uint32_t sz = q[i].size;
        if (!sz || sz > (1u << 20)) {
            continue;
        }
        const size_t vbytes = ((sz >> 2) + 7) >> 3;
        uint8_t *vis = calloc(vbytes ? vbytes : 1, 1);
        if (!vis) {
            break;
        }
        const uint64_t off = d->part_off + (uint64_t)q[i].sector * NC_XDVDFS_SECTOR;
        bool owned = false;
        const uint8_t *buf = dir_read(d, q[i].sector, sz, &owned);
        if (buf) {
            ctx.dir = buf;
            ctx.dir_size = sz;
            ctx.visited = vis;
            check_entry(&ctx, 0, 0);
            if (owned) {
                free((void *)buf);
            }
        } else {
            d->truncated = true;
            if (off + sz > d->truncated_need) {
                d->truncated_need = off + sz;
            }
        }
        free(vis);
    }
    if (d->truncated) {
        nc_log("hakuX-native", "disc: image is truncated (%.1f MiB, needs at least %.1f MiB)",
               (double)d->image_size / (1024.0 * 1024.0), (double)d->truncated_need / (1024.0 * 1024.0));
        return false;
    }
    return true;
}

void nc_disc_close(NcDisc *d)
{
    dir_cache_close(d);
    if (d->fd >= 0) {
        close(d->fd);
    }
    d->fd = -1;
}

static bool name_eq(const uint8_t *a, size_t alen, const char *b, size_t blen)
{
    if (alen != blen) {
        return false;
    }
    for (size_t i = 0; i < alen; i++) {
        if (toupper(a[i]) != toupper((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

/* Depth-first over the whole tree: robust against any ordering differences. */
static bool dir_search(const uint8_t *dir, uint32_t size, uint32_t off, const char *name,
                       size_t namelen, NcDiscFile *out, unsigned depth)
{
    if (off + 14 > size || depth > 64) {
        return false;
    }
    const uint8_t *e = dir + off;
    uint16_t left = nc_rd16(e), right = nc_rd16(e + 2);
    if (left == 0xFFFF && right == 0xFFFF && e[12] == 0xFF) {
        return false; /* padding */
    }
    uint8_t nlen = e[13];
    if (off + 14 + nlen <= size && name_eq(e + 14, nlen, name, namelen)) {
        out->sector = nc_rd32(e + 4);
        out->size = nc_rd32(e + 8);
        out->attr = e[12];
        return true;
    }
    return (left && left != 0xFFFF && dir_search(dir, size, left * 4u, name, namelen, out, depth + 1)) ||
           (right && right != 0xFFFF && dir_search(dir, size, right * 4u, name, namelen, out, depth + 1));
}

/* the disc's own files */
static bool disc_find(const NcDisc *d, const char *path, NcDiscFile *out)
{
    NcDiscFile cur = {d->root_sector, d->root_size, 0x10};
    const char *p = path;
    while (*p) {
        while (*p == '\\' || *p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *end = p;
        while (*end && *end != '\\' && *end != '/') {
            end++;
        }
        if (!(cur.attr & 0x10) || !cur.size) {
            return false;
        }
        bool owned = false;
        const uint8_t *dir = dir_read(d, cur.sector, cur.size, &owned);
        bool ok = dir && dir_search(dir, cur.size, 0, p, (size_t)(end - p), &cur, 0);
        if (owned) {
            free((void *)dir);
        }
        if (!ok) {
            return false;
        }
        p = end;
    }
    *out = cur;
    return true;
}

/* ---- the overlay (mods) ----
 * Entries keyed by their path, normalized: no leading slash, backslashes,
 * lower case. Added in the mods' order, then sorted (the later of two at a
 * path stays); a file's descriptor opens at its first read. */
typedef struct OvEntry {
    char path[256];             /* normalized: the key */
    char shown[256];            /* as the mod has it (listings) */
    char *host;                 /* NULL: a folder */
    uint32_t size;
    unsigned seq;               /* the order added: the later wins */
    int fd;                     /* atomic: -1 until the first read */
} OvEntry;

typedef struct OvPatch {
    uint32_t sector;            /* the file it patches (its NcDiscFile) */
    uint64_t off;
    uint32_t len;
    uint8_t *bytes;
} OvPatch;

struct NcDiscOverlay {
    OvEntry *e;
    unsigned n, cap;
    bool sealed;
    OvPatch *p;
    unsigned np, pcap;
};

static void norm_case(const char *in, char *out, size_t cap, bool lower)
{
    size_t n = 0;
    while (*in == '\\' || *in == '/') {
        in++;
    }
    for (; *in && n + 1 < cap; in++) {
        char c = *in == '/' ? '\\' : lower ? (char)tolower((unsigned char)*in) : *in;
        if (c == '\\' && n && out[n - 1] == '\\') {
            continue;
        }
        out[n++] = c;
    }
    while (n && out[n - 1] == '\\') {
        n--;
    }
    out[n] = 0;
}

static void norm_path(const char *in, char *out, size_t cap)
{
    norm_case(in, out, cap, true);
}

static NcDiscOverlay *ov_of(NcDisc *d)
{
    if (!d->ov) {
        d->ov = calloc(1, sizeof(*d->ov));
    }
    return d->ov;
}

/* shown: the path as the mod has it (normalized but its case) */
static OvEntry *ov_push(NcDiscOverlay *o, const char *path, const char *shown)
{
    if (o->n == o->cap) {
        unsigned cap = o->cap ? o->cap * 2 : 64;
        OvEntry *e = realloc(o->e, cap * sizeof(*e));
        if (!e) {
            return NULL;
        }
        o->e = e;
        o->cap = cap;
    }
    OvEntry *e = &o->e[o->n];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    snprintf(e->shown, sizeof(e->shown), "%s", shown);
    e->seq = o->n++;
    e->fd = -1;
    return e;
}

bool nc_disc_overlay_file(NcDisc *d, const char *path, const char *host)
{
    NcDiscOverlay *o = ov_of(d);
    char p[256], shown[256];
    norm_path(path, p, sizeof(p));
    norm_case(path, shown, sizeof(shown), false);   /* the same length: only the case differs */
    struct stat st;
    if (!o || o->sealed || !*p || stat(host, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 0xFFFFFFFFll) {
        return false;
    }
    OvEntry *e = ov_push(o, p, shown);
    if (!e || !(e->host = strdup(host))) {
        return false;
    }
    e->size = (uint32_t)st.st_size;
    /* the folders on its way the disc doesn't have */
    for (char *s = strchr(p, '\\'); s; s = strchr(s + 1, '\\')) {
        const size_t at = (size_t)(s - p);
        *s = 0;
        shown[at] = 0;
        NcDiscFile f;
        if (!disc_find(d, p, &f)) {
            ov_push(o, p, shown);
        } else if (!(f.attr & 0x10)) {
            nc_log("hakuX-native-mods", "%s: %s is a file on the disc", path, p);
        }
        *s = '\\';
        shown[at] = '\\';
    }
    return true;
}

static int ov_cmp(const void *a, const void *b)
{
    const OvEntry *x = a, *y = b;
    const int c = strcmp(x->path, y->path);
    return c ? c : (x->seq > y->seq) - (x->seq < y->seq);
}

void nc_disc_overlay_seal(NcDisc *d)
{
    NcDiscOverlay *o = d->ov;
    if (!o || o->sealed) {
        return;
    }
    qsort(o->e, o->n, sizeof(*o->e), ov_cmp);
    unsigned n = 0;
    for (unsigned i = 0; i < o->n; i++) {
        /* of the entries at a path the last added stays; a file over a folder */
        if (n && !strcmp(o->e[n - 1].path, o->e[i].path)) {
            if (!o->e[i].host && o->e[n - 1].host) {
                continue;
            }
            free(o->e[n - 1].host);
            o->e[n - 1] = o->e[i];
            continue;
        }
        o->e[n++] = o->e[i];
    }
    o->n = n;
    o->sealed = true;
}

static const OvEntry *ov_find(const NcDiscOverlay *o, const char *norm)
{
    if (!o || !o->sealed) {
        return NULL;
    }
    unsigned lo = 0, hi = o->n;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        const int c = strcmp(o->e[mid].path, norm);
        if (!c) {
            return &o->e[mid];
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

bool nc_disc_find(const NcDisc *d, const char *path, NcDiscFile *out)
{
    if (d->ov) {
        char p[256];
        norm_path(path, p, sizeof(p));
        const OvEntry *e = ov_find(d->ov, p);
        if (e) {
            out->sector = NC_DISC_OVERLAY + (uint32_t)(e - d->ov->e);
            out->size = e->size;
            out->attr = e->host ? 0x01 : 0x10;      /* read-only, or a folder */
            return true;
        }
    }
    return disc_find(d, path, out);
}

bool nc_disc_overlay_patch(NcDisc *d, const char *path, uint64_t off, const uint8_t *bytes, uint32_t len)
{
    NcDiscOverlay *o = ov_of(d);
    NcDiscFile f;
    if (!o || !len || !nc_disc_find(d, path, &f) || (f.attr & 0x10) || off + len > f.size) {
        return false;
    }
    if (o->np == o->pcap) {
        unsigned cap = o->pcap ? o->pcap * 2 : 16;
        OvPatch *p = realloc(o->p, cap * sizeof(*p));
        if (!p) {
            return false;
        }
        o->p = p;
        o->pcap = cap;
    }
    OvPatch *p = &o->p[o->np];
    if (!(p->bytes = malloc(len))) {
        return false;
    }
    memcpy(p->bytes, bytes, len);
    p->sector = f.sector;
    p->off = off;
    p->len = len;
    o->np++;
    return true;
}

void nc_disc_overlay_list(const NcDisc *d, const char *dir,
                          void (*fn)(void *ctx, const char *name, uint32_t size, bool is_dir), void *ctx)
{
    const NcDiscOverlay *o = d->ov;
    if (!o || !o->sealed) {
        return;
    }
    char p[256];
    norm_path(dir, p, sizeof(p));
    const size_t n = strlen(p);
    for (unsigned i = 0; i < o->n; i++) {
        const char *e = o->e[i].path;
        if (n && (strncmp(e, p, n) || e[n] != '\\')) {
            continue;
        }
        const size_t at = n ? n + 1 : 0;
        if (e[at] && !strchr(e + at, '\\')) {
            fn(ctx, o->e[i].shown + at, o->e[i].size, !o->e[i].host);
        }
    }
}

static bool ov_read(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    const NcDiscOverlay *o = d->ov;
    const uint32_t i = f->sector - NC_DISC_OVERLAY;
    if (!o || i >= o->n || !o->e[i].host) {
        return false;
    }
    OvEntry *e = &o->e[i];
    int fd = __atomic_load_n(&e->fd, __ATOMIC_ACQUIRE);
    if (fd < 0) {
        int mine = open(e->host, O_RDONLY | O_CLOEXEC), none = -1;
        if (mine < 0) {
            nc_log("hakuX-native-mods", "%s: can't open %s", e->path, e->host);
            return false;
        }
        if (!__atomic_compare_exchange_n(&e->fd, &none, mine, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            close(mine);                /* another thread's came first */
        }
        fd = __atomic_load_n(&e->fd, __ATOMIC_ACQUIRE);
    }
    return pread_all(fd, buf, len, off);
}

bool nc_disc_find_orig(const NcDisc *d, const char *path, NcDiscFile *out)
{
    return disc_find(d, path, out);
}

bool nc_disc_read_orig(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    return f->sector < NC_DISC_OVERLAY && off + len <= f->size &&
           pread_all(d->fd, buf, len, d->part_off + (uint64_t)f->sector * NC_XDVDFS_SECTOR + off);
}

bool nc_disc_read_file(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    if (off + len > f->size) {
        return false;
    }
    if (f->sector >= NC_DISC_OVERLAY) {
        return ov_read(d, f, off, buf, len);
    }
    const uint64_t pos = d->part_off + (uint64_t)f->sector * NC_XDVDFS_SECTOR + off;
    if (!pread_all(d->fd, buf, len, pos)) {
        if (errno == EIO || (d->image_size && pos + len > d->image_size)) {
            note_truncated(d, pos + len);
        }
        return false;
    }
    return true;
}

bool nc_disc_read(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    const bool ok = nc_disc_read_file(d, f, off, buf, len);
    if (ok && d->ov) {
        for (unsigned i = 0; i < d->ov->np; i++) {
            const OvPatch *p = &d->ov->p[i];
            if (p->sector != f->sector || p->off >= off + len || p->off + p->len <= off) {
                continue;
            }
            const uint64_t a = p->off > off ? p->off : off;
            const uint64_t b = p->off + p->len < off + len ? p->off + p->len : off + len;
            memcpy((uint8_t *)buf + (a - off), p->bytes + (a - p->off), (size_t)(b - a));
        }
    }
    return ok;
}
