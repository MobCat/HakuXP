/*
 * HakuXP native core: the device a run is on, logged once per process
 * ("hakuX-native-device"), so a capture says what it ran on next to the GPU
 * driver's own line (hw/xbox/hle/vk/quirks.c): maker, model, SoC, Android
 * build, RAM, and the CPU (cpuinfo.c). Read from system properties, /proc
 * and /sys, all readable by apps; what can't be read shows as "?".
 */
#include "native/core/nc.h"
#include "native/core/cpuinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

static void prop(const char *name, char *out)
{
    out[0] = 0;
    __system_property_get(name, out);
    if (!out[0]) {
        strcpy(out, "?");
    }
}

/* A small text file's contents (truncated to n - 1), "" when it can't be read. */
static size_t slurp(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t got = f ? fread(out, 1, n - 1, f) : 0;
    if (f) {
        fclose(f);
    }
    out[got] = 0;
    return got;
}

static char g_device_line[512];
static char g_cpu_line[512];
static int g_devinfo_ready;

static void collect_device_info(void)
{
    if (g_devinfo_ready) {
        return;
    }
    char maker[PROP_VALUE_MAX], model[PROP_VALUE_MAX], device[PROP_VALUE_MAX], soc_maker[PROP_VALUE_MAX],
        soc[PROP_VALUE_MAX], board[PROP_VALUE_MAX], release[PROP_VALUE_MAX], sdk[PROP_VALUE_MAX],
        build[PROP_VALUE_MAX];
    prop("ro.product.manufacturer", maker);
    prop("ro.product.model", model);
    prop("ro.product.device", device);
    prop("ro.soc.manufacturer", soc_maker);
    prop("ro.soc.model", soc);
    prop("ro.board.platform", board);
    prop("ro.build.version.release", release);
    prop("ro.build.version.sdk", sdk);
    prop("ro.build.display.id", build);
    char text[4096];
    double ram_gib = 0;
    if (slurp("/proc/meminfo", text, sizeof(text))) {
        const char *m = strstr(text, "MemTotal:");
        ram_gib = m ? strtod(m + 9, NULL) / (1024.0 * 1024.0) : 0;
    }
    snprintf(g_device_line, sizeof(g_device_line),
             "%s %s (%s) | SoC %s %s (%s) | Android %s (API %s), %s | RAM %.1f GiB",
             maker, model, device, soc_maker, soc, board, release, sdk, build, ram_gib);

    static char cpuinfo[65536];
    slurp("/proc/cpuinfo", cpuinfo, sizeof(cpuinfo));
    unsigned khz[64] = {0};
    for (unsigned i = 0; i < 64; i++) {
        char path[96], v[32];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", i);
        if (!slurp(path, v, sizeof(v))) {
            if (i >= 8) {
                break;
            }
            continue;   /* an offline core: its siblings may still answer */
        }
        khz[i] = (unsigned)strtoul(v, NULL, 10);
    }
    nc_cpu_describe(cpuinfo, khz, 64, g_cpu_line, sizeof(g_cpu_line));
    if (!g_cpu_line[0]) {
        strcpy(g_cpu_line, "?");
    }
    g_devinfo_ready = 1;
}

void nc_device_log(void)
{
    static int done;
    if (done++) {
        return;
    }
    collect_device_info();
    nc_log("hakuX-native-device", "device: %s", g_device_line);
    nc_log("hakuX-native-device", "cpu: %s", g_cpu_line);
}

size_t nc_device_summary(char *buf, size_t len)
{
    if (!buf || !len) {
        return 0;
    }
    collect_device_info();
    int n = snprintf(buf, len, "device: %s\ncpu: %s\n", g_device_line, g_cpu_line);
    if (n < 0) {
        buf[0] = 0;
        return 0;
    }
    return (size_t)n < len ? (size_t)n : len - 1;
}
