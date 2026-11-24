/*
 * HakuXP native core: the host CPU in one line (cpuinfo.h).
 */
#include "native/core/cpuinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CORES 64

typedef struct Part {
    unsigned implementer, part;
    const char *name;
} Part;

/* The cores phones and handhelds have (MIDR implementer 0x41 Arm, 0x51 Qualcomm). */
static const Part k_parts[] = {
    { 0x41, 0xd03, "Cortex-A53" },  { 0x41, 0xd04, "Cortex-A35" },  { 0x41, 0xd05, "Cortex-A55" },
    { 0x41, 0xd07, "Cortex-A57" },  { 0x41, 0xd08, "Cortex-A72" },  { 0x41, 0xd09, "Cortex-A73" },
    { 0x41, 0xd0a, "Cortex-A75" },  { 0x41, 0xd0b, "Cortex-A76" },  { 0x41, 0xd0d, "Cortex-A77" },
    { 0x41, 0xd41, "Cortex-A78" },  { 0x41, 0xd44, "Cortex-X1" },   { 0x41, 0xd46, "Cortex-A510" },
    { 0x41, 0xd47, "Cortex-A710" }, { 0x41, 0xd48, "Cortex-X2" },   { 0x41, 0xd4b, "Cortex-A78C" },
    { 0x41, 0xd4d, "Cortex-A715" }, { 0x41, 0xd4e, "Cortex-X3" },   { 0x41, 0xd80, "Cortex-A520" },
    { 0x41, 0xd81, "Cortex-A720" }, { 0x41, 0xd82, "Cortex-X4" },   { 0x41, 0xd85, "Cortex-X925" },
    { 0x41, 0xd87, "Cortex-A725" },
    { 0x51, 0x001, "Oryon" },       { 0x51, 0x800, "Kryo 2xx Gold" }, { 0x51, 0x801, "Kryo 2xx Silver" },
    { 0x51, 0x802, "Kryo 3xx Gold" }, { 0x51, 0x803, "Kryo 3xx Silver" }, { 0x51, 0x804, "Kryo 4xx Gold" },
    { 0x51, 0x805, "Kryo 4xx Silver" },
};

const char *nc_cpu_part_name(unsigned implementer, unsigned part)
{
    for (size_t i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); i++) {
        if (k_parts[i].implementer == implementer && k_parts[i].part == part) {
            return k_parts[i].name;
        }
    }
    return NULL;
}

/* The value after "key<spaces>:" on a line, or NULL. */
static const char *field(const char *line, const char *key)
{
    const size_t n = strlen(key);
    if (strncmp(line, key, n)) {
        return NULL;
    }
    const char *p = line + n;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return *p == ':' ? p + 1 : NULL;
}

/* Whether the space-separated list (to the line's end) has word. */
static int has_word(const char *list, const char *word)
{
    const size_t n = strlen(word);
    for (const char *p = list; *p && *p != '\n';) {
        while (*p == ' ') {
            p++;
        }
        const char *e = p;
        while (*e && *e != ' ' && *e != '\n') {
            e++;
        }
        if ((size_t)(e - p) == n && !strncmp(p, word, n)) {
            return 1;
        }
        p = e;
    }
    return 0;
}

size_t nc_cpu_describe(const char *cpuinfo, const unsigned *max_khz, unsigned nkhz, char *out, size_t n)
{
    unsigned impl[MAX_CORES] = {0}, part[MAX_CORES] = {0};
    int cores = 0, cur = -1;
    const char *features = NULL;
    for (const char *line = cpuinfo; line && *line;) {
        const char *v;
        if ((v = field(line, "processor"))) {
            cur = atoi(v);
            if (cur >= 0 && cur < MAX_CORES && cur + 1 > cores) {
                cores = cur + 1;
            }
        } else if (cur >= 0 && cur < MAX_CORES && (v = field(line, "CPU implementer"))) {
            impl[cur] = (unsigned)strtoul(v, NULL, 0);
        } else if (cur >= 0 && cur < MAX_CORES && (v = field(line, "CPU part"))) {
            part[cur] = (unsigned)strtoul(v, NULL, 0);
        } else if (!features && (v = field(line, "Features"))) {
            features = v;
        }
        line = strchr(line, '\n');
        line = line ? line + 1 : NULL;
    }
    size_t o = 0;
    if (n) {
        out[0] = 0;
    }
    if (cores) {
        o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%d cores: ", cores);
    }
    /* fastest first: the cores counted down (phones list their little cores first),
     * alike ones (part and clock) together in one group */
    int done[MAX_CORES] = {0};
    int groups = 0;
    for (int i = cores - 1; i >= 0; i--) {
        if (done[i]) {
            continue;
        }
        const unsigned khz = (unsigned)i < nkhz ? max_khz[i] : 0;
        int count = 0;
        for (int j = i; j >= 0; j--) {
            const unsigned kj = (unsigned)j < nkhz ? max_khz[j] : 0;
            if (!done[j] && impl[j] == impl[i] && part[j] == part[i] && kj == khz) {
                done[j] = 1;
                count++;
            }
        }
        char name[24];
        const char *known = nc_cpu_part_name(impl[i], part[i]);
        if (known) {
            snprintf(name, sizeof(name), "%s", known);
        } else {
            snprintf(name, sizeof(name), "0x%02x:0x%03x", impl[i], part[i]);
        }
        o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%s%dx %s", groups++ ? ", " : "", count, name);
        if (khz) {
            o += (size_t)snprintf(out + o, o < n ? n - o : 0, " %.2f GHz", khz / 1e6);
        }
    }
    if (features) {
        /* what FEX uses when it's there: LSE atomics, RCPC loads, the flag
         * extensions, AFP, SVE */
        static const char *const k_of_interest[] = { "atomics", "lrcpc", "ilrcpc", "flagm", "flagm2",
                                                     "afp", "rpres", "sve", "sve2" };
        o += (size_t)snprintf(out + o, o < n ? n - o : 0, " |");
        int any = 0;
        for (size_t i = 0; i < sizeof(k_of_interest) / sizeof(k_of_interest[0]); i++) {
            if (has_word(features, k_of_interest[i])) {
                o += (size_t)snprintf(out + o, o < n ? n - o : 0, " %s", k_of_interest[i]);
                any = 1;
            }
        }
        if (!any) {
            o += (size_t)snprintf(out + o, o < n ? n - o : 0, " none of atomics/lrcpc/flagm/afp/sve");
        }
    }
    return o < n ? o : (n ? n - 1 : 0);
}
