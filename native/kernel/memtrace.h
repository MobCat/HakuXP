/*
 * HakuXP native core - debug: a hash of each page of a guest range at every
 * vblank the title takes, to a file: two runs of a playbook, compared
 * (tools/native/memtrace.py), give the first vblank and the pages where
 * their memory parts ways. With the playbook's vblank at the title's own
 * point (detevents.h), equal runs give equal files.
 *
 * NC_TRACE_MEM=<lo hex>-<hi hex>@<file> (debug.hakux.env for the app), e.g.
 * NC_TRACE_MEM=14e000-654000@/sdcard/Android/data/<app>/files/mem.bin.
 * The file: "HXMT", u32 lo, u32 hi; then a record per vblank: u32 vblank
 * (from the trace's start), u32 pages, a u32 FNV-1a hash per 4 KiB page
 * (0 for an unmapped one). NC_TRACE_MEMDUMP=<vblank> also writes the
 * range's bytes at that vblank to <file>.v<vblank>.
 */
#ifndef NATIVE_KERNEL_MEMTRACE_H
#define NATIVE_KERNEL_MEMTRACE_H

#include <stdint.h>

#include "native/kernel/kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

void nc_memtrace_init(void);
/* The guest CPU's thread, as the kernel dispatches lines: the vblank's line. */
void nc_memtrace_irq(NcKernel *k, uint32_t lines);

#ifdef __cplusplus
}
#endif

#endif
