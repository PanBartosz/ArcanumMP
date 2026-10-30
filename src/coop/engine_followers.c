#include "coop/engine_followers.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_object.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/broadcast.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/tile.h"
#include "ui/dialog_ui.h"
#include <string.h>
unsigned coop_engine_follower_order(int64_t actor, const CoopFollowerRequest* r)
{
    if (!coop_authority_can_simulate() || !actor || !critter_is_active(actor)) return COOP_FOLLOW_UNAVAILABLE;
    if (r->generation != combat_turn_generation_get()) return COOP_FOLLOW_STALE;
    if (r->order == COOP_FOLLOW_BROADCAST) {
        int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
        if (r->x >= max_x || r->y >= max_y) return COOP_FOLLOW_TARGET;
        int64_t target = r->target.bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(r->target)) : OBJ_HANDLE_NULL;
        if (target && (obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) target = OBJ_HANDLE_NULL;
        Broadcast message = { .loc = location_make(r->x, r->y) }; memcpy(message.str, r->command, sizeof(message.str));
        broadcast_coop(actor, &message, target); return COOP_FOLLOW_OK;
    }
    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(r->follower));
    if (!npc || obj_field_int32_get(npc, OBJ_F_TYPE) != OBJ_TYPE_NPC || !critter_is_active(npc)
        || (obj_field_int32_get(npc, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return COOP_FOLLOW_INVALID;
    if (critter_pc_leader_get(npc) != actor) return COOP_FOLLOW_OWNER;
    if (object_dist(actor, npc) > 10) return COOP_FOLLOW_DISTANCE;
    if (r->order == COOP_FOLLOW_INVENTORY) {
        if (!coop_engine_dialogue_can_start(actor)) return COOP_FOLLOW_BUSY;
        return dialog_ui_coop_companion_inventory(actor, npc) ? COOP_FOLLOW_OK : COOP_FOLLOW_REFUSED;
    }
    /* Movement/attacks use the native NPC's turn and AP. Orders cannot make
     * an AI companion act during someone else's turn. */
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != npc) return COOP_FOLLOW_TURN;
    bool controlled = (obj_field_int32_get(npc, OBJ_F_SPELL_FLAGS) & OSF_MIND_CONTROLLED) != 0;
    if (r->order == COOP_FOLLOW_WALK) {
        int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
        int64_t location = location_make(r->x, r->y);
        if (r->x >= max_x || r->y >= max_y || object_dist(actor, npc) > 10 || location_dist(obj_field_int64_get(npc, OBJ_F_LOCATION), location) > 30
            || tile_is_blocking(location, false)) return COOP_FOLLOW_TARGET;
        if (obj_field_int64_get(npc, OBJ_F_LOCATION) == location) return COOP_FOLLOW_OK;
        int8_t rotations[128];
        PathCreateInfo path = { .obj = npc, .from = obj_field_int64_get(npc, OBJ_F_LOCATION), .to = location,
            .max_rotations = 128, .rotations = rotations };
        if (path_make(&path) <= 0) return COOP_FOLLOW_TARGET;
        return anim_goal_run_to_tile(npc, location) ? COOP_FOLLOW_OK : COOP_FOLLOW_REFUSED;
    }
    if (r->order == COOP_FOLLOW_ATTACK) {
        int64_t target = obj_pool_perm_lookup(coop_object_id_to_engine(r->target));
        if (!target || target == npc || coop_campaign_slot_for_object(target) >= 0
            || (obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return COOP_FOLLOW_TARGET;
        int type = obj_field_int32_get(target, OBJ_F_TYPE);
        if (obj_type_is_critter(type)) {
            if (critter_is_dead(target) || (!r->force && critter_party_same(actor, target))) return COOP_FOLLOW_TARGET;
        } else if (!r->force || (type != OBJ_TYPE_CONTAINER && type != OBJ_TYPE_SCENERY && type != OBJ_TYPE_PORTAL)) return COOP_FOLLOW_TARGET;
        if (object_dist(npc, target) > 30) return COOP_FOLLOW_DISTANCE;
        sub_4AA580(npc); ai_attack(npc, target, LOUDNESS_LOUD, 4); return COOP_FOLLOW_OK;
    }
    switch (r->order) {
    case COOP_FOLLOW_CLOSE: critter_stay_close(npc); break;
    case COOP_FOLLOW_SPREAD: critter_spread_out(npc); break;
    case COOP_FOLLOW_BACK: ai_stop_attacking(npc); break;
    case COOP_FOLLOW_WAIT:
        if (controlled) return COOP_FOLLOW_REFUSED;
        sub_424070(npc, PRIORITY_3, false, true); ai_npc_wait(npc); break;
    case COOP_FOLLOW_RESUME: {
        bool force = (obj_field_int32_get(npc, OBJ_F_NPC_FLAGS) & ONF_FORCED_FOLLOWER) != 0;
        if (ai_check_follow(npc, actor, force) != AI_FOLLOW_OK) return COOP_FOLLOW_REFUSED;
        ai_npc_unwait(npc, force); break;
    }
    case COOP_FOLLOW_DISMISS:
        if (controlled || !critter_disband(npc, true)) return COOP_FOLLOW_REFUSED;
        break;
    default: return COOP_FOLLOW_REFUSED;
    }
    ai_process(npc); return COOP_FOLLOW_OK;
}
