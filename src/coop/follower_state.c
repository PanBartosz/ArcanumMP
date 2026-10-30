#include "coop/follower_state.h"
#include "coop/session.h"
#include <limits.h>
#include <string.h>
static bool valid(const CoopFollowerRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || r->order > COOP_FOLLOW_BROADCAST
        || !coop_identity_valid(r->actor) || !coop_object_id_valid(&r->follower, r->order == COOP_FOLLOW_BROADCAST)
        || !coop_object_id_valid(&r->target, true)) return false;
    if (r->order == COOP_FOLLOW_BROADCAST) {
        if (r->follower.bytes[0] || r->force || r->x > INT32_MAX || r->y > INT32_MAX || !r->command[0]) return false;
        bool ended = false;
        for (unsigned i = 0; i < sizeof(r->command); i++) {
            unsigned char ch = (unsigned char)r->command[i];
            if (!ch) ended = true;
            else if (ended || ch < 32 || ch == 127) return false;
        }
        return ended && (!r->target.bytes[0] || r->target.bytes[0] == 2 || r->target.bytes[0] == 3);
    }
    if (r->follower.bytes[0] != 2) return false;
    for (unsigned i = 0; i < sizeof(r->command); i++) if (r->command[i]) return false;
    if (r->order == COOP_FOLLOW_WALK) return !r->target.bytes[0] && !r->force && r->x <= INT32_MAX && r->y <= INT32_MAX;
    if (r->x || r->y) return false;
    return r->order == COOP_FOLLOW_ATTACK ? r->target.bytes[0] != 0 : !r->target.bytes[0] && !r->force;
}
bool coop_follower_encode(const CoopFollowerRequest* r, uint8_t bytes[COOP_FOLLOWER_REQUEST_BYTES])
{
    if (!bytes || !valid(r)) return false;
    coop_object_write32(bytes, r->epoch); coop_object_write32(bytes + 4, r->sequence); coop_object_write32(bytes + 8, r->map);
    memcpy(bytes + 12, r->actor, 16); memcpy(bytes + 28, r->follower.bytes, 17); memcpy(bytes + 45, r->target.bytes, 17);
    coop_object_write32(bytes + 62, r->x); coop_object_write32(bytes + 66, r->y); bytes[70] = (uint8_t)r->order; bytes[71] = r->force;
    coop_object_write32(bytes + 72, r->generation); memcpy(bytes + 76, r->command, sizeof(r->command)); return true;
}
bool coop_follower_decode(const void* bytes, size_t size, CoopFollowerRequest* out)
{
    if (!bytes || !out || size != COOP_FOLLOWER_REQUEST_BYTES) return false;
    const uint8_t* p = bytes; if (p[71] > 1) return false;
    CoopFollowerRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8),
        .x = coop_object_read32(p + 62), .y = coop_object_read32(p + 66), .order = (CoopFollowerOrder)p[70], .force = p[71] != 0, .generation = coop_object_read32(p + 72) };
    memcpy(r.actor, p + 12, 16); memcpy(r.follower.bytes, p + 28, 17); memcpy(r.target.bytes, p + 45, 17);
    memcpy(r.command, p + 76, sizeof(r.command));
    if (!valid(&r)) return false;
    *out = r; return true;
}
