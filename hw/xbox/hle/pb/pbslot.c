/*
 * hakuX HLE GPU - push-buffer mode's object table eviction (pbslot.h).
 */
#include "hw/xbox/hle/pb/pbslot.h"

static const HlePbSlot *find(const HlePbSlot *s, unsigned n, uint32_t id)
{
    for (unsigned i = 0; id && i < n; i++) {
        if (s[i].id == id) {
            return &s[i];
        }
    }
    return NULL;
}

/* A cube with its views: busy if any is, used when any was last */
static void cube_unit(const HlePbSlot *s, unsigned n, const HlePbSlot *c, bool *busy, uint64_t *used)
{
    *busy = c->busy;
    *used = c->used;
    for (unsigned i = 0; i < n; i++) {
        if (s[i].parent == c->id && !s[i].cube) {
            *busy |= s[i].busy;
            *used = MAX(*used, s[i].used);
        }
    }
}

uint32_t hle_pb_slot_victim(const HlePbSlot *s, unsigned n)
{
    uint32_t best = 0;
    uint64_t best_used = 0;
    for (unsigned i = 0; i < n; i++) {
        const HlePbSlot *o = &s[i];
        if (!o->id) {
            continue;
        }
        bool busy = o->busy;
        uint64_t used = o->used;
        if (o->cube) {
            cube_unit(s, n, o, &busy, &used);
        } else if (o->parent && find(s, n, o->parent)) {
            continue;                       /* goes with its cube */
        }
        if (!busy && (!best || used < best_used)) {
            best = o->id;
            best_used = used;
        }
    }
    return best;
}
