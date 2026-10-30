#include "coop/engine_rest.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_transition.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/sector.h"
#include "game/script.h"
#include "game/timeevent.h"
#include "game/townmap.h"
#include <string.h>
#include <stdio.h>
static bool bed_valid(int64_t bed)
{
    return bed && obj_handle_is_valid(bed) && obj_field_int32_get(bed, OBJ_F_TYPE) == OBJ_TYPE_SCENERY
        && !(obj_field_int32_get(bed, OBJ_F_FLAGS) & (OF_DESTROYED | OF_OFF | OF_EXTINCT))
        && tig_art_scenery_id_type_get(obj_field_int32_get(bed, OBJ_F_CURRENT_AID)) == TIG_ART_SCENERY_TYPE_BEDS;
}
unsigned coop_engine_rest_validate(int64_t actor, int64_t bed, unsigned choice)
{
    if (!coop_authority_can_simulate() || !coop_campaign_active() || choice > 7 || !actor || coop_campaign_slot_for_object(actor) < 0) return COOP_CONTROL_OWNER;
    if (!critter_is_active(actor) || critter_is_unconscious(actor)) return COOP_CONTROL_INCAPACITATED;
    if (bed && (!bed_valid(bed) || object_dist(actor, bed) > 3 || obj_field_handle_get(bed, OBJ_F_SCENERY_WHOS_IN_ME))) return COOP_CONTROL_LOCATION;
    bool waiting = false;
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (!pc || object_dist(actor, pc) > 8) return COOP_CONTROL_DISTANCE;
        if (!critter_is_dead(pc) && !combat_can_exit_combat_mode(pc)) return COOP_CONTROL_ENEMIES;
        if (!bed) {
            int town = townmap_get(sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION)));
            if (town != TOWNMAP_NONE) {
                if (!townmap_is_waitable(town)) return COOP_CONTROL_LOCATION;
                waiting = true;
            }
        }
    }
    return waiting && choice == 7 ? COOP_CONTROL_LOCATION : COOP_CONTROL_OK;
}
static bool add(CoopRest* rest, int64_t actor)
{
    if (!actor || !obj_handle_is_valid(actor)) return true;
    ObjectID id = obj_get_id(actor);
    for (unsigned i = 0; i < rest->count; i++) if (objid_is_equal(rest->actors[i], id)) return true;
    if (rest->count == COOP_REST_MAX_ACTORS) return false;
    rest->actors[rest->count++] = id; return true;
}
static bool rest_context(const CoopRest* rest)
{
    return coop_authority_can_simulate() && coop_runtime_phase() != COOP_RUNTIME_FAILED
        && rest->map == (unsigned)map_current_map() && rest->epoch == coop_runtime_epoch();
}
static bool rest_story_pending(void)
{
    return coop_engine_dialogue_pending() || coop_engine_transition_pending();
}
unsigned coop_engine_rest_begin(CoopRest* rest, int64_t actor, int64_t bed, unsigned choice, uint64_t now)
{
    if (!rest || rest->active) return COOP_CONTROL_BUSY;
    unsigned status = coop_engine_rest_validate(actor, bed, choice);
    if (status) return status;
    CoopRest r = { .choice = choice, .map = (unsigned)map_current_map(), .epoch = coop_runtime_epoch(),
        .next = now + 200, .initiator = obj_get_id(actor) };
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i); if (!add(&r, pc)) return COOP_CONTROL_BUSY;
        ObjectList list; object_list_all_followers(pc, &list);
        bool good = true;
        for (ObjectNode* node = list.head; node && good; node = node->next) good = add(&r, node->obj);
        object_list_destroy(&list);
        if (!good) return COOP_CONTROL_BUSY;
        if (!bed && townmap_get(sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION))) != TOWNMAP_NONE) r.waiting = true;
    }
    if (bed) {
        ObjectID actor_id = obj_get_id(actor), bed_id = obj_get_id(bed);
        ObjectCallbackState actor_state, bed_state;
        if (!object_callback_capture(&actor_state, actor) || !object_callback_capture(&bed_state, bed)) return COOP_CONTROL_LOCATION;
        /* Native inn/bed permission scripts run once, after party consent. */
        int denied = object_script_execute(actor, bed, OBJ_HANDLE_NULL, SAP_USE, 0);
        if (!rest_context(&r) || !object_callback_live(&actor_state) || !object_callback_live(&bed_state)
            || rest_story_pending()) return COOP_CONTROL_INTERRUPTED;
        actor = obj_pool_perm_lookup(actor_id); bed = obj_pool_perm_lookup(bed_id);
        if (denied == 1 || !actor || !bed_valid(bed) || !critter_enter_bed(actor, bed)) return COOP_CONTROL_LOCATION;
        r.bed = bed_id; r.using_bed = true;
    }
    r.active = true; *rest = r;
    fprintf(stderr, "ARCMP rest begin actors=%u choice=%u waiting=%d\n", rest->count, choice, rest->waiting);
    for (unsigned i = 0; i < rest->count; i++) {
        int64_t target = obj_pool_perm_lookup(rest->actors[i]);
        if (target && !critter_is_dead(target)) {
            if (combat_critter_is_combat_mode_active(target)) combat_critter_deactivate_combat_mode(target);
            magictech_demaintain_spells(target);
            if (!rest_context(rest)) { coop_engine_rest_end(rest); return COOP_CONTROL_INTERRUPTED; }
        }
    }
    /* Notify each NPC once, using its own leader as in native sleeping. */
    for (unsigned i = 0; i < rest->count; i++) {
        int64_t follower = obj_pool_perm_lookup(rest->actors[i]);
        if (!follower || obj_field_int32_get(follower, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        int64_t leader = critter_leader_get(follower);
        if (leader) object_script_execute(leader, follower, OBJ_HANDLE_NULL, SAP_LEADER_SLEEPING, 0);
        if (!rest_context(rest)) { coop_engine_rest_end(rest); return COOP_CONTROL_INTERRUPTED; }
    }
    if (rest_story_pending()) { coop_engine_rest_end(rest); return COOP_CONTROL_INTERRUPTED; }
    return COOP_CONTROL_OK;
}
static bool healed(const CoopRest* rest)
{
    for (unsigned i = 0; i < rest->count; i++) {
        int64_t actor = obj_pool_perm_lookup(rest->actors[i]);
        if (actor && !critter_is_dead(actor) && object_hp_damage_get(actor) > 0) return false;
    }
    return true;
}
unsigned coop_engine_rest_step(CoopRest* rest, uint64_t now, bool cancel, bool* complete)
{
    if (!rest || !complete || !rest->active || !coop_authority_can_simulate()) return COOP_CONTROL_UNAVAILABLE;
    *complete = false;
    if (cancel || !rest_context(rest) || rest_story_pending()) { *complete = true; return COOP_CONTROL_INTERRUPTED; }
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t pc = coop_campaign_object(i);
        if (!pc || (!critter_is_dead(pc) && !combat_can_exit_combat_mode(pc))) { *complete = true; return COOP_CONTROL_ENEMIES; }
    }
    if (now < rest->next) return COOP_CONTROL_OK;
    DateTime hour; sub_45A950(&hour, 3600000); timeevent_inc_datetime(&hour);
    if (!rest_context(rest) || rest_story_pending()) { *complete = true; return COOP_CONTROL_INTERRUPTED; }
    if (!rest->waiting) for (unsigned i = 0; i < rest->count; i++) {
        int64_t actor = obj_pool_perm_lookup(rest->actors[i]);
        if (actor) {
            critter_resting_heal(actor, rest->using_bed ? 2 : 1);
            if (!rest_context(rest) || rest_story_pending()) { *complete = true; return COOP_CONTROL_INTERRUPTED; }
        }
    }
    rest->completed++; rest->next = now + 200;
    static const unsigned hours[] = { 1, 2, 4, 8, 24 };
    if (rest->choice < 5) *complete = rest->completed >= hours[rest->choice];
    else if (rest->choice == 5) *complete = datetime_current_hour() == 7;
    else if (rest->choice == 6) *complete = datetime_current_hour() == 20;
    else *complete = healed(rest);
    if (!*complete && rest->completed >= 168) { *complete = true; return COOP_CONTROL_INTERRUPTED; }
    return COOP_CONTROL_OK;
}
void coop_engine_rest_end(CoopRest* rest)
{
    if (!rest || !rest->active) return;
    if (coop_authority_can_simulate() && rest->using_bed) {
        int64_t actor = obj_pool_perm_lookup(rest->initiator), bed = obj_pool_perm_lookup(rest->bed);
        if (actor && bed_valid(bed) && obj_field_handle_get(bed, OBJ_F_SCENERY_WHOS_IN_ME) == actor) critter_leave_bed(actor, bed);
    }
    rest->active = false;
}
