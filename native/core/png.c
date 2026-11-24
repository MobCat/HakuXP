/*
 * HakuXP native core - PNG files (png.h), moved here from xboxrun's
 * capture.c so the app's playbook checkpoints write the same files.
 */
#include "native/core/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t be[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len};
    fwrite(be, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) {
        fwrite(data, 1, len, f);
    }
    uLong crc = crc32(0, (const Bytef *)type, 4);
    crc = crc32(crc, data, len);
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
}

bool nc_png_write(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    const size_t row = 1 + (size_t)w * 3;
    uint8_t *raw = malloc(row * h);
    if (!raw) {
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *d = raw + row * y;
        const uint8_t *s = rgba + (size_t)y * w * 4;
        d[0] = 0;
        for (uint32_t x = 0; x < w; x++) {
            d[1 + 3 * x] = s[4 * x];
            d[2 + 3 * x] = s[4 * x + 1];
            d[3 + 3 * x] = s[4 * x + 2];
        }
    }
    uLongf zlen = compressBound(row * h);
    uint8_t *z = malloc(zlen);
    bool ok = z && compress2(z, &zlen, raw, row * h, 6) == Z_OK;
    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                            (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                            8, 2, 0, 0, 0};
        fwrite(sig, 1, 8, f);
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", z, (uint32_t)zlen);
        png_chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0;
    } else {
        ok = false;
    }
    free(raw);
    free(z);
    return ok;
}
