/*
 * HakuXP native core - a picture as a PNG file: 8-bit RGB from RGBA rows,
 * top row first, through zlib. Used by xboxrun's frame captures
 * (native/run/capture.c) and the playbooks' checkpoints (native/dev/padplay.c).
 */
#ifndef NATIVE_CORE_PNG_H
#define NATIVE_CORE_PNG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes path; false when it couldn't (memory, compression, the file). */
bool nc_png_write(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif

#endif
