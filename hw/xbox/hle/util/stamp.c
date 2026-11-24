/*
 * HakuXP HLE GPU: a number as 7-segment hex digits (stamp.h).
 */
#include "hw/xbox/hle/util/stamp.h"

/* a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle */
static const uint8_t k_digits[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,     /* 0 1 2 3 4 5 6 7 */
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,     /* 8 9 A b C d E F */
};

uint8_t hle_stamp_segments(unsigned d)
{
    return k_digits[d & 0xF];
}

unsigned hle_stamp_layout(uint64_t value, unsigned min_digits, int32_t right, int32_t top, int32_t t,
                          HleStampRect *box, HleStampRect seg[HLE_STAMP_MAX_SEGMENTS])
{
    /* each segment's place in a 4t x 7t digit, in units of t: x, y, w, h */
    static const int8_t k_seg[7][4] = {
        { 1, 0, 2, 1 }, { 3, 1, 1, 2 }, { 3, 4, 1, 2 }, { 1, 6, 2, 1 },
        { 0, 4, 1, 2 }, { 0, 1, 1, 2 }, { 1, 3, 2, 1 },
    };
    unsigned digits = 1;
    while (digits < HLE_STAMP_MAX_DIGITS && (value >> (4 * digits))) {
        digits++;
    }
    if (digits < min_digits) {
        digits = min_digits < HLE_STAMP_MAX_DIGITS ? min_digits : HLE_STAMP_MAX_DIGITS;
    }
    const int32_t width = (int32_t)digits * 5 * t - t;   /* 4t a digit, t between */
    box->w = width + 2 * t;
    box->h = 9 * t;
    box->x = right - box->w;
    box->y = top;
    unsigned n = 0;
    for (unsigned i = 0; i < digits; i++) {
        const unsigned d = (unsigned)(value >> (4 * (digits - 1 - i))) & 0xF;
        const int32_t x0 = box->x + t + (int32_t)i * 5 * t, y0 = box->y + t;
        for (unsigned s = 0; s < 7; s++) {
            if (k_digits[d] >> s & 1) {
                seg[n++] = (HleStampRect){ x0 + k_seg[s][0] * t, y0 + k_seg[s][1] * t,
                                           k_seg[s][2] * t, k_seg[s][3] * t };
            }
        }
    }
    return n;
}
