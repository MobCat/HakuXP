/*
 * HakuXP native core: the host CPU in one line, from /proc/cpuinfo and the
 * cores' top clocks: "1x Cortex-X3 3.19 GHz, 2x Cortex-A710 2.80 GHz, ...
 * | atomics lrcpc ilrcpc flagm flagm2". With the GPU driver it tells device
 * types apart in a run's log (devinfo.c logs it). Pure: text in, text out;
 * tested on the host (tests/hle/test_cpuinfo.c).
 */
#ifndef NATIVE_CORE_CPUINFO_H
#define NATIVE_CORE_CPUINFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A core's name by its MIDR implementer and part ("Cortex-X3"), or NULL. */
const char *nc_cpu_part_name(unsigned implementer, unsigned part);

/* cpuinfo: /proc/cpuinfo's text. max_khz[i]: core i's top clock (0 unknown),
 * nkhz of them. Writes the cores, fastest first, alike ones counted
 * together, then the CPU features the JIT cares about. Returns the length. */
size_t nc_cpu_describe(const char *cpuinfo, const unsigned *max_khz, unsigned nkhz, char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif
