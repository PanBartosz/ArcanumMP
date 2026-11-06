#include "coop/engine_script.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/modern.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"

bool coop_engine_party_companion(int64_t npc, int64_t pc)
{
    if (!npc || !pc || !obj_handle_is_valid(npc) || !obj_handle_is_valid(pc)
        || obj_field_int32_get(npc,OBJ_F_TYPE)!=OBJ_TYPE_NPC) return false;
    if (critter_pc_leader_get(npc)==pc) return true;
    if (!coop_campaign_active() || coop_campaign_slot_for_object(pc)<0
        || coop_campaign_slot_for_object(critter_pc_leader_get(npc))<0
        || (obj_field_int32_get(npc,OBJ_F_FLAGS)&(OF_OFF|OF_DESTROYED|OF_EXTINCT))
        || (obj_field_int32_get(npc,OBJ_F_SPELL_FLAGS)&(OSF_MIND_CONTROLLED|OSF_SUMMONED|OSF_ILLUSION|OSF_FAMILIAR))) return false;
    return true;
}
bool coop_engine_party_lend(int64_t actor, int64_t holder)
{
    if (!actor || !holder || !obj_handle_is_valid(actor) || !obj_handle_is_valid(holder)
        || !coop_campaign_active() || coop_campaign_slot_for_object(actor)<0
        || (coop_campaign_slot_for_object(holder)<0 && !coop_engine_party_companion(holder,actor))
        || !critter_is_active(holder)) return false;
    unsigned radius=coop_play_shared_radius();
    return !radius || object_dist(actor,holder)<=radius;
}
static bool story_name(int name)
{
    /* Native script-visible NAMEs, not descriptions or prototype IDs. Generic
     * equipment/supply names (2000/2800/3600/5600 etc.) stay personal. The
     * numbered generic instances and documents are campaign quest resources. */
    if ((name>=5601 && name<=5700) || (name>=5201 && name<=5253)) return true;
    static const int names[]={2001,2002,2004,2005,2007,2013,2014,2015,2025,2030,2031,
        2801,2802,2803,2804,2806,2807,2808,2809,2810,2812,2813,2814,2816,2825,2826,
        2848,2851,2855,2864,3602,3603,3604,3605,3606,3607,3610,3611,3619};
    for (unsigned i=0;i<sizeof(names)/sizeof(*names);i++) if (names[i]==name) return true;
    return false;
}
int64_t coop_engine_story_item(int64_t actor, int name)
{
    int64_t item=item_find_by_name(actor,name);
    if (item || !actor || !coop_campaign_active() || coop_campaign_slot_for_object(actor)<0) return item;
    ObjectList team; object_list_team(actor,&team);
    /* Keep the speaker's own followers ahead of other humans. */
    ObjectList own; object_list_all_followers(actor,&own);
    for (ObjectNode* node=own.head; node && !item; node=node->next) {
        int64_t candidate=item_find_by_name(node->obj,name);
        if (candidate && coop_engine_party_lend(actor,node->obj)
            && (story_name(name) || obj_field_int32_get(candidate,OBJ_F_TYPE)==OBJ_TYPE_KEY)) item=candidate;
    }
    object_list_destroy(&own);
    for (ObjectNode* node=team.head; node; node=node->next) {
        if (item) break;
        if (node->obj==actor || !coop_engine_party_lend(actor,node->obj)) continue;
        int64_t candidate=item_find_by_name(node->obj,name);
        if (!candidate) continue;
        /* Script names are assigned to story instances. Ordinary equipment
         * shares the generic name/type IDs; those must never become a party
         * purse or a source of crafting ingredients. */
        int type=obj_field_int32_get(candidate,OBJ_F_TYPE);
        if (type==OBJ_TYPE_KEY || story_name(name)) { item=candidate; break; }
    }
    object_list_destroy(&team);
    return item;
}

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
