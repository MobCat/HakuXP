/*
 * HakuXP native core - debug: how often one guest function runs, and from
 * where, frame by frame: for finding where two runs of a playbook part ways.
 *
 * NC_TRACE_EIP=<hex address> (debug.hakux.env for the app): the instruction
 * there is hooked (core.c's hook query) and each time it runs, its caller
 * (the return address on the stack) and the guest thread are counted. At
 * each vblank the title takes, one line, if it ran:
 *
 *   hakuX-native-trace: [EC] eip 0001bfd0 vblank 312: 7 calls: t8 00017070x3 0005230ax4
 *
 * (vblank: counted from the core's start). Two runs' lines, compared, give
 * the first vblank where the calls differ and which callers did it.
 */
#ifndef NATIVE_KERNEL_CALLTRACE_H
#define NATIVE_KERNEL_CALLTRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "native/kernel/kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

void nc_calltrace_init(void);
bool nc_calltrace_hook_query(uint32_t eip);
/* The hook at eip: true when it was the traced one (counted, then run). */
bool nc_calltrace_hook(NcKernel *k, NcFrame *f, uint32_t eip);
/* The guest CPU's thread, as the kernel dispatches lines: the vblank's line. */
void nc_calltrace_irq(uint32_t lines);

#ifdef __cplusplus
}
#endif

#endif
