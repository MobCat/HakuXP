/*
 * hakuX HLE GPU - push-buffer mode's object table: which object goes when
 * the table is full (pbdraw.c's evict_slot). Pure: the table is described
 * to it, nothing is freed here.
 *
 * Objects are render targets and cube textures. A cube's faces are views
 * into its image (HleSurfDesc.parent): the cube and its views are one unit,
 * freed together and kept while any of them is in use. Titles that make a
 * new cube for each level (RalliSport Challenge 2: one per stage, at a new
 * address) leave the old ones behind; they go once used longest ago.
 */
#ifndef HW_XBOX_HLE_PB_PBSLOT_H
#define HW_XBOX_HLE_PB_PBSLOT_H

#include "hw/xbox/hle/hle_pure.h"

typedef struct HlePbSlot {
    uint32_t id;
    uint32_t parent;                    /* a face view's cube (its id); 0 none */
    uint64_t used;                      /* last used: bound as a target, a cube sampled */
    bool cube;
    bool busy;                          /* in use now: it can't go */
} HlePbSlot;

/* The object to free: the one used longest ago (never used first), a cube
 * counting as its latest use and its views' and busy while any of them is.
 * Views are never chosen alone: their cube's id stands for them (a view
 * whose cube isn't in the table goes alone). 0 when every one is busy. */
uint32_t hle_pb_slot_victim(const HlePbSlot *s, unsigned n);

#endif
