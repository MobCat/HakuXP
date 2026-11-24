/*
 * hakuX native core — logging to stderr and the Android log.
 */
#include "native/core/nc.h"
#include "native/debug/xbdm.h"

#include <stdio.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

/* The emulation count (EC) every line starts with, in hex: emulated kernel
 * calls, so a line can be placed against guest work and against a
 * screenshot, which the present pass stamps with the same count. It is the
 * log's own, not the kernel's total (k->calls): a savestate keeps that total
 * and a load puts it back, which would show the same EC twice in one
 * capture. The EC is never saved and never goes back: a load inside this
 * process goes on from where it was (nc_log_count_continue), and a session
 * that goes on in a new process (a state's load, a relaunch) starts it where
 * the last one left off (ec=, nc_log_count_start). Unset (xbdm_smoke): no
 * prefix. */
static const uint64_t *g_count;     /* the kernel's call total */
static uint64_t g_offset;           /* EC = total + offset (mod 2^64) */
static uint64_t g_start;            /* the EC the count starts at */

void nc_log_count_start(uint64_t ec)
{
    g_start = ec;
}

void nc_log_count_from(const uint64_t *count)
{
    __atomic_store_n(&g_offset, g_start - *count, __ATOMIC_RELAXED);
    __atomic_store_n(&g_count, count, __ATOMIC_RELEASE);
}

void nc_log_count_continue(uint64_t ec)
{
    const uint64_t *c = __atomic_load_n(&g_count, __ATOMIC_ACQUIRE);
    if (c) {
        __atomic_store_n(&g_offset, ec - __atomic_load_n(c, __ATOMIC_RELAXED), __ATOMIC_RELEASE);
    }
}

bool nc_log_count(uint64_t *value)
{
    const uint64_t *c = __atomic_load_n(&g_count, __ATOMIC_ACQUIRE);
    if (!c) {
        return false;
    }
    /* one writer (the vCPU thread); an aligned 64-bit load is whole on ARM64 */
    *value = __atomic_load_n(c, __ATOMIC_RELAXED) + __atomic_load_n(&g_offset, __ATOMIC_ACQUIRE);
    return true;
}

static void log_message(uint32_t thread, const char *tag, const char *fmt, va_list ap)
{
    char text[1024], buf[1048];
    vsnprintf(text, sizeof(text), fmt, ap);
    char count[24] = "";
    uint64_t c;
    if (nc_log_count(&c)) {
        snprintf(count, sizeof(count), "[%08llX] ", (unsigned long long)c);
    }
    snprintf(buf, sizeof(buf), "%s%s", count, text);
    nc_xbdm_log(thread, tag, buf);
    fprintf(stderr, "%s: %s\n", tag, buf);
#ifdef __ANDROID__
    if (thread) {
        char guest[1100];
        /* logcat repeats its prefix per line, so retain guest attribution
         * on every line of a multiline DbgPrint as well; the count follows
         * the guest marker, which the app's log reader takes off first
         * (debug/LogcatRecord.kt). */
        const char *line = text;
        for (;;) {
            const char *end = strchr(line, '\n');
            int length = end ? (int)(end - line) : (int)strlen(line);
            snprintf(guest, sizeof(guest), "[guest-thread=0x%08x] %s%.*s", thread, count, length, line);
            __android_log_write(ANDROID_LOG_INFO, tag, guest);
            if (!end || !end[1]) break;
            line = end + 1;
        }
    } else {
        __android_log_write(ANDROID_LOG_INFO, tag, buf);
    }
#endif
}

void nc_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_message(0, tag, fmt, ap);
    va_end(ap);
}

void nc_log_guest(uint32_t thread, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_message(thread, tag, fmt, ap);
    va_end(ap);
}

void nc_dump_line(int fd, const char *line, size_t len)
{
    if (fd >= 0) {
        write(fd, line, len);
        return;
    }
    char tag[64] = "hakuX-native";
    const char *colon = memchr(line, ':', len);
    if (colon && (size_t)(colon - line) < sizeof(tag)) {
        memcpy(tag, line, (size_t)(colon - line));
        tag[colon - line] = '\0';
        len -= (size_t)(colon - line) + 1;
        line = colon + 1;
    }
    while (len && *line == ' ') {
        line++;
        len--;
    }
    while (len && line[len - 1] == '\n') {
        len--;
    }
    nc_log(tag, "%.*s", (int)len, line);
}
