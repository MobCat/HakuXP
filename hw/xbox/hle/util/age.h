/*
 * hakuX HLE GPU: how long ago, for the stutter monitor's overlay lines.
 */
#ifndef HW_XBOX_HLE_UTIL_AGE_H
#define HW_XBOX_HLE_UTIL_AGE_H

#include <stddef.h>
#include <stdint.h>

/* "22s ago", "12m30s ago", "1h05m ago" for us microseconds ago; "--" once it
 * is limit_s seconds or older (limit_s 0: never), a time no one needs to
 * watch counting up. Pure: no clock, no state. */
void hle_age_text(char *buf, size_t n, int64_t us, int limit_s);

#endif
