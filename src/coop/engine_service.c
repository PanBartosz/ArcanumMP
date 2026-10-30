#include "coop/engine_service.h"
#include "coop/authority.h"
#include "coop/runtime.h"
#include "game/ai.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/reaction.h"
#include "game/skill.h"
#include "game/spell.h"
#include <limits.h>
#include <string.h>
static bool service_object_live(int64_t obj, ObjectID id)
{
    return obj_pool_perm_lookup(id) == obj
        && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT));
}
static bool service_participants_live(int map, uint32_t epoch, int64_t pc, ObjectID pc_id, int64_t npc, ObjectID npc_id, int64_t stock, ObjectID stock_id)
{
    return map == map_current_map() && epoch == coop_runtime_epoch() && service_object_live(pc, pc_id) && service_object_live(npc, npc_id)
        && service_object_live(stock, stock_id) && critter_is_active(pc) && critter_is_active(npc);
}
CoopServiceStatus coop_engine_service_quote(int64_t pc, int64_t npc, int64_t item, unsigned mode, CoopServiceQuote* q)
{
    return coop_engine_service_quote_quantity(pc, npc, item, mode, 1, q);
}
CoopServiceStatus coop_engine_service_quote_quantity(int64_t pc, int64_t npc, int64_t item, unsigned mode, unsigned quantity, CoopServiceQuote* q)
{
    if (!coop_authority_can_simulate() || !pc || !npc || !item || !q || !critter_is_active(pc) || !critter_is_active(npc)
        || obj_field_int32_get(npc, OBJ_F_TYPE) != OBJ_TYPE_NPC || (mode != 3 && mode != 8 && mode != 9) || !quantity || quantity > 1000000) return COOP_SERVICE_UNAVAILABLE;
    if (ai_can_speak(npc, pc, false) != AI_SPEAK_OK || object_dist(pc, npc) > 10) return COOP_SERVICE_CONVERSATION;
    int64_t parent = 0;
    int64_t stock = critter_substitute_inventory_get(npc); if (!stock) stock = npc;
    if (!obj_type_is_item(obj_field_int32_get(item, OBJ_F_TYPE)) || !item_parent(item, &parent)
        || (parent != pc && (mode != 3 || parent != stock))
        || (obj_field_int32_get(item, OBJ_F_FLAGS) & (OF_DESTROYED | OF_OFF | OF_EXTINCT))) return COOP_SERVICE_OWNERSHIP;
    memset(q, 0, sizeof(*q)); q->mode = (int)mode; q->quantity = quantity;
    q->art = (uint32_t)obj_field_int32_get(item, OBJ_F_CURRENT_AID);
    q->flags = (uint32_t)obj_field_int32_get(item, OBJ_F_ITEM_FLAGS);
    if (mode == 3) {
        int type = obj_field_int32_get(item, OBJ_F_TYPE);
        int64_t leader = critter_leader_get(npc);
        q->follower = leader == pc;
        if (leader && obj_field_int32_get(leader, OBJ_F_TYPE) == OBJ_TYPE_PC && leader != pc) return COOP_SERVICE_OWNERSHIP;
        if ((type == OBJ_TYPE_GOLD && (!q->follower || quantity > (unsigned)item_gold_get(item)))
            || (type != OBJ_TYPE_AMMO && type != OBJ_TYPE_GOLD && quantity != 1)
            || (type == OBJ_TYPE_AMMO && quantity > (unsigned)obj_field_int32_get(item, OBJ_F_AMMO_QUANTITY))) return COOP_SERVICE_TARGET;
        q->selling = parent == pc;
        if (q->follower) return COOP_SERVICE_OK;
        int64_t seller = q->selling ? pc : npc, buyer = q->selling ? npc : pc;
        ObjectID id = obj_get_id(item), pc_id = obj_get_id(pc), npc_id = obj_get_id(npc), stock_id = obj_get_id(stock);
        int quoted_map = map_current_map();
        uint32_t quoted_epoch = coop_runtime_epoch();
        int unit = item_cost(item, seller, buyer, false);
        /* Native buy scripts can alter/remove the item or either participant.
         * Never retain a handle across that callback without checking it. */
        int64_t current_parent = 0;
        if (!service_participants_live(quoted_map, quoted_epoch, pc, pc_id, npc, npc_id, stock, stock_id)
            || !service_object_live(item, id) || !item_parent(item, &current_parent) || current_parent != parent
            || (obj_field_int32_get(item, OBJ_F_FLAGS) & OF_OFF)
            || q->art != (uint32_t)obj_field_int32_get(item, OBJ_F_CURRENT_AID) || q->flags != (uint32_t)obj_field_int32_get(item, OBJ_F_ITEM_FLAGS)) return COOP_SERVICE_CHANGED;
        if (unit <= 0 || (int64_t)unit * quantity > INT_MAX) return COOP_SERVICE_TARGET;
        q->price = unit * (int)quantity;
        q->maximum = object_hp_max(item); q->damage = object_hp_damage_get(item);
        q->training = basic_skill_training_get(pc, BASIC_SKILL_HAGGLE);
        q->effectiveness = basic_skill_effectiveness(pc, BASIC_SKILL_HAGGLE, item);
        return COOP_SERVICE_OK;
    }
    if (quantity != 1) return COOP_SERVICE_TARGET;
    if (mode == 8) {
        if (item_is_identified(item)) return COOP_SERVICE_IDENTIFIED;
        q->price = spell_money(SPELL_DIVINE_MAGICK);
    } else {
        q->maximum = object_hp_max(item); q->damage = object_hp_damage_get(item);
        q->training = tech_skill_training_get(npc, TECH_SKILL_REPAIR);
        q->effectiveness = tech_skill_effectiveness(npc, TECH_SKILL_REPAIR, item);
        if (q->maximum <= 0 || q->damage < 0) return COOP_SERVICE_TARGET;
        if (tig_art_item_id_destroyed_get(q->art)) {
            if (q->training != TRAINING_MASTER) return COOP_SERVICE_BROKEN;
            q->damage = q->maximum;
        }
        if (!q->damage) return COOP_SERVICE_UNDAMAGED;
        int worth = item_cost(item, pc, npc, true);
        if (worth < 0) return COOP_SERVICE_LIMIT;
        int64_t price = (int64_t)q->damage * (worth / 2) / q->maximum;
        if (price > INT_MAX) return COOP_SERVICE_LIMIT;
        q->price = price < 2 ? 2 : (int)price;
    }
    return q->price > 0 ? COOP_SERVICE_OK : COOP_SERVICE_LIMIT;
}
bool coop_engine_service_equal(const CoopServiceQuote* a, const CoopServiceQuote* b)
{
    return a && b && a->price == b->price && a->mode == b->mode && a->maximum == b->maximum && a->damage == b->damage
        && a->training == b->training && a->effectiveness == b->effectiveness && a->art == b->art && a->flags == b->flags
        && a->quantity == b->quantity && a->selling == b->selling && a->follower == b->follower;
}
CoopServiceStatus coop_engine_service_buy(int64_t pc, int64_t npc, int64_t item, const CoopServiceQuote* q)
{
    if (!q) return COOP_SERVICE_CHANGED;
    CoopServiceQuote current;
    CoopServiceStatus status = coop_engine_service_quote_quantity(pc, npc, item, (unsigned)q->mode, q->quantity, &current);
    if (status) return status;
    if (!coop_engine_service_equal(q, &current)) return COOP_SERVICE_CHANGED;
    int64_t payer = q->mode == 3 && q->selling ? npc : pc, payee = payer == pc ? npc : pc;
    if (item_gold_get(payer) < q->price) return COOP_SERVICE_MONEY;
    if (item_gold_get(payee) > INT_MAX - q->price) return COOP_SERVICE_LIMIT;
    if (q->mode == 3) {
        int64_t stock = critter_substitute_inventory_get(npc); if (!stock) stock = npc;
        int64_t from = q->selling ? pc : stock, to = q->selling ? stock : pc;
        if (item_check_remove(item) != ITEM_CANNOT_OK) return COOP_SERVICE_TARGET;
        int where = -1;
        if (item_check_insert_quantity(item, to, (int)q->quantity, &where) != ITEM_CANNOT_OK) return COOP_SERVICE_TARGET;
        int worth = item_worth(item);
        int type = obj_field_int32_get(item, OBJ_F_TYPE);
        if (type == OBJ_TYPE_GOLD) {
            if (item_gold_get(to) > INT_MAX - (int)q->quantity) return COOP_SERVICE_LIMIT;
            return item_gold_transfer(from, to, (int)q->quantity, item) ? COOP_SERVICE_OK : COOP_SERVICE_TARGET;
        }
        int ammo_type = type == OBJ_TYPE_AMMO ? obj_field_int32_get(item, OBJ_F_AMMO_TYPE) : 0;
        ObjectID pc_id = obj_get_id(pc), npc_id = obj_get_id(npc), item_id = obj_get_id(item), stock_id = obj_get_id(stock);
        int traded_map = map_current_map();
        uint32_t traded_epoch = coop_runtime_epoch();
        int ammo_from = type == OBJ_TYPE_AMMO ? item_ammo_quantity_get(from, ammo_type) : 0;
        int ammo_to = type == OBJ_TYPE_AMMO ? item_ammo_quantity_get(to, ammo_type) : 0;
        bool moved = type == OBJ_TYPE_AMMO
            ? item_ammo_transfer(from, to, (int)q->quantity, ammo_type, item)
            : item_transfer(item, to);
        if (!moved) return COOP_SERVICE_TARGET;
        if (!service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)) return COOP_SERVICE_CHANGED;
        int payer_gold = item_gold_get(payer), payee_gold = item_gold_get(payee);
        if (!q->follower && (payer_gold < q->price || payee_gold > INT_MAX - q->price
                || !item_gold_transfer(payer, payee, q->price, OBJ_HANDLE_NULL))) {
            /* A refused payment can still execute INSERT_ITEM scripts. Do
             * not roll back through deleted participants, a replaced map or
             * inventory that the script has independently changed. */
            if (!service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)) {
                coop_runtime_native_failure("The trade was interrupted after transferring an item. Use Recover party.");
                return COOP_SERVICE_CHANGED;
            }
            bool restored = false;
            if (type == OBJ_TYPE_AMMO) {
                if (item_ammo_quantity_get(from, ammo_type) == ammo_from - (int)q->quantity
                    && (int64_t)item_ammo_quantity_get(to, ammo_type) == (int64_t)ammo_to + q->quantity)
                    restored = item_ammo_transfer(to, from, (int)q->quantity, ammo_type, OBJ_HANDLE_NULL);
                restored = restored && service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)
                    && item_ammo_quantity_get(from, ammo_type) == ammo_from && item_ammo_quantity_get(to, ammo_type) == ammo_to;
            } else if (service_object_live(item, item_id)) {
                int64_t parent = OBJ_HANDLE_NULL;
                if (item_parent(item, &parent)) {
                    if (parent == to) restored = item_transfer(item, from);
                    else restored = parent == from;
                }
                restored = restored && service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)
                    && service_object_live(item, item_id) && item_parent(item, &parent) && parent == from;
            }
            if (!restored || !service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)
                || item_gold_get(payer) != payer_gold || item_gold_get(payee) != payee_gold)
                coop_runtime_native_failure("The failed trade could not be restored. Use Recover party.");
            return COOP_SERVICE_MONEY;
        }
        /* Payment can itself run an INSERT_ITEM script. Re-resolve again
         * before touching the sold object or either participant's reaction. */
        if (!service_participants_live(traded_map, traded_epoch, pc, pc_id, npc, npc_id, stock, stock_id)) return COOP_SERVICE_CHANGED;
        bool item_live = obj_pool_perm_lookup(item_id) == item
            && !(obj_field_int32_get(item, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT));
        if (q->follower) return COOP_SERVICE_OK;
        if (q->selling && type != OBJ_TYPE_AMMO && item_live) item_flags_set(item, OIF_IDENTIFIED);
        int64_t difference = q->selling ? (int64_t)worth * q->quantity - q->price : q->price - (int64_t)worth * q->quantity;
        if (difference >= 0 && difference <= INT_MAX) sub_4C11D0(npc, pc, (int)difference);
        return COOP_SERVICE_OK;
    }
    /* Native rules retain repair effectiveness/durability and identify effects.
     * Claiming the quote belongs to the ordered caller before this callback. */
    bool completed = q->mode == 8 ? item_perform_identify_service(item, npc, pc, q->price)
                                 : skill_perform_repair_service(item, npc, pc, q->price);
    return completed ? COOP_SERVICE_OK : COOP_SERVICE_CHANGED;
}
