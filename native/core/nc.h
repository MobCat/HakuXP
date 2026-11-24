/*
 * hakuX native core — common definitions.
 *
 * The native core runs an Xbox title without QEMU: FEXCore (fork, branch
 * hakuX) executes the 32-bit guest behind a guest memory base, and the Xbox
 * kernel is reimplemented in native/kernel. See
 * docs/superpowers/plans/2026-10-12-native-core-00-roadmap.md.
 */
#ifndef NATIVE_CORE_NC_H
#define NATIVE_CORE_NC_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_PAGE 4096u

/* Log line to stderr and the Android log under the given tag. */
void nc_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Guest DbgPrint preserves the guest thread ID on the debug wire. */
void nc_log_guest(uint32_t thread, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* The emulation count (EC) log lines start with ("[0001A2B3] "): the
 * kernel's emulated kernel calls (the counter at count) counted from the
 * EC nc_log_count_start set (0 by default), read from any thread. Never
 * saved in a state and never going back (log.c). */
void nc_log_count_start(uint64_t ec);
void nc_log_count_from(const uint64_t *count);
/* A state replaced the kernel's counter: the EC goes on from ec, its value
 * before the load. */
void nc_log_count_continue(uint64_t ec);
/* Its value now; false before the kernel registers it. */
bool nc_log_count(uint64_t *value);
/* Logs the device once per process ("hakuX-native-device": maker, model,
 * SoC, Android build, RAM; the CPU's cores, clocks and features). */
void nc_device_log(void);
/* Formats the same device and CPU lines into buf (crash reports). */
size_t nc_device_summary(char *buf, size_t len);
/* A report line ("tag: text\n") to fd, or with fd < 0 to the log (the app's
 * on-demand dumps). */
void nc_dump_line(int fd, const char *line, size_t len);

static inline uint16_t nc_rd16(const void *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static inline uint32_t nc_rd32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static inline void nc_wr16(void *p, uint16_t v)
{
    memcpy(p, &v, 2);
}

static inline void nc_wr32(void *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static inline uint32_t nc_align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) & ~(a - 1);
}

#ifdef __cplusplus
}
#endif

#endif
