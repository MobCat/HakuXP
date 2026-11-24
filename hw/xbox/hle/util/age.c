/*
 * hakuX HLE GPU: how long ago, for the stutter monitor's overlay lines
 * (see age.h).
 */
#include "hw/xbox/hle/util/age.h"

#include <stdio.h>

void hle_age_text(char *buf, size_t n, int64_t us, int limit_s)
{
    int64_t s = us > 0 ? us / 1000000 : 0;
    if (limit_s > 0 && s >= limit_s) {
        snprintf(buf, n, "--");
    } else if (s < 60) {
        snprintf(buf, n, "%llds ago", (long long)s);
    } else if (s < 3600) {
        snprintf(buf, n, "%lldm%02llds ago", (long long)(s / 60), (long long)(s % 60));
    } else {
        snprintf(buf, n, "%lldh%02lldm ago", (long long)(s / 3600), (long long)(s / 60 % 60));
    }
}
