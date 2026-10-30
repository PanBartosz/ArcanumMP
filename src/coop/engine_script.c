#include "coop/engine_script.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"

static int64_t owner(int64_t object)
{
    for (unsigned depth = 0; object && depth < 16; depth++) {
        if (!obj_handle_is_valid(object)) return OBJ_HANDLE_NULL;
        if (coop_campaign_slot_for_object(object) >= 0) return object;
        int type = obj_field_int32_get(object, OBJ_F_TYPE);
        if (type == OBJ_TYPE_NPC) {
            int64_t leader = critter_pc_leader_get(object);
            return coop_campaign_slot_for_object(leader) >= 0 ? leader : OBJ_HANDLE_NULL;
        }
        if (!obj_type_is_item(type)) return OBJ_HANDLE_NULL;
        int64_t parent = OBJ_HANDLE_NULL;
        if (!item_parent(object, &parent) || parent == object) return OBJ_HANDLE_NULL;
        object = parent;
    }
    return OBJ_HANDLE_NULL;
}
int64_t coop_engine_script_player(const ScriptInvocation* invocation, int64_t inherited)
{
    if (!invocation || coop_authority_get() != COOP_AUTHORITY_HOST || !coop_campaign_active())
        return player_get_local_pc_obj();
    int64_t candidates[] = { invocation->triggerer_obj, invocation->attachee_obj, invocation->extra_obj, inherited };
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(*candidates); i++) {
        int64_t pc = owner(candidates[i]);
        if (pc) return pc;
    }
    int64_t anchor = invocation->attachee_obj ? invocation->attachee_obj : invocation->triggerer_obj;
    if (anchor && obj_handle_is_valid(anchor)) {
        int64_t pc = coop_campaign_nearest_player(anchor);
        if (pc) return pc;
    }
    return player_get_local_pc_obj();
}
