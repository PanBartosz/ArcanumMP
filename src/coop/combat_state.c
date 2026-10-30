#include "coop/combat_state.h"
#include <limits.h>
#include <string.h>
static bool instance_id(const CoopObjectId* id, bool nullable)
{
    return coop_object_id_valid(id, nullable) && (!id->bytes[0] || id->bytes[0] == 2 || id->bytes[0] == 3);
}
bool coop_combat_state_valid(const CoopCombatState* s)
{
    if (!s || s->points > INT32_MAX || s->round > INT32_MAX || s->generation == UINT32_MAX
        || !instance_id(&s->actor, !s->active) || s->order_count > COOP_COMBAT_ORDER_MAX
        || (!s->active && s->order_count)) return false;
    for (unsigned i = 0; i < COOP_COMBAT_ORDER_MAX; i++) {
        if (!instance_id(&s->order[i], i >= s->order_count)
            || (i >= s->order_count && s->order[i].bytes[0])) return false;
        if (i < s->order_count) {
            if (!s->order[i].bytes[0]) return false;
            for (unsigned j = 0; j < i; j++)
                if (!memcmp(&s->order[i], &s->order[j], sizeof(s->order[i]))) return false;
        }
    }
    if (s->order_count && memcmp(&s->actor, &s->order[0], sizeof(s->actor))) return false;
    if (s->active) return s->turn_based && s->actor.bytes[0] && s->generation && s->round;
    return !s->actor.bytes[0] && !s->points && !s->round;
}
bool coop_combat_state_encode(const CoopCombatState* s, uint8_t out[COOP_COMBAT_STATE_BYTES])
{
    if (!out || !coop_combat_state_valid(s)) return false;
    memcpy(out, "ACB2", 4);
    coop_object_write32(out + 4, (s->turn_based ? 1u : 0u) | (s->active ? 2u : 0u));
    coop_object_write32(out + 8, s->points);
    coop_object_write32(out + 12, s->round);
    coop_object_write32(out + 16, s->generation);
    memcpy(out + 20, s->actor.bytes, sizeof(s->actor.bytes));
    out[37] = s->order_count;
    for (unsigned i = 0; i < COOP_COMBAT_ORDER_MAX; i++) memcpy(out + 38 + i * 17, s->order[i].bytes, 17);
    return true;
}
bool coop_combat_state_decode(const void* data, size_t size, CoopCombatState* out)
{
    if (!data || !out || size != COOP_COMBAT_STATE_BYTES) return false;
    const uint8_t* p = data;
    uint32_t flags = coop_object_read32(p + 4);
    if (memcmp(p, "ACB2", 4) || (flags & ~3u)) return false;
    CoopCombatState s = { .turn_based = (flags & 1) != 0, .active = (flags & 2) != 0, .points = coop_object_read32(p + 8), .round = coop_object_read32(p + 12), .generation = coop_object_read32(p + 16) };
    memcpy(s.actor.bytes, p + 20, sizeof(s.actor.bytes));
    s.order_count = p[37];
    for (unsigned i = 0; i < COOP_COMBAT_ORDER_MAX; i++) memcpy(s.order[i].bytes, p + 38 + i * 17, 17);
    if (!coop_combat_state_valid(&s)) return false;
    *out = s;
    return true;
}
static bool request_valid(const CoopCombatRequest* r)
{
    if (!r || !r->epoch || !r->sequence || r->map > INT32_MAX || !coop_identity_valid(r->actor)
        || r->operation < COOP_COMBAT_ATTACK || r->operation > COOP_COMBAT_IMMORTAL
        || r->generation == UINT32_MAX || !instance_id(&r->target, r->operation != COOP_COMBAT_ATTACK)) return false;
    if (r->operation == COOP_COMBAT_ATTACK) return r->target.bytes[0] != 0 && !(r->value & ~COOP_ATTACK_VALUE_MASK);
    return r->value <= (r->operation == COOP_COMBAT_IMMORTAL ? 7 : r->operation == COOP_COMBAT_POLICY ? 2 : 1) && !r->target.bytes[0]
        && (r->operation != COOP_COMBAT_END_TURN || !r->value);
}
bool coop_combat_request_encode(const CoopCombatRequest* r, uint8_t out[COOP_COMBAT_REQUEST_BYTES])
{
    if (!out || !request_valid(r)) return false;
    coop_object_write32(out, r->epoch);
    coop_object_write32(out + 4, r->sequence);
    coop_object_write32(out + 8, r->map);
    memcpy(out + 12, r->actor, sizeof(r->actor));
    out[28] = (uint8_t)r->operation;
    out[29] = r->value;
    memcpy(out + 30, r->target.bytes, sizeof(r->target.bytes));
    coop_object_write32(out + 47, r->generation);
    return true;
}
bool coop_combat_request_decode(const void* data, size_t size, CoopCombatRequest* out)
{
    if (!data || !out || size != COOP_COMBAT_REQUEST_BYTES) return false;
    const uint8_t* p = data;
    CoopCombatRequest r = { .epoch = coop_object_read32(p), .sequence = coop_object_read32(p + 4), .map = coop_object_read32(p + 8), .operation = (CoopCombatOperation)p[28], .value = p[29], .generation = coop_object_read32(p + 47) };
    memcpy(r.actor, p + 12, sizeof(r.actor));
    memcpy(r.target.bytes, p + 30, sizeof(r.target.bytes));
    if (!request_valid(&r)) return false;
    *out = r;
    return true;
}
