/*
 * HakuXP native core - crash report builder.
 *
 * Formats guest CPU/fault state, stack trace, XBE layout, recent kernel calls,
 * guest threads, host device/CPU info and Vulkan GPU/driver/quirk info into a
 * plain ASCII text report for diagnosing "The game stopped" and boot failures.
 */
#ifndef NATIVE_CORE_CRASH_REPORT_H
#define NATIVE_CORE_CRASH_REPORT_H

#include "native/core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds a newly allocated NUL-terminated ASCII crash report (caller frees),
 * or NULL on allocation failure. core_stopped must be true before guest memory
 * and thread tables are inspected. */
char *nc_crash_report_build(const NcCore *c, bool core_stopped, const char *fatal_why, int fatal_kind);

#ifdef __cplusplus
}
#endif

#endif
