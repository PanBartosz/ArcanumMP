#include "coop/engine_skills.h"
#include "coop/authority.h"
#include "coop/engine_object.h"
#include "coop/items.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/dialog.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/skill.h"
#include "game/target.h"
#include "ui/dialog_ui.h"

static int64_t resolve(const CoopObjectId* id)
{
    if (id->bytes[0] == 3) {
        int64_t x, y; location_limits_get(&x, &y);
        if (coop_object_read32(id->bytes + 1) >= (uint64_t)x || coop_object_read32(id->bytes + 5) >= (uint64_t)y
            || coop_object_read32(id->bytes + 13) != (uint32_t)map_current_map()) return OBJ_HANDLE_NULL;
    }
    int64_t obj = obj_pool_perm_lookup(coop_object_id_to_engine(*id));
    return obj && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)) ? obj : OBJ_HANDLE_NULL;
}
CoopSkillStatus coop_engine_skills_execute(int64_t actor, const CoopSkillRequest* r, CoopItems* items, uint8_t sender)
{
    if (!actor || !r || !coop_authority_can_simulate()) return COOP_SKILL_UNAVAILABLE;
    if (r->generation != combat_turn_generation_get()) return COOP_SKILL_STALE;
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != actor) return COOP_SKILL_TURN;
    if (r->action == COOP_SKILL_CONCEAL) {
        if (critter_is_concealed(actor) != (r->value != 0)) critter_set_concealed(actor, r->value != 0);
        return COOP_SKILL_OK;
    }
    int64_t target = resolve(&r->target), parent;
    if (!target) return COOP_SKILL_TARGET;
    if (r->action == COOP_SKILL_REPAIR) {
        if (!obj_type_is_item(obj_field_int32_get(target, OBJ_F_TYPE))) return COOP_SKILL_TARGET;
        /* Repair selects from inventory already visible to this player. */
        if (!item_parent(target, &parent) || (parent != actor && !coop_items_has_access(items, sender, parent))) return COOP_SKILL_OWNERSHIP;
    } else if ((obj_field_int32_get(target, OBJ_F_FLAGS) & OF_INVENTORY)
        && (!item_parent(target, &parent) || parent != actor)) return COOP_SKILL_OWNERSHIP;
    TargetParams params = { .tgt = r->action == COOP_SKILL_REPAIR ? TGT_OBJ_INVEN | TGT_OBJECT : TGT_TILE | TGT_OBJECT };
    TargetContext ctx = {0}; target_context_init(&ctx, &params, actor);
    ctx.target_obj = ctx.orig_target_obj = target;
    if (!target_context_evaluate(&ctx)) return COOP_SKILL_TARGET;
    SkillInvocation invocation; skill_invocation_init(&invocation);
    sub_4440E0(actor, &invocation.source); sub_4440E0(target, &invocation.target);
    invocation.skill = r->action == COOP_SKILL_REPAIR ? SKILL_REPAIR : SKILL_DISARM_TRAPS;
    if (r->action == COOP_SKILL_DISARM) {
        if (!r->value) skill_pick_best_follower(&invocation);
        if (!combat_consume_action_points(actor, 4)) return COOP_SKILL_REFUSED;
        invocation.flags = SKILL_INVOCATION_0x04;
        if (invocation.source.obj != actor) {
            char line[1000]; dialog_copy_npc_let_me_handle_msg(invocation.source.obj, actor, line);
            dialog_ui_float_line(invocation.source.obj, actor, line, 0);
        }
    }
    /* Assistance uses owned NPCs and the requesting player's explicit modifier;
     * no other human's skills or host keyboard state enter the choice. */
    return skill_invocation_run(&invocation) ? COOP_SKILL_OK : COOP_SKILL_REFUSED;
}
