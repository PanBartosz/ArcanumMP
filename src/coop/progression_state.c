#include "coop/progression_state.h"
#include "coop/identity.h"
#include <limits.h>
#include <string.h>
bool coop_progression_selection_valid(CoopProgressionKind kind, unsigned index)
{
    switch (kind) {
    case COOP_PROGRESS_STAT: case COOP_PROGRESS_DISCIPLINE: return index < 8;
    case COOP_PROGRESS_HP: case COOP_PROGRESS_FATIGUE: return !index;
    case COOP_PROGRESS_SKILL: case COOP_PROGRESS_COLLEGE: return index < 16;
    case COOP_PROGRESS_SCHEME: return index < 200;
    default: return false;
    }
}
static bool valid(const CoopProgressionRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)) return false;
    if (r->operation == COOP_PROGRESS_OPTIONS)
        return !r->kind && !r->lease && !r->index && r->direction < 32 && !r->points
            && (!r->expected || (r->expected & ~127u) == 0x80);
    if (r->operation == COOP_PROGRESS_FATE)
        return !r->kind && !r->lease && r->index < 12 && (r->direction == 1 || (r->direction == 2 && r->index))
            && r->expected <= 0x7ffu && r->points <= INT32_MAX;
    if (r->operation == COOP_PROGRESS_OPEN || r->operation == COOP_PROGRESS_CLOSE)
        return !r->kind && !r->index && !r->direction && !r->expected && !r->points
            && (r->operation == COOP_PROGRESS_OPEN ? !r->lease : r->lease != 0);
    return r->operation == COOP_PROGRESS_EDIT && r->lease && (r->direction == 1 || r->direction == 2)
        && (r->kind != COOP_PROGRESS_SCHEME || r->direction == 1)
        && r->expected <= INT32_MAX && r->points <= INT32_MAX && coop_progression_selection_valid(r->kind, r->index);
}
bool coop_progression_request_encode(const CoopProgressionRequest* r, uint8_t out[COOP_PROGRESSION_REQUEST_BYTES])
{
    if (!out || !valid(r)) return false;
    coop_object_write32(out, r->epoch); coop_object_write32(out + 4, r->sequence); coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, 16);
    out[28] = (uint8_t)r->operation; out[29] = (uint8_t)r->kind; out[30] = r->index; out[31] = r->direction;
    coop_object_write32(out + 32, r->lease); coop_object_write32(out + 36, r->expected); coop_object_write32(out + 40, r->points);
    return true;
}
bool coop_progression_request_decode(const void* bytes, size_t size, CoopProgressionRequest* out)
{
    if (!bytes || !out || size != COOP_PROGRESSION_REQUEST_BYTES) return false;
    const uint8_t* p = bytes;
    CoopProgressionRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8),
        .operation = (CoopProgressionOperation)p[28], .kind = (CoopProgressionKind)p[29], .index = p[30], .direction = p[31],
        .lease = coop_object_read32(p + 32), .expected = coop_object_read32(p + 36), .points = coop_object_read32(p + 40) };
    memcpy(r.actor, p + 12, 16);
    if (!valid(&r)) return false;
    *out = r; return true;
}
