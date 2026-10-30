#include "coop/runtime.h"
#include "coop/engine_use.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_magic.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/dialog.h"
#include "game/item.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/mt_item.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/spell.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/trap.h"
#include "ui/dialog_ui.h"
#include <string.h>

static CoopUseTicket* tickets[COOP_MAX_PLAYERS];
static unsigned next_token;
static int64_t lookup(ObjectID id)
{
    int64_t obj = obj_pool_perm_lookup(id);
    return obj && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)) ? obj : OBJ_HANDLE_NULL;
}
static bool live(CoopUseTicket* ticket)
{
    AnimRunInfo* run;
    return ticket && ticket->token && anim_id_to_run_info(&ticket->goal, &run)
        && run->goals[0].type == (ticket->tile ? AG_USE_ITEM_ON_TILE : AG_USE_ITEM_ON_OBJECT)
        && (unsigned)run->goals[0].params[AGDATA_SKILL_DATA].data == ticket->token;
}
void coop_engine_use_release(CoopUseTicket* ticket)
{
    if (!ticket) return;
    if (!ticket->done && live(ticket)) {
        int64_t actor = lookup(ticket->actor);
        if (actor) sub_424070(actor, PRIORITY_3, false, true);
    }
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (tickets[i] == ticket) tickets[i] = NULL;
    memset(ticket, 0, sizeof(*ticket));
}
CoopMagicStatus coop_engine_use_default(int64_t actor, int64_t item, TargetDescriptor* target, bool force)
{
    if (!coop_authority_can_simulate()) return COOP_MAGIC_UNAVAILABLE;
    unsigned flags = obj_field_int32_get(item, OBJ_F_ITEM_FLAGS);
    if (!obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE) && !(flags & OIF_IS_MAGICAL)) return COOP_MAGIC_OK;
    int spell = mt_item_spell(item, 0);
    if (spell < 0 || spell >= MT_SPELL_COUNT || !magictech_is_enabled(spell)) return COOP_MAGIC_SOURCE;
    /* Native invocation uses the item's actual parent. The caller stops an
     * old default action if SAP_USE has consumed or reparented the item. */
    int64_t parent;
    if (!item_parent(item, &parent) || !obj_handle_is_valid(parent) || !obj_type_is_critter(obj_field_int32_get(parent, OBJ_F_TYPE))) return COOP_MAGIC_SOURCE;
    actor = parent;
    if (!magictech_can_charge_spell_fatigue(item, spell)) return COOP_MAGIC_REFUSED;
    if (!target->is_loc && target->obj && (!obj_handle_is_valid(target->obj)
            || (obj_field_int32_get(target->obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)))) return COOP_MAGIC_TARGET;
    if (!coop_engine_magic_target_valid(actor, item, spell, force, target)) return COOP_MAGIC_TARGET;
    int64_t loc = target->is_loc ? target->loc : obj_field_int64_get(target->obj, OBJ_F_LOCATION);
    bool inventory = !target->is_loc && (obj_field_int32_get(target->obj, OBJ_F_FLAGS) & OF_INVENTORY);
    if (!inventory && location_dist(obj_field_int64_get(actor, OBJ_F_LOCATION), loc) > magictech_get_range(spell)) return COOP_MAGIC_DISTANCE;
    if (!sub_45A030(spell)) {
        MagicTechInvocation invocation;
        magictech_invocation_init(&invocation, item, spell);
        if (target->is_loc) invocation.target_loc = target->loc;
        else sub_4440E0(target->obj, &invocation.target_obj);
        if (!magictech_check_los(&invocation)) return COOP_MAGIC_LINE_OF_SIGHT;
    }
    return COOP_MAGIC_OK;
}
unsigned coop_engine_use_execute(int64_t actor, int64_t item, const TargetDescriptor* target, bool force, bool own_skill, CoopUseTicket* ticket)
{
    int64_t parent;
    if (!obj_type_is_item(obj_field_int32_get(item, OBJ_F_TYPE)) || !item_parent(item, &parent) || parent != actor) return COOP_MAGIC_SOURCE;
    unsigned flags = obj_field_int32_get(item, OBJ_F_ITEM_FLAGS);
    if (!(flags & (OIF_CAN_USE_BOX | OIF_USE_IS_THROW))) return COOP_MAGIC_SOURCE;
    AnimID current, fidget;
    if (sub_423300(actor, &current)
        && !(anim_is_current_goal_type(actor, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget))) return COOP_MAGIC_BUSY;
    if (!combat_check_action_points(actor, 4)) return COOP_MAGIC_REFUSED;
    bool none = !target->is_loc && !target->obj;
    if (none && (flags & (OIF_NEEDS_TARGET | OIF_USE_IS_THROW))) return COOP_MAGIC_TARGET;
    if (!own_skill && !target->is_loc && target->obj && object_is_lockable(target->obj)
        && obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_GENERIC
        && (obj_field_int32_get(item, OBJ_F_GENERIC_FLAGS) & OGF_IS_LOCKPICK)
        && !(flags & OIF_IS_MAGICAL) && !obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE)) {
        if (object_dist(actor, target->obj) > 30) return COOP_MAGIC_DISTANCE;
        SkillInvocation invocation; skill_invocation_init(&invocation);
        invocation.skill = SKILL_PICK_LOCKS;
        sub_4440E0(actor, &invocation.source); sub_4440E0(target->obj, &invocation.target); sub_4440E0(item, &invocation.item);
        skill_pick_best_follower(&invocation);
        if (invocation.source.obj != actor) {
            int64_t helper = invocation.source.obj;
            if (!combat_check_action_points(helper, 4)) return COOP_MAGIC_REFUSED;
            int64_t tool = item_find_first_generic(helper, OGF_IS_LOCKPICK);
            bool started = tool ? item_use_on_obj_authoritative(helper, tool, target->obj, force) == COOP_MAGIC_OK
                : anim_goal_use_item_on_obj_with_skill(helper, OBJ_HANDLE_NULL, target->obj, SKILL_PICK_LOCKS, 0);
            if (!started) return COOP_MAGIC_REFUSED;
            char line[1000]; dialog_copy_npc_let_me_handle_msg(helper, actor, line);
            dialog_ui_float_line(helper, actor, line, 0);
            return COOP_MAGIC_OK;
        }
    }
    if (obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_SCROLL
        && spell_min_intelligence(obj_field_int32_get(item, OBJ_F_ITEM_SPELL_1)) > stat_level_get(actor, STAT_INTELLIGENCE)) return COOP_MAGIC_REFUSED;
    if (flags & OIF_USE_IS_THROW) {
        int64_t loc = target->is_loc ? target->loc : obj_field_int64_get(target->obj, OBJ_F_LOCATION);
        return anim_goal_throw_item(actor, item, loc) ? COOP_MAGIC_OK : COOP_MAGIC_REFUSED;
    }
    bool approach = !none && (trap_is_trap_device(item) || (flags & OIF_NO_RANGED_USE));
    int spell = mt_item_spell(item, 0);
    bool magical = obj_field_int32_get(item, OBJ_F_ITEM_SPELL_MANA_STORE) || (flags & OIF_IS_MAGICAL);
    if (!none && !target->is_loc && magical && spell >= 0 && spell < MT_SPELL_COUNT
        && !(obj_field_int32_get(target->obj, OBJ_F_FLAGS) & OF_INVENTORY)
        && object_dist(actor, target->obj) > magictech_get_range(spell)) approach = true;
    if (!approach) return target->is_loc ? (unsigned)item_use_on_loc_authoritative(actor, item, target->loc, force)
                                         : (unsigned)item_use_on_obj_authoritative(actor, item, target->obj, force);
    if (!ticket || next_token == INT32_MAX) return COOP_MAGIC_UNAVAILABLE;
    coop_engine_use_release(ticket);
    unsigned slot = 0;
    while (slot < COOP_MAX_PLAYERS && tickets[slot]) slot++;
    if (slot == COOP_MAX_PLAYERS) return COOP_MAGIC_BUSY;
    ticket->token = ++next_token; ticket->map = (unsigned)map_current_map(); ticket->force = force;
    ticket->generation = combat_turn_generation_get();
    ticket->actor = obj_get_id(actor); ticket->item = obj_get_id(item); ticket->tile = target->is_loc;
    if (target->is_loc) ticket->location = target->loc;
    else ticket->target = obj_get_id(target->obj);
    tickets[slot] = ticket;
    bool queued = target->is_loc ? anim_goal_use_item_on_loc_tagged(actor, target->loc, item, 0, ticket->token, &ticket->goal)
                                 : anim_goal_use_item_on_obj_tagged(actor, target->obj, item, 0, ticket->token, &ticket->goal);
    if (!queued) { tickets[slot] = NULL; memset(ticket, 0, sizeof(*ticket)); return COOP_MAGIC_REFUSED; }
    return COOP_USE_PENDING;
}
void coop_engine_use_reached(unsigned token, const AnimID* goal, int64_t actor, int64_t item, int64_t target, int64_t location, bool tile)
{
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        CoopUseTicket* ticket = tickets[i];
        if (!ticket || ticket->token != token || ticket->dispatched || ticket->done) continue;
        /* Claim the callback before entering a native script, which can pump
         * engine work or otherwise reenter the completion boundary. */
        CoopGameplay* borrowing = coop_runtime_gameplay();
        coop_gameplay_retain(borrowing);
        ticket->dispatched = true;
        unsigned status;
        AnimID incoming = *goal;
        if (!coop_authority_can_simulate() || ticket->map != (unsigned)map_current_map()) status = COOP_MAGIC_UNAVAILABLE;
        else if (ticket->generation != combat_turn_generation_get()) status = COOP_MAGIC_STALE;
        else if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != actor) status = COOP_MAGIC_TURN;
        else if (!live(ticket) || !anim_id_is_equal(&ticket->goal, &incoming) || ticket->tile != tile) status = COOP_MAGIC_REFUSED;
        else if (lookup(ticket->actor) != actor || lookup(ticket->item) != item || !critter_is_active(actor)) status = COOP_MAGIC_SOURCE;
        else if (tile ? ticket->location != location : lookup(ticket->target) != target) status = COOP_MAGIC_TARGET;
        else status = tile ? (unsigned)item_use_on_loc_authoritative(actor, item, location, ticket->force)
                           : (unsigned)item_use_on_obj_authoritative(actor, item, target, ticket->force);
        /* A native script may have torn down its gameplay context. */
        if (tickets[i] == ticket && ticket->token == token) { ticket->status = status; ticket->done = true; }
        coop_gameplay_release(borrowing);
        return;
    }
}
unsigned coop_engine_use_poll(CoopUseTicket* ticket, bool expired)
{
    if (ticket->done) return ticket->status;
    int64_t actor = lookup(ticket->actor), item = lookup(ticket->item), parent;
    bool owned = item && item_parent(item, &parent) && parent == actor;
    unsigned reason = COOP_MAGIC_OK;
    if (ticket->map != (unsigned)map_current_map()) reason = COOP_MAGIC_UNAVAILABLE;
    else if (ticket->generation != combat_turn_generation_get()) reason = COOP_MAGIC_STALE;
    else if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != actor) reason = COOP_MAGIC_TURN;
    else if (!owned) reason = COOP_MAGIC_SOURCE;
    else if (!live(ticket) || !actor || !critter_is_active(actor) || expired) reason = COOP_MAGIC_REFUSED;
    if (reason) {
        if (live(ticket) && actor && !sub_424070(actor, PRIORITY_3, false, true)) return COOP_USE_FAILURE;
        ticket->status = reason;
        ticket->done = true;
        return ticket->status;
    }
    return COOP_USE_PENDING;
}
static bool actor_settled(int64_t actor)
{
    const int goals[] = { AG_USE_OBJECT, AG_OPEN_DOOR, AG_CLOSE_DOOR, AG_ATTEMPT_OPEN_DOOR, AG_USE_ITEM_ON_OBJECT, AG_USE_ITEM_ON_OBJECT_WITH_SKILL, AG_USE_ITEM_ON_TILE, AG_USE_ITEM_ON_TILE_WITH_SKILL, AG_THROW_ITEM,
        AG_USE_SKILL_ON, AG_ATTEMPT_USE_SKILL_ON, AG_ATTEMPT_USE_PICKPOCKET_SKILL_ON, AG_USE_PICKLOCK_SKILL_ON };
    for (int slot = anim_find_first(actor); slot != -1; slot = anim_find_next(slot, actor))
        for (unsigned j = 0; j < sizeof(goals) / sizeof(goals[0]); j++)
            if (anim_run_info[slot].goals[0].type == goals[j]) return false;
    return true;
}
bool coop_engine_use_settled(void)
{
    if (!coop_authority_can_simulate()) return true;
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t actor = coop_campaign_object(i);
        if (!actor_settled(actor)) return false;
        ObjectList followers; object_list_all_followers(actor, &followers);
        bool settled = true;
        for (ObjectNode* node = followers.head; node; node = node->next)
            if (critter_pc_leader_get(node->obj) == actor && !actor_settled(node->obj)) { settled = false; break; }
        object_list_destroy(&followers);
        if (!settled) return false;
    }
    return true;
}
