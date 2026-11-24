/*
 * HakuXP HLE GPU: a number as 7-segment hex digits in solid rectangles, no
 * font and no shader. The present pass stamps a number the host gives it on
 * each frame it presents (vkCmdClearAttachments), so a screenshot carries
 * it: the native core's emulation count, the same one its log lines start
 * with (native/core/log.c). Pure; tested on the host (tests/hle/test_stamp.c).
 */
#ifndef HW_XBOX_HLE_UTIL_STAMP_H
#define HW_XBOX_HLE_UTIL_STAMP_H

#include "hw/xbox/hle/hle_pure.h"

typedef struct HleStampRect {
    int32_t x, y, w, h;     /* window pixels, top left at the origin */
} HleStampRect;

#define HLE_STAMP_MAX_DIGITS 16
#define HLE_STAMP_MAX_SEGMENTS (HLE_STAMP_MAX_DIGITS * 7)

/* value in upper-case hex, at least min_digits digits, its box's top right
 * corner at (right, top), segments t pixels thick (digits 4t by 7t, t apart,
 * the box a t margin around them). *box: the box; seg[]: the lit segments,
 * their count returned. */
unsigned hle_stamp_layout(uint64_t value, unsigned min_digits, int32_t right, int32_t top, int32_t t,
                          HleStampRect *box, HleStampRect seg[HLE_STAMP_MAX_SEGMENTS]);

/* The segments lit for hex digit d (bit 0 a, the top, then b c d e f g). */
uint8_t hle_stamp_segments(unsigned d);

#endif
