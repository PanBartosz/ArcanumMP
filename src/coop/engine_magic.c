#include "coop/engine_magic.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_teleport.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/mt_item.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/player.h"
#include "game/object.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/target.h"
#include "game/trap.h"
#include <stdlib.h>
#include <string.h>

static CoopMagicView presentation;
static int token_order(const void* a, const void* b)
{
    uint64_t x = ((const CoopMagicEntry*)a)->token, y = ((const CoopMagicEntry*)b)->token;
    return (x > y) - (x < y);
}
bool coop_engine_magic_capture(CoopMagicView* view)
{
    if (!view) return false;
    if (coop_authority_get() == COOP_AUTHORITY_REPLICA) {
        *view = presentation;
        return true;
    }
    memset(view, 0, sizeof(*view));
    coop_engine_teleport_capture(view->choices);
    if (!magictech_run_info || !coop_campaign_active()) return true;
    for (unsigned i = 0; i < COOP_MAGIC_RUN_LIMIT; i++) {
        MagicTechRunInfo* run = &magictech_run_info[i];
        if (!(run->flags & MAGICTECH_RUN_ACTIVE) || run->action != MAGICTECH_ACTION_MAINTAIN
            || run->spell < 0 || run->spell >= MT_SPELL_COUNT || magictech_is_tech(run->spell)
            || magictech_get_maintenance(run->spell)->period <= 0) continue;
        int member = coop_campaign_slot_for_object(run->parent_obj.obj);
        if (member < 0) continue;
        const CoopCampaignMember* pc = coop_campaign_member((unsigned)member);
        CoopMagicEntry* e = &view->entries[view->count++];
        e->token = magictech_run_token(i);
        e->spell = (uint16_t)run->spell;
        memcpy(e->owner, pc->object_id.d.g.data, 16);
        int64_t target = run->target_obj.obj;
        if (!target && run->summoned_obj) target = run->summoned_obj->obj;
        if (target && obj_handle_is_valid(target) && !coop_object_id_from_engine(obj_get_id(target), &e->target)) return false;
    }
    qsort(view->entries, view->count, sizeof(view->entries[0]), token_order);
    return coop_magic_view_valid(view);
}
static bool same_view(const CoopMagicView* view)
{
    CoopMagicView native;
    if (!coop_engine_magic_capture(&native) || native.count != view->count) return false;
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) {
        const CoopMagicChoice *a = &native.choices[i], *b = &view->choices[i];
        if (a->token != b->token || a->map != b->map || a->spell != b->spell || a->charge != b->charge || memcmp(a->actor, b->actor, 16)) return false;
    }
    for (unsigned i = 0; i < view->count; i++) {
        const CoopMagicEntry *a = &native.entries[i], *b = &view->entries[i];
        if (a->token != b->token || a->spell != b->spell || memcmp(a->owner, b->owner, 16)
            || memcmp(&a->target, &b->target, sizeof(a->target))) return false;
    }
    return true;
}
static bool placement(const CoopObjectId* id)
{
    if (id->bytes[0] != 3) return true;
    int64_t x, y;
    location_limits_get(&x, &y);
    return coop_object_read32(id->bytes + 1) < (uint64_t)x && coop_object_read32(id->bytes + 5) < (uint64_t)y
        && coop_object_read32(id->bytes + 13) == (uint32_t)map_current_map();
}
bool coop_engine_magic_resolve(const CoopMagicView* view, const ObjectID* removed, unsigned count)
{
    if (!coop_magic_view_valid(view)) return false;
    if (coop_authority_get() != COOP_AUTHORITY_REPLICA) return same_view(view);
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) if (view->choices[i].token) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (!member || memcmp(member->object_id.d.g.data, view->choices[i].actor, 16)
            || view->choices[i].map != (uint32_t)map_current_map() || !magictech_is_enabled(view->choices[i].spell)) return false;
    }
    for (unsigned i = 0; i < view->count; i++) {
        const CoopMagicEntry* e = &view->entries[i];
        ObjectID owner = { .type = OID_TYPE_GUID };
        memcpy(owner.d.g.data, e->owner, 16);
        if (coop_campaign_slot_for_object(obj_pool_perm_lookup(owner)) < 0 || !placement(&e->target)) return false;
        if (!e->target.bytes[0]) continue;
        ObjectID target = coop_object_id_to_engine(e->target);
        for (unsigned j = 0; j < count; j++)
            if (objid_is_equal(target, removed[j])) return false;
        if (!obj_pool_perm_lookup(target)) return false;
    }
    return true;
}
bool coop_engine_magic_install(const CoopMagicView* view)
{
    if (!coop_engine_magic_resolve(view, NULL, 0)) return false;
    if (coop_authority_get() == COOP_AUTHORITY_REPLICA) presentation = *view;
    return true;
}
const CoopMagicView* coop_engine_magic_view(void)
{
    if (coop_authority_get() != COOP_AUTHORITY_REPLICA && !coop_engine_magic_capture(&presentation))
        memset(&presentation, 0, sizeof(presentation));
    return &presentation;
}
void coop_engine_magic_reset(void) { memset(&presentation, 0, sizeof(presentation)); }
static int64_t resolve(const CoopObjectId* id, bool inventory)
{
    if (!id->bytes[0] || !placement(id)) return OBJ_HANDLE_NULL;
    int64_t obj = obj_pool_perm_lookup(coop_object_id_to_engine(*id));
    return obj && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT | (inventory ? 0 : OF_INVENTORY))) ? obj : OBJ_HANDLE_NULL;
}
static bool source_knows(int64_t pc, int64_t source, int spell)
{
    if (source == pc) return spell < SPELL_COUNT && spell_is_known(pc, spell);
    int64_t parent;
    if (!obj_type_is_item(obj_field_int32_get(source, OBJ_F_TYPE)) || !item_parent(source, &parent) || parent != pc) return false;
    for (int i = 0; i < 5; i++)
        if (mt_item_spell(source, i) == spell && !sub_45A030(spell)) return true;
    return false;
}
bool coop_engine_magic_target_valid(int64_t pc, int64_t source, int spell, bool force, TargetDescriptor* td)
{
    uint64_t* flags;
    sub_459F20(spell, &flags);
    if (*flags == TGT_NONE || ((*flags & TGT_SELF) && !(*flags & TGT_TILE)) || *flags == TGT_OBJ_RADIUS) {
        target_descriptor_set_loc(td, obj_field_int64_get(pc, OBJ_F_LOCATION));
        return true;
    }
    TargetParams params = magictech_spells[spell].target_params[MAGICTECH_ACTION_BEGIN];
    params.tgt = *flags;
    if (magictech_is_aggressive(spell) && !force) params.tgt |= TGT_NON_PARTY_CRITTERS;
    TargetContext ctx = { 0 };
    target_context_init(&ctx, NULL, source);
    ctx.params = &params;
    ctx.self_obj = pc;
    ctx.source_loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
    ctx.target_obj = ctx.orig_target_obj = td->is_loc ? OBJ_HANDLE_NULL : td->obj;
    ctx.target_loc = ctx.orig_target_loc = td->is_loc ? td->loc : 0;
    if (target_context_evaluate(&ctx) && params.tgt) return true;
    if (td->is_loc || !td->obj) return false;
    ctx.target_loc = ctx.orig_target_loc = obj_field_int64_get(td->obj, OBJ_F_LOCATION);
    if (!target_context_evaluate(&ctx) || !params.tgt) return false;
    target_descriptor_set_loc(td, ctx.target_loc);
    return true;
}
bool coop_engine_magic_item_apply(int64_t item, const TargetDescriptor* td)
{
    int spell = mt_item_spell(item, 0);
    if (!coop_authority_can_simulate() || spell < 0 || spell >= MT_SPELL_COUNT || !magictech_is_enabled(spell)) return false;
    if (!magictech_can_allocate()) return false;
    /* Triggered enchantments execute through mt_item_notify_used, once. */
    if (sub_45A030(spell)) return true;
    MagicTechInvocation invocation;
    magictech_invocation_init(&invocation, item, spell);
    uint64_t* flags;
    sub_459F20(spell, &flags);
    int64_t parent;
    bool self = *flags == TGT_NONE || ((*flags & TGT_SELF) && !(*flags & TGT_TILE)) || *flags == TGT_OBJ_RADIUS;
    if (self && item_parent(item, &parent)) invocation.target_loc = obj_field_int64_get(parent, OBJ_F_LOCATION);
    else if (td->is_loc) invocation.target_loc = td->loc;
    else sub_4440E0(td->obj, &invocation.target_obj);
    return magictech_invocation_submit(&invocation);
}
unsigned coop_engine_magic_execute(int64_t pc, const CoopMagicRequest* r, CoopUseTicket* ticket)
{
    if (!pc || !r || !coop_authority_can_simulate()) return COOP_MAGIC_UNAVAILABLE;
    if (r->generation != combat_turn_generation_get()) return COOP_MAGIC_STALE;
    if (r->operation == COOP_MAGIC_DESTINATION) return coop_engine_teleport_apply(pc, r);
    if (r->operation == COOP_MAGIC_CANCEL) {
        for (unsigned i = 0; i < COOP_MAGIC_RUN_LIMIT; i++) {
            MagicTechRunInfo* run = &magictech_run_info[i];
            if (magictech_run_token(i) == r->token && (run->flags & MAGICTECH_RUN_ACTIVE)
                && run->parent_obj.obj == pc && run->action == MAGICTECH_ACTION_MAINTAIN
                && magictech_is_magic(run->spell) && magictech_get_maintenance(run->spell)->period > 0) {
                magictech_interrupt(run->id);
                return COOP_MAGIC_OK;
            }
        }
        return COOP_MAGIC_TARGET;
    }
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != pc) return COOP_MAGIC_TURN;
    if (r->operation != COOP_MAGIC_USE_ITEM && (r->spell >= MT_SPELL_COUNT || !magictech_is_enabled(r->spell))) return COOP_MAGIC_UNKNOWN;
    int64_t source = resolve(&r->source, true);
    if (!source) return COOP_MAGIC_SOURCE;
    if (coop_engine_teleport_pending(pc) && (r->spell == SPELL_TELEPORTATION
        || (r->operation == COOP_MAGIC_USE_ITEM && mt_item_spell(source, 0) == SPELL_TELEPORTATION))) return COOP_MAGIC_BUSY;
    if (r->operation != COOP_MAGIC_USE_ITEM && !source_knows(pc, source, r->spell)) return source == pc ? COOP_MAGIC_UNKNOWN : COOP_MAGIC_SOURCE;
    TargetDescriptor td;
    if (r->flags & COOP_MAGIC_LOCATION) {
        int64_t x, y;
        location_limits_get(&x, &y);
        if (r->x >= (uint64_t)x || r->y >= (uint64_t)y) return COOP_MAGIC_TARGET;
        target_descriptor_set_loc(&td, location_make(r->x, r->y));
    } else if (r->operation == COOP_MAGIC_USE_ITEM && !r->target.bytes[0]) {
        target_descriptor_set_obj(&td, OBJ_HANDLE_NULL);
    } else {
        int64_t obj = resolve(&r->target, true);
        if (!obj) return COOP_MAGIC_TARGET;
        if (r->operation != COOP_MAGIC_USE_ITEM && magictech_is_aggressive(r->spell) && !(r->flags & COOP_MAGIC_FORCE)
            && obj_type_is_critter(obj_field_int32_get(obj, OBJ_F_TYPE)) && critter_party_same(pc, obj)) return COOP_MAGIC_TARGET;
        if (obj_field_int32_get(obj, OBJ_F_FLAGS) & OF_INVENTORY) {
            int64_t parent;
            if (!item_parent(obj, &parent) || parent != pc) return COOP_MAGIC_TARGET;
        }
        target_descriptor_set_obj(&td, obj);
    }
    if (r->operation == COOP_MAGIC_USE_ITEM) return coop_engine_use_execute(pc, source, &td,
        (r->flags & COOP_MAGIC_FORCE) != 0, (r->flags & COOP_MAGIC_OWN_SKILL) != 0, ticket);
    if (!coop_engine_magic_target_valid(pc, source, r->spell, (r->flags & COOP_MAGIC_FORCE) != 0, &td)) return COOP_MAGIC_TARGET;
    int64_t location = td.is_loc ? td.loc : obj_field_int64_get(td.obj, OBJ_F_LOCATION);
    bool inventory_target = !td.is_loc && (obj_field_int32_get(td.obj, OBJ_F_FLAGS) & OF_INVENTORY);
    if (!inventory_target && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), location) > magictech_get_range(r->spell)) return COOP_MAGIC_DISTANCE;
    if (!combat_check_cast_spell(pc)) return COOP_MAGIC_REFUSED;
    if (source == pc && !magictech_can_charge_spell_fatigue(pc, r->spell)) return COOP_MAGIC_FATIGUE;
    bool available = false;
    for (unsigned i = 0; i < COOP_MAGIC_RUN_LIMIT; i++)
        available |= magictech_run_info[i].id == -1;
    if (!available) return COOP_MAGIC_BUSY;
    /* Item-spell quickkeys originally equip their weapon before activation.
     * The replica selects the target; only the host may swap the inventory.
     * Native item_wield_set validates removal, curses and insertion space. */
    if (source != pc && obj_field_int32_get(source, OBJ_F_TYPE) == OBJ_TYPE_WEAPON
        && item_wield_get(pc, ITEM_INV_LOC_WEAPON) != source) {
        if (!item_wield_set(source, ITEM_INV_LOC_WEAPON)) return COOP_MAGIC_SOURCE;
        if (r->map != (uint32_t)map_current_map()) return COOP_MAGIC_STALE;
        /* WIELD scripts can change targets, turns, ownership and resources.
         * Resolve the request again after the successful native equip; its
         * postcondition keeps this revalidation to one additional pass. */
        return coop_engine_magic_execute(pc, r, ticket);
    }
    MagicTechInvocation invocation;
    magictech_invocation_init(&invocation, source, r->spell);
    if (td.is_loc)
        invocation.target_loc = td.loc;
    else
        sub_4440E0(td.obj, &invocation.target_obj);
    if (!magictech_check_los(&invocation)) return COOP_MAGIC_LINE_OF_SIGHT;
    return magictech_invocation_submit(&invocation) ? COOP_MAGIC_OK : COOP_MAGIC_REFUSED;
}
