#include "coop/engine_combat.h"
#include "coop/authority.h"
#include "coop/engine_object.h"
#include "game/combat.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include <string.h>
bool coop_engine_combat_capture(CoopCombatState* state)
{
    CombatCampaignState native;
    if (!state || !combat_campaign_state_get(&native)) return false;
    *state = (CoopCombatState) { .turn_based = native.turn_based, .active = native.active, .points = (uint32_t)native.points, .round = native.round, .generation = native.generation };
    if (native.active && !coop_object_id_from_engine(obj_get_id(native.actor), &state->actor)) return false;
    state->order_count = (uint8_t)native.order_count;
    for (unsigned i = 0; i < native.order_count; i++)
        if (!coop_object_id_from_engine(obj_get_id(native.order[i]), &state->order[i])) return false;
    return coop_combat_state_valid(state);
}
static bool same_native_state(const CoopCombatState* state)
{
    CoopCombatState current;
    uint8_t first[COOP_COMBAT_STATE_BYTES], second[COOP_COMBAT_STATE_BYTES];
    return coop_engine_combat_capture(&current) && coop_combat_state_encode(&current, first)
        && coop_combat_state_encode(state, second) && !memcmp(first, second, sizeof(first));
}
bool coop_engine_combat_resolve(const CoopCombatState* state, const ObjectID* removed, unsigned count)
{
    if (!coop_combat_state_valid(state)) return false;
    if (coop_authority_get() != COOP_AUTHORITY_REPLICA) return same_native_state(state);
    if (!state->active) return true;
    for (unsigned n = 0; n <= state->order_count; n++) {
        const CoopObjectId* ref = n ? &state->order[n - 1] : &state->actor;
        ObjectID id = coop_object_id_to_engine(*ref);
        for (unsigned i = 0; i < count; i++) if (objid_is_equal(id, removed[i])) return false;
        if (ref->bytes[0] == 3) {
            int64_t max_x, max_y;
            location_limits_get(&max_x, &max_y);
            if (coop_object_read32(ref->bytes + 1) >= (uint64_t)max_x || coop_object_read32(ref->bytes + 5) >= (uint64_t)max_y
                || coop_object_read32(ref->bytes + 13) != (uint32_t)map_current_map()) return false;
        }
        int64_t actor = obj_pool_perm_lookup(id);
        if (!actor || !obj_type_is_critter(obj_field_int32_get(actor, OBJ_F_TYPE))) return false;
    }
    return true;
}
bool coop_engine_combat_install(const CoopCombatState* state)
{
    if (!coop_engine_combat_resolve(state, NULL, 0)) return false;
    if (coop_authority_get() != COOP_AUTHORITY_REPLICA) return true;
    CombatCampaignState native = { .turn_based = state->turn_based, .active = state->active, .points = (int32_t)state->points, .round = state->round, .generation = state->generation };
    if (state->active) native.actor = obj_pool_perm_lookup(coop_object_id_to_engine(state->actor));
    native.order_count = state->order_count;
    for (unsigned i = 0; i < state->order_count; i++) native.order[i] = obj_pool_perm_lookup(coop_object_id_to_engine(state->order[i]));
    return combat_campaign_state_restore(&native);
}
