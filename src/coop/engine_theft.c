#include "coop/runtime.h"
#include "coop/engine_theft.h"
#include "coop/authority.h"
#include "coop/items.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/skill.h"
#include <stdio.h>
#include <string.h>

static CoopTheftTicket* tickets[COOP_MAX_PLAYERS];
static unsigned next_token;
static int64_t lookup(ObjectID id)
{
    int64_t obj = obj_pool_perm_lookup(id);
    return obj && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)) ? obj : OBJ_HANDLE_NULL;
}
static bool live(CoopTheftTicket* ticket)
{
    AnimRunInfo* run;
    if (!ticket || !ticket->token || !anim_id_to_run_info(&ticket->goal, &run)
        || (unsigned)run->goals[0].params[AGDATA_SCRATCH_VAL5].data != ticket->token
        || run->goals[0].params[AGDATA_SKILL_DATA].data != SKILL_PICK_POCKET) return false;
    return run->goals[0].type == AG_USE_SKILL_ON || run->goals[0].type == AG_ATTEMPT_USE_PICKPOCKET_SKILL_ON;
}
void coop_engine_theft_release(CoopTheftTicket* ticket)
{
    if (!ticket) return;
    if (!ticket->done && live(ticket)) {
        int64_t actor = lookup(ticket->actor);
        if (actor) sub_424070(actor, PRIORITY_3, false, true);
    }
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (tickets[i] == ticket) tickets[i] = NULL;
    memset(ticket, 0, sizeof(*ticket));
}
unsigned coop_engine_theft_start(CoopTheftTicket* ticket, int64_t actor, int64_t target, int64_t item, bool plant, CoopTheftAllowed allowed, void* context)
{
    if (!ticket || !actor || !target || !item || !allowed || !coop_authority_can_simulate()) return COOP_ITEM_UNAVAILABLE;
    AnimID current, fidget;
    if (sub_423300(actor, &current) && !(anim_is_current_goal_type(actor, AG_ANIM_FIDGET, &fidget) && anim_id_is_equal(&current, &fidget))) return COOP_ITEM_REFUSED;
    if (next_token == INT32_MAX || !combat_check_action_points(actor, 4) || !allowed(context)) return COOP_ITEM_REFUSED;
    coop_engine_theft_release(ticket);
    unsigned slot = 0; while (slot < COOP_MAX_PLAYERS && tickets[slot]) slot++;
    if (slot == COOP_MAX_PLAYERS) return COOP_ITEM_REFUSED;
    ticket->actor = obj_get_id(actor); ticket->target = obj_get_id(target); ticket->item = obj_get_id(item);
    ticket->token = ++next_token; ticket->map = (unsigned)map_current_map(); ticket->generation = combat_turn_generation_get();
    ticket->plant = plant; ticket->allowed = allowed; ticket->context = context; tickets[slot] = ticket;
    if (!anim_goal_use_skill_on_tagged(actor, target, item, SKILL_PICK_POCKET, plant ? 1u : 0u, ticket->token, &ticket->goal)) {
        tickets[slot] = NULL; memset(ticket, 0, sizeof(*ticket)); return COOP_ITEM_REFUSED;
    }
    fprintf(stderr, "ARCMP theft admitted token=%u root-live=%d generation=%u plant=%d\n", ticket->token, live(ticket), ticket->generation, plant);
    return COOP_THEFT_PENDING;
}
bool coop_engine_theft_reached(unsigned token, const AnimID* goal, int64_t actor, int64_t target, int64_t item, bool plant)
{
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        CoopTheftTicket* ticket = tickets[i];
        if (!ticket || ticket->token != token || ticket->done || ticket->dispatched) continue;
        CoopGameplay* borrowing = coop_runtime_gameplay();
        coop_gameplay_retain(borrowing);
        ticket->dispatched = true;
        AnimID incoming = *goal;
        unsigned status = COOP_ITEM_INTERRUPTED; bool success = false;
        int64_t parent;
        if (coop_authority_can_simulate() && ticket->map == (unsigned)map_current_map() && ticket->generation == combat_turn_generation_get()
            && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == actor)
            && live(ticket) && anim_id_is_equal(&ticket->goal, &incoming) && ticket->plant == plant
            && lookup(ticket->actor) == actor && lookup(ticket->target) == target && lookup(ticket->item) == item
            && critter_is_active(actor) && item_parent(item, &parent) && parent == (plant ? actor : target) && ticket->allowed(ticket->context)) {
            status = COOP_ITEM_REFUSED;
            if (combat_consume_action_points(actor, 4)) {
                SkillInvocation invocation; skill_invocation_init(&invocation);
                invocation.skill = SKILL_PICK_POCKET;
                sub_4440E0(actor, &invocation.source); sub_4440E0(target, &invocation.target); sub_4440E0(item, &invocation.item);
                if (plant) invocation.flags = SKILL_INVOCATION_0x02;
                if (skill_invocation_run(&invocation)) { status = 0; success = (invocation.flags & SKILL_INVOCATION_SUCCESS) != 0; }
            }
        }
        if (tickets[i] == ticket && ticket->token == token) { ticket->status = status; ticket->success = success; ticket->done = true; }
        coop_gameplay_release(borrowing);
        return success;
    }
    return false;
}
unsigned coop_engine_theft_poll(CoopTheftTicket* ticket, bool expired)
{
    if (ticket->done) return ticket->status;
    int64_t actor = lookup(ticket->actor);
    bool native_live = live(ticket), allowed = ticket->allowed(ticket->context);
    if (!coop_authority_can_simulate() || expired || !native_live || !actor || !critter_is_active(actor)
        || ticket->map != (unsigned)map_current_map() || ticket->generation != combat_turn_generation_get()
        || (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != actor) || !allowed) {
        AnimRunInfo* run = NULL; anim_id_to_run_info(&ticket->goal, &run);
        fprintf(stderr, "ARCMP theft cancelled token=%u expired=%d live=%d root=%d actor=%d allowed=%d generation=%u/%u map=%u/%u\n",
            ticket->token, expired, native_live, run ? run->goals[0].type : -1, actor != 0, allowed, ticket->generation, combat_turn_generation_get(), ticket->map, (unsigned)map_current_map());
        if (actor && live(ticket) && !sub_424070(actor, PRIORITY_3, false, true)) return COOP_THEFT_FAILURE;
        ticket->status = COOP_ITEM_INTERRUPTED; ticket->done = true;
        return ticket->status;
    }
    return COOP_THEFT_PENDING;
}
