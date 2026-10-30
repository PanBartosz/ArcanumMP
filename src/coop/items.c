#include "coop/items.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_theft.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/gsound.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portal.h"
#include "game/sfx.h"
#include "game/target.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { REQUEST_SIZE = 59,
    RESULT_SIZE = 16,
    ACCESS_SIZE = 30,
    STAGED = 1,
    PICKING_UP,
    CAPTURING,
    REPLY,
    SENT,
    OPENING_LOOT,
    TAKING_ALL,
    STEALING,
    WORLD_USING };
typedef struct ItemPeer {
    uint8_t request[REQUEST_SIZE];
    uint32_t sequence;
    unsigned phase;
    unsigned status;
    uint32_t revision;
    uint64_t capture;
    uint64_t started;
    uint64_t approach_at;
    int64_t approach_location;
    unsigned count;
    uint64_t window;
    int initial_quantity;
    int quantity;
    int ammo_type;
    int item_type;
    bool changed;
    ObjectID access_target;
    uint32_t access_generation;
    unsigned access_flags;
    bool access_event;
    uint64_t access_capture;
    bool native_pending;
    ObjectID native_target;
    uint64_t native_at;
    ObjectID* take_ids;
    unsigned take_count;
    unsigned take_index;
    bool take_failed;
    bool access_steal;
    CoopTheftTicket theft;
    AnimID world_goal;
    ObjectID world_actor, world_target;
    bool world_reached, world_open;
    struct CoopItems* items;
    unsigned sender;
} ItemPeer;
struct CoopItems {
    CoopSession* session;
    CoopWorldNetwork* world;
    uint32_t epoch;
    bool host;
    CoopItemActor actor;
    void* context;
    uint64_t now;
    ItemPeer peers[COOP_MAX_PLAYERS];
    bool pending;
    bool pending_approach;
    bool received;
    uint32_t submitted;
    uint32_t last_result;
    unsigned last_status;
    uint32_t waiting_revision;
    uint64_t submitted_at;
    unsigned executed;
    unsigned rejected;
    int map;
    unsigned decay_holds;
    bool failed;
    bool retired;
    uint32_t access_received;
    uint32_t access_applied;
    uint32_t access_presented;
    uint32_t access_revision;
    CoopObjectId access_target;
    bool access_steal;
    bool release_requested;
    uint32_t release_generation;
};
static void release_access(CoopItems* items, ItemPeer* peer, bool notify);
CoopItems* coop_items_create(CoopSession* session, CoopWorldNetwork* world, uint32_t epoch, CoopItemActor actor, void* context)
{
    CoopItems* items = calloc(1, sizeof(*items));
    if (items) {
        items->session = session; items->world = world; items->epoch = epoch;
        items->host = coop_session_is_host(session); items->actor = actor; items->context = context;
        items->map = map_current_map();
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) { items->peers[i].items = items; items->peers[i].sender = i; }
    }
    return items;
}
void coop_items_retire(CoopItems* items)
{
    if (!items || items->retired) return;
    items->retired = true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        ItemPeer* peer = &items->peers[i];
        AnimRunInfo* run;
        if (items->host && peer->phase == WORLD_USING && anim_id_to_run_info(&peer->world_goal, &run)) {
            int64_t actor = obj_pool_perm_lookup(peer->world_actor);
            if (actor) sub_424070(actor, PRIORITY_3, false, true);
        }
        coop_engine_theft_release(&items->peers[i].theft);
        if (items->host) release_access(items, &items->peers[i], false);
    }
}
void coop_items_destroy(CoopItems* items)
{
    if (!items) return;
    coop_items_retire(items);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) free(items->peers[i].take_ids);
    free(items);
}
bool coop_items_message(CoopItems* items, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (size < 8 || coop_object_read32(p) != items->epoch) return false;
    uint32_t sequence = coop_object_read32(p + 4);
    if (!items->host && type == COOP_LOOT_ACCESS) {
        if (sender || size != ACCESS_SIZE || !sequence || sequence < items->access_received || p[29] > 1) return false;
        CoopObjectId target;
        memcpy(target.bytes, p + 12, sizeof(target.bytes));
        if (!coop_object_id_valid(&target, true) || (target.bytes[0] && target.bytes[0] != 2 && target.bytes[0] != 3)) return false;
        if (target.bytes[0] == 3) {
            int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
            if (coop_object_read32(target.bytes + 1) >= (uint64_t)max_x || coop_object_read32(target.bytes + 5) >= (uint64_t)max_y
                || coop_object_read32(target.bytes + 13) != (uint32_t)items->map) return false;
        }
        if (!target.bytes[0] && p[29]) return false;
        if (sequence == items->access_received) return !memcmp(&target, &items->access_target, sizeof(target)) && coop_object_read32(p + 8) == items->access_revision && items->access_steal == (p[29] != 0);
        items->access_received = sequence;
        items->access_revision = coop_object_read32(p + 8);
        items->access_target = target;
        items->access_steal = p[29] != 0;
        return true;
    }
    if (items->host) {
        if (type != COOP_ITEM_REQUEST || size != REQUEST_SIZE || sender >= COOP_MAX_PLAYERS
            || !coop_session_players(items->session)[sender].connected || !sequence || p[28] > COOP_ITEM_DROP_AMOUNT) return false;
        ItemPeer* peer = &items->peers[sender];
        if (sequence == peer->sequence) {
            if (memcmp(p, peer->request, size)) return false;
            if (peer->phase == SENT) peer->phase = REPLY;
            return true;
        }
        if (peer->sequence == UINT32_MAX || sequence != peer->sequence + 1 || (peer->phase && peer->phase != SENT)) return false;
        if (items->now - peer->window >= 1000) { peer->count = 0; peer->window = items->now; }
        if (++peer->count > 30) return false;
        memcpy(peer->request, p, size);
        peer->sequence = sequence;
        peer->phase = STAGED;
        peer->started = items->now;
        peer->changed = false;
        return true;
    }
    if (sender || type != COOP_ITEM_RESULT || size != RESULT_SIZE || sequence != items->submitted) return false;
    unsigned status = coop_object_read32(p + 8);
    if (items->received) return status == items->last_status && coop_object_read32(p + 12) == items->waiting_revision;
    if (!items->pending) return false;
    if (status > ITEM_CANNOT_WIELD_TECH_ITEMS && (status < COOP_ITEM_UNAVAILABLE || status > COOP_LOOT_HELD)) return false;
    items->last_status = status;
    items->waiting_revision = coop_object_read32(p + 12);
    items->received = true;
    return true;
}
static int64_t target_object(const uint8_t* p)
{
    CoopObjectId id;
    memcpy(id.bytes, p + 29, sizeof(id.bytes));
    if (!coop_object_id_valid(&id, false) || (id.bytes[0] != 2 && id.bytes[0] != 3)) return OBJ_HANDLE_NULL;
    if (id.bytes[0] == 3) {
        int64_t max_x, max_y;
        location_limits_get(&max_x, &max_y);
        if (coop_object_read32(id.bytes + 1) >= (uint64_t)max_x || coop_object_read32(id.bytes + 5) >= (uint64_t)max_y
            || coop_object_read32(id.bytes + 13) != coop_object_read32(p + 8)) return OBJ_HANDLE_NULL;
    }
    int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(id));
    return item && !(obj_field_int32_get(item, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT)) ? item : OBJ_HANDLE_NULL;
}
static int64_t target_item(const uint8_t* p)
{
    int64_t item = target_object(p);
    return item && item_is_item(item) ? item : OBJ_HANDLE_NULL;
}
static int64_t peer_actor(const CoopItems* items, unsigned sender)
{
    int member = coop_campaign_slot_for_owner(coop_session_players(items->session)[sender].identity);
    return member >= 0 ? coop_campaign_object((unsigned)member) : OBJ_HANDLE_NULL;
}
static bool lootable(int64_t target)
{
    if (!target || !obj_handle_is_valid(target) || (obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return false;
    int type = obj_field_int32_get(target, OBJ_F_TYPE);
    return type == OBJ_TYPE_CONTAINER || (obj_type_is_critter(type) && coop_campaign_slot_for_object(target) < 0 && critter_is_dead(target));
}
static bool stealable(int64_t pc, int64_t target)
{
    if (!pc || !target || !obj_handle_is_valid(target) || obj_field_int32_get(target, OBJ_F_TYPE) != OBJ_TYPE_NPC
        || critter_is_dead(target) || (obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
        || (obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS) & OCF_MECHANICAL)
        || (obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS2) & OCF2_NO_PICKPOCKET)
        || (obj_field_int32_get(target, OBJ_F_SPELL_FLAGS) & OSF_STONED)) return false;
    TargetParams params = { .tgt = TGT_OBJ_NO_ST_CRITTER_ANIMAL | TGT_OBJ_T_CRITTER | TGT_OBJECT };
    TargetContext ctx = {0}; target_context_init(&ctx, &params, pc); ctx.target_obj = ctx.orig_target_obj = target;
    return target_context_evaluate(&ctx);
}
static unsigned access_users(const CoopItems* items, ObjectID target)
{
    unsigned count = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (items->peers[i].access_target.type != OID_TYPE_NULL && objid_is_equal(items->peers[i].access_target, target)) count++;
    return count;
}
static void access_event(CoopItems* items, ItemPeer* peer)
{
    if (peer->access_generation == UINT32_MAX) { items->failed = true; return; }
    peer->access_generation++;
    peer->access_event = true;
    peer->access_capture = coop_world_net_settled(items->world) ? coop_world_net_captures(items->world) : coop_world_net_request_capture(items->world);
}
static void release_access(CoopItems* items, ItemPeer* peer, bool notify)
{
    ObjectID id = peer->access_target;
    if (id.type == OID_TYPE_NULL) return;
    peer->access_target = (ObjectID) { .type = OID_TYPE_NULL };
    peer->access_steal = false;
    int64_t target = obj_pool_perm_lookup(id);
    bool container = peer->access_flags & UINT32_C(0x80000000);
    if (container && items->decay_holds && --items->decay_holds == 0) item_decay_process_enable();
    if (target && container && obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER && !access_users(items, id)) {
        unsigned flags = (unsigned)obj_field_int32_get(target, OBJ_F_CONTAINER_FLAGS);
        if (tig_art_id_frame_get(obj_field_int32_get(target, OBJ_F_CURRENT_AID)) == 1 && !(flags & OCOF_STICKY)) {
            object_dec_current_aid(target);
            gsound_play_sfx(sfx_container_sound(target, CONTAINER_SOUND_CLOSE), 1);
        }
        sub_463540(target);
    }
    if (notify) access_event(items, peer);
}
static bool access_valid(const CoopItems* items, const ItemPeer* peer, int64_t pc)
{
    if (!pc || !critter_is_active(pc) || peer->access_target.type == OID_TYPE_NULL) return false;
    if ((obj_field_int32_get(pc, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
        || (obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS) & (OCF_PARALYZED | OCF_STUNNED))
        || (obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_STONED)) return false;
    if (obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_POLYMORPHED) return false;
    int64_t target = obj_pool_perm_lookup(peer->access_target);
    if (!(peer->access_steal ? stealable(pc, target) : lootable(target))
        || location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(target, OBJ_F_LOCATION)) > (peer->access_steal ? 30 : 3)) return false;
    if (obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) {
        unsigned locks = (unsigned)obj_field_int32_get(target, OBJ_F_CONTAINER_FLAGS) & (OCOF_LOCKED | OCOF_JAMMED | OCOF_MAGICALLY_HELD);
        if (locks & ~peer->access_flags) return false;
    }
    (void)items;
    return true;
}
static bool theft_allowed(void* context)
{
    ItemPeer* peer = context; CoopItems* items = peer->items;
    int64_t pc = peer_actor(items, peer->sender), item = target_item(peer->request), parent;
    bool plant = peer->request[28] == COOP_LOOT_PUT;
    return peer->access_steal && access_valid(items, peer, pc)
        && coop_object_read32(peer->request + 51) == peer->access_generation
        && !memcmp(obj_get_id(pc).d.g.data, peer->request + 12, 16) && coop_object_read32(peer->request + 8) == (uint32_t)items->map
        && item && item_parent(item, &parent) && parent == (plant ? pc : obj_pool_perm_lookup(peer->access_target));
}
static unsigned open_failure(int64_t pc, int64_t target)
{
    if (lootable(target) && obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) {
        unsigned flags = (unsigned)obj_field_int32_get(target, OBJ_F_CONTAINER_FLAGS);
        if (flags & OCOF_JAMMED) return COOP_LOOT_JAMMED;
        if (flags & OCOF_MAGICALLY_HELD) return COOP_LOOT_HELD;
        if (object_locked_get(target) && pc && !sub_463370(pc, obj_field_int32_get(target, OBJ_F_CONTAINER_KEY_ID))) return COOP_LOOT_LOCKED;
    }
    return COOP_ITEM_INTERRUPTED;
}
static unsigned grant_access(CoopItems* items, ItemPeer* peer, int64_t pc, int64_t target, bool steal)
{
    if (!pc || !critter_is_active(pc) || !(steal ? stealable(pc, target) : lootable(target))) return COOP_ITEM_TARGET;
    if (location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(target, OBJ_F_LOCATION)) > (steal ? 30 : 3)) return COOP_ITEM_DISTANCE;
    int mode = steal ? INVEN_UI_MODE_STEAL : INVEN_UI_MODE_LOOT;
    ObjectID id = obj_get_id(target);
    ObjectID pc_id = obj_get_id(pc);
    if (peer->access_target.type != OID_TYPE_NULL && objid_is_equal(peer->access_target, id) && peer->access_steal == steal) {
        access_event(items, peer);
        return 0;
    }
    peer->changed = true; /* Lock/USE scripts can mutate state even on refusal. */
    if (!sub_572370(pc, target, mode)) {
        unsigned status = open_failure(pc, target);
        return status == COOP_ITEM_INTERRUPTED ? COOP_ITEM_REFUSED : status;
    }
    pc = obj_pool_perm_lookup(pc_id);
    target = obj_pool_perm_lookup(id);
    if (!obj_handle_is_valid(pc) || !critter_is_active(pc) || !(steal ? stealable(pc, target) : lootable(target))) return COOP_ITEM_TARGET;
    release_access(items, peer, false);
    if (!access_users(items, id)) {
        sub_572640(pc, target, mode);
        target = obj_pool_perm_lookup(id);
        if (!(steal ? stealable(pc, target) : lootable(target))) return COOP_ITEM_TARGET;
        item_arrange_inventory(target, false);
        sub_4640C0(target);
    }
    if (!(steal ? stealable(pc, target) : lootable(target))) return COOP_ITEM_TARGET;
    peer->access_target = obj_get_id(target);
    peer->access_steal = steal;
    peer->access_flags = 0;
    if (obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) {
        peer->access_flags = UINT32_C(0x80000000) | (unsigned)obj_field_int32_get(target, OBJ_F_CONTAINER_FLAGS);
        if (items->decay_holds++ == 0) item_decay_process_disable();
    }
    access_event(items, peer);
    peer->changed = true;
    return 0;
}
void coop_items_native_loot(CoopItems* items, int64_t pc, int64_t target)
{
    if (!items->host || !pc || !target || !lootable(target)) return;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if (coop_session_players(items->session)[i].connected && peer_actor(items, i) == pc) {
        ItemPeer* peer = &items->peers[i];
        if (!peer->native_pending) {
            peer->native_pending = true;
            peer->native_target = obj_get_id(target);
            peer->native_at = items->now;
        }
        return;
    }
}
static unsigned relocate(int64_t pc, int64_t item, int location)
{
    if (location == -1) return sub_464C80(item) ? 0 : COOP_ITEM_REFUSED;
    if (IS_WEAR_INV_LOC(location)) return item_wield_set(item, location) ? 0 : COOP_ITEM_REFUSED;
    if (location < 0 || location >= (obj_field_int32_get(pc, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? 960 : 120)) return COOP_ITEM_TARGET;
    int old = item_inventory_location_get(item), slots[960];
    if (old == location) return 0;
    int reason = item_check_remove(item);
    if (reason) return (unsigned)reason;
    item_inventory_slots_get(pc, slots);
    item_inventory_slots_set(item, old, slots, 0);
    if (!item_inventory_slots_has_room_for(item, pc, location, slots)) return ITEM_CANNOT_NO_ROOM;
    item_remove(item);
    reason = item_check_insert(item, pc, NULL);
    item_insert(item, pc, reason ? old : location);
    return (unsigned)reason;
}
static unsigned loot_transfer(ItemPeer* peer, int64_t pc, int64_t item, bool putting, int location)
{
    int64_t target = obj_pool_perm_lookup(peer->access_target), parent = OBJ_HANDLE_NULL;
    if (!item || !target || !item_parent(item, &parent) || parent != (putting ? pc : target)) return COOP_ITEM_OWNERSHIP;
    if (location != -1 && (putting ? location < 0 || location >= 960 : (location < 0 || location >= 120) && !IS_WEAR_INV_LOC(location))) return COOP_ITEM_TARGET;
    int reason = item_check_remove(item);
    if (reason) return (unsigned)reason;
    if (!putting && IS_WEAR_INV_LOC(location)) {
        reason = sub_464D20(item, location, pc);
        if (reason) return (unsigned)reason;
        int64_t old = item_wield_get(pc, location);
        if (old && (reason = item_check_remove(old)) != 0) return (unsigned)reason;
    }
    peer->changed = true; /* TRANSFER scripts may refuse after updating state. */
    if (!item_transfer_ex(item, putting ? target : pc, !putting && IS_WEAR_INV_LOC(location) ? -1 : location)) return COOP_ITEM_REFUSED;
    if (!putting && IS_WEAR_INV_LOC(location) && !item_wield_set(item, location)) return COOP_LOOT_PARTIAL;
    return 0;
}
static unsigned execute(CoopItems* items, ItemPeer* peer, int64_t pc)
{
    const uint8_t* p = peer->request;
    unsigned op = p[28];
    if (op == COOP_LOOT_TAKE_AMOUNT || op == COOP_LOOT_PUT_AMOUNT || op == COOP_ITEM_DROP_AMOUNT) {
        bool drop = op == COOP_ITEM_DROP_AMOUNT, putting = op == COOP_LOOT_PUT_AMOUNT;
        if (!drop && (coop_object_read32(p + 51) != peer->access_generation || !access_valid(items, peer, pc)
            || peer->access_steal)) return COOP_LOOT_DENIED;
        int64_t item = target_item(p), parent = 0;
        int64_t target = drop ? 0 : obj_pool_perm_lookup(peer->access_target);
        uint32_t amount = coop_object_read32(p + 47);
        if (p[46] || !amount || amount > INT_MAX || !item) return COOP_ITEM_TARGET;
        if (!item_parent(item, &parent) || parent != (drop || putting ? pc : target)) return COOP_ITEM_OWNERSHIP;
        int type = obj_field_int32_get(item, OBJ_F_TYPE);
        if (type != OBJ_TYPE_GOLD && type != OBJ_TYPE_AMMO) return COOP_ITEM_TARGET;
        int quantity = type == OBJ_TYPE_GOLD ? item_gold_get(item) : obj_field_int32_get(item, OBJ_F_AMMO_QUANTITY);
        if (quantity <= 0 || amount > (unsigned)quantity) return COOP_ITEM_TARGET;
        int reason = item_check_remove(item);
        if (reason) return (unsigned)reason;
        if (drop && amount == (unsigned)quantity) { peer->changed = true; return item_drop(item) ? 0 : COOP_ITEM_REFUSED; }
        int64_t recipient = drop ? 0 : putting ? target : pc;
        int ammo = type == OBJ_TYPE_AMMO ? obj_field_int32_get(item, OBJ_F_AMMO_TYPE) : 0;
        if (ammo < 0 || ammo >= TIG_ART_AMMO_TYPE_COUNT) return COOP_ITEM_TARGET;
        if (recipient) {
            int held = type == OBJ_TYPE_GOLD ? item_gold_get(recipient) : item_ammo_quantity_get(recipient, ammo);
            if (held < 0 || held > INT_MAX - (int)amount) return COOP_ITEM_TARGET;
            reason = item_check_insert_quantity(item, recipient, (int)amount, NULL);
            if (reason) return (unsigned)reason;
        }
        /* Native quantity drops create a separate stack and use the same
         * pickup/drop placement and decay path as the original quantity UI.
         * Allocate before subtracting so allocation failure retains the stack. */
        int64_t dropped = 0;
        int64_t location = obj_field_int64_get(pc, OBJ_F_LOCATION);
        if (drop) {
            dropped = type == OBJ_TYPE_GOLD ? item_gold_create((int)amount, location) : item_ammo_create((int)amount, ammo, location);
            if (!dropped) return COOP_ITEM_REFUSED;
        }
        peer->changed = true;
        bool moved = type == OBJ_TYPE_GOLD ? item_gold_transfer(parent, recipient, (int)amount, item)
                                          : item_ammo_transfer(parent, recipient, (int)amount, ammo, item);
        if (!moved) { if (dropped) object_destroy(dropped); return COOP_ITEM_REFUSED; }
        if (dropped) { object_pickup(dropped, pc); sub_466E50(dropped, location); }
        return 0;
    }
    if (op == COOP_LOOT_CLOSE) {
        if (coop_object_read32(p + 51) == peer->access_generation) release_access(items, peer, true);
        return 0;
    }
    if (op == COOP_WORLD_EXAMINE) {
        int64_t target = target_object(p);
        if (!target || p[46] || (int32_t)coop_object_read32(p + 47) != -1) return COOP_ITEM_TARGET;
        int64_t parent = OBJ_HANDLE_NULL;
        if (obj_field_int32_get(target, OBJ_F_FLAGS) & OF_INVENTORY) {
            if (!item_parent(target, &parent) || !parent || (parent != pc && !objid_is_equal(obj_get_id(parent), peer->access_target))) return COOP_ITEM_OWNERSHIP;
            if (parent != pc && object_dist(pc, parent) > 3) return COOP_ITEM_DISTANCE;
        } else if (object_dist(pc, target) > 30)
            return COOP_ITEM_DISTANCE;
        Script script = { 0 };
        obj_arrayfield_script_get(target, OBJ_F_SCRIPTS_IDX, SAP_EXAMINE, &script);
        if (!script.num) return 0;
        /* Looking consumes no native AP. Scripts still run exactly once under
         * the authenticated PC, and their mutations cross the world barrier. */
        peer->changed = true;
        object_script_execute(pc, target, OBJ_HANDLE_NULL, SAP_EXAMINE, 0);
        return 0;
    }
    if (op == COOP_WORLD_USE) {
        int64_t target = target_object(p);
        if (!target || p[46] || (int32_t)coop_object_read32(p + 47) != -1) return COOP_ITEM_TARGET;
        int type = obj_field_int32_get(target, OBJ_F_TYPE);
        Script script = { 0 };
        if (type == OBJ_TYPE_WALL) obj_arrayfield_script_get(target, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
        if (type != OBJ_TYPE_PORTAL && type != OBJ_TYPE_SCENERY
            && !(type == OBJ_TYPE_WALL && script.num)) return COOP_ITEM_TARGET;
        if ((obj_field_int32_get(target, OBJ_F_FLAGS) & OF_INVENTORY)
            || (obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_POLYMORPHED)) return COOP_ITEM_UNAVAILABLE;
        if (type == OBJ_TYPE_SCENERY && tig_art_scenery_id_type_get(obj_field_int32_get(target, OBJ_F_CURRENT_AID)) == TIG_ART_SCENERY_TYPE_BEDS) return COOP_ITEM_TARGET;
        if (object_dist(pc, target) > 30) return COOP_ITEM_DISTANCE;
        if (!combat_check_action_points(pc, 2)) return COOP_ITEM_REFUSED;
        AnimGoalData goal;
        if (!sub_44D500(&goal, pc, AG_USE_OBJECT) || !sub_424070(pc, PRIORITY_3, false, false)) return COOP_ITEM_REFUSED;
        goal.params[AGDATA_TARGET_OBJ].obj = target;
        peer->world_actor = obj_get_id(pc);
        peer->world_target = obj_get_id(target);
        peer->world_reached = false;
        peer->world_open = type == OBJ_TYPE_PORTAL && portal_is_open(target);
        if (!anim_goal_add(&goal, &peer->world_goal)) return COOP_ITEM_REFUSED;
        /* The ordinary Use click enables native interaction pathing, including
         * approach through doors. Keep that behavior for remote owners too. */
        sub_436ED0(peer->world_goal);
        peer->approach_at = items->now;
        peer->approach_location = obj_field_int64_get(pc, OBJ_F_LOCATION);
        peer->phase = WORLD_USING;
        return 0;
    }
    if (op == COOP_STEAL_OPEN) {
        if (obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_POLYMORPHED) return COOP_ITEM_UNAVAILABLE;
        return grant_access(items, peer, pc, target_object(p), true);
    }
    if (op == COOP_LOOT_OPEN) {
        int64_t target = target_object(p);
        if (!lootable(target)) return COOP_ITEM_TARGET;
        if (obj_field_int32_get(pc, OBJ_F_SPELL_FLAGS) & OSF_POLYMORPHED) return COOP_ITEM_UNAVAILABLE;
        if (location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(target, OBJ_F_LOCATION)) > 30) return COOP_ITEM_DISTANCE;
        if (peer->access_target.type != OID_TYPE_NULL && objid_is_equal(peer->access_target, obj_get_id(target)) && access_valid(items, peer, pc))
            return grant_access(items, peer, pc, target, false);
        AnimGoalData goal;
        AnimID id;
        if (!sub_44D500(&goal, pc, AG_USE_CONTAINER) || !sub_424070(pc, PRIORITY_3, false, false)) return COOP_ITEM_REFUSED;
        goal.params[AGDATA_TARGET_OBJ].obj = target;
        if (!anim_goal_add(&goal, &id)) return COOP_ITEM_REFUSED;
        /* Match the native Use click: approach may need to traverse a door
         * or route around the target's blocking footprint. */
        sub_436ED0(id);
        peer->approach_at = items->now;
        peer->approach_location = obj_field_int64_get(pc, OBJ_F_LOCATION);
        peer->phase = OPENING_LOOT;
        return 0;
    }
    if (op >= COOP_LOOT_TAKE && op <= COOP_LOOT_MOVE) {
        if (coop_object_read32(p + 51) != peer->access_generation || !access_valid(items, peer, pc)) return COOP_LOOT_DENIED;
        if (peer->access_steal) {
            if (op != COOP_LOOT_TAKE && op != COOP_LOOT_PUT) return COOP_ITEM_TARGET;
            int64_t item = target_item(p);
            if (!item) return COOP_ITEM_TARGET;
            int reason = item_check_remove(item); if (reason) return (unsigned)reason;
            if (!theft_allowed(peer)) return COOP_ITEM_OWNERSHIP;
            unsigned status = coop_engine_theft_start(&peer->theft, pc, obj_pool_perm_lookup(peer->access_target), item, op == COOP_LOOT_PUT, theft_allowed, peer);
            if (status == COOP_THEFT_PENDING) { peer->phase = STEALING; return 0; }
            return status;
        }
        if (op == COOP_LOOT_MOVE) {
            int64_t item = target_item(p), parent = OBJ_HANDLE_NULL, target = obj_pool_perm_lookup(peer->access_target);
            if (!item || !item_parent(item, &parent) || parent != target) return COOP_ITEM_OWNERSHIP;
            int location = (int32_t)coop_object_read32(p + 47);
            if (location < 0 || location >= (obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? 960 : 120)) return COOP_ITEM_TARGET;
            return relocate(target, item, (int32_t)coop_object_read32(p + 47));
        }
        if (op == COOP_LOOT_TAKE_ALL) {
            int64_t target = obj_pool_perm_lookup(peer->access_target), *handles = NULL;
            int count = item_list_get(target, &handles);
            if (count < 0 || count > 4096) { item_list_free(handles); return COOP_ITEM_REFUSED; }
            peer->take_ids = count ? malloc((size_t)count * sizeof(ObjectID)) : NULL;
            if (count && !peer->take_ids) { item_list_free(handles); return COOP_ITEM_REFUSED; }
            peer->take_count = (unsigned)count;
            peer->take_index = 0;
            peer->take_failed = false;
            for (int i = 0; i < count; i++) peer->take_ids[i] = obj_get_id(handles[i]);
            item_list_free(handles);
            peer->phase = TAKING_ALL;
            return 0;
        }
        return loot_transfer(peer, pc, target_item(p), op == COOP_LOOT_PUT, (int32_t)coop_object_read32(p + 47));
    }
    if (op == COOP_ITEM_ARRANGE) {
        CoopObjectId null_id = { 0 };
        if (memcmp(p + 29, &null_id, sizeof(null_id)) || p[46] || coop_object_read32(p + 47) > 1) return COOP_ITEM_TARGET;
        item_arrange_inventory(pc, coop_object_read32(p + 47) != 0);
        return 0;
    }
    int64_t item = target_item(p), parent = OBJ_HANDLE_NULL;
    if (!item) return COOP_ITEM_TARGET;
    item_parent(item, &parent);
    if (op == COOP_ITEM_PICKUP) {
        if (parent) return COOP_ITEM_OWNERSHIP;
        if (location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(item, OBJ_F_LOCATION)) > 30) return COOP_ITEM_DISTANCE;
        peer->item_type = obj_field_int32_get(item, OBJ_F_TYPE);
        if (peer->item_type == OBJ_TYPE_GOLD) {
            peer->quantity = item_gold_get(item); peer->initial_quantity = item_gold_get(pc);
        } else if (peer->item_type == OBJ_TYPE_AMMO) {
            peer->ammo_type = obj_field_int32_get(item, OBJ_F_AMMO_TYPE);
            peer->quantity = obj_field_int32_get(item, OBJ_F_AMMO_QUANTITY);
            peer->initial_quantity = item_ammo_quantity_get(pc, peer->ammo_type);
        }
        if (!anim_goal_pickup_item(pc, item)) return COOP_ITEM_REFUSED;
        peer->phase = PICKING_UP;
        return 0;
    }
    if (parent != pc) return COOP_ITEM_OWNERSHIP;
    if (op == COOP_ITEM_MOVE) return relocate(pc, item, (int32_t)coop_object_read32(p + 47));
    if (op == COOP_ITEM_DROP) return item_drop(item) ? 0 : COOP_ITEM_REFUSED;
    if ((op != COOP_ITEM_GIVE && op != COOP_ITEM_GIVE_AMOUNT) || p[46] >= COOP_MAX_PLAYERS) return COOP_ITEM_TARGET;
    int64_t recipient = coop_campaign_object(p[46]);
    if (!recipient || recipient == pc || !critter_is_active(recipient)) return COOP_ITEM_TARGET;
    if (location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(recipient, OBJ_F_LOCATION)) > 8) return COOP_ITEM_DISTANCE;
    int reason = item_check_remove(item);
    if (reason) return (unsigned)reason;
    if (op == COOP_ITEM_GIVE_AMOUNT) {
        uint32_t amount = coop_object_read32(p + 47);
        if (!amount || amount > INT_MAX) return COOP_ITEM_TARGET;
        int type = obj_field_int32_get(item, OBJ_F_TYPE);
        if (type == OBJ_TYPE_GOLD) {
            if (amount > (unsigned)item_gold_get(item) || item_gold_get(recipient) > INT_MAX - (int)amount) return COOP_ITEM_TARGET;
            peer->changed = true;
            return item_gold_transfer(pc, recipient, (int)amount, item) ? 0 : COOP_ITEM_REFUSED;
        }
        if (type == OBJ_TYPE_AMMO) {
            if (amount > (unsigned)obj_field_int32_get(item, OBJ_F_AMMO_QUANTITY)) return COOP_ITEM_TARGET;
            reason = item_check_insert_quantity(item, recipient, (int)amount, NULL);
            if (reason) return (unsigned)reason;
            peer->changed = true;
            return item_ammo_transfer(pc, recipient, (int)amount, obj_field_int32_get(item, OBJ_F_AMMO_TYPE), item) ? 0 : COOP_ITEM_REFUSED;
        }
        if (amount != 1) return COOP_ITEM_TARGET;
    }
    peer->changed = true;
    return item_transfer(item, recipient) ? 0 : COOP_ITEM_REFUSED;
}
static bool native_world_check(CoopItems* items, int64_t actor, int64_t target, const AnimID* goal, bool reached)
{
    if (!items || !items->host || !goal) return true;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        ItemPeer* peer = &items->peers[i];
        if (peer->phase != WORLD_USING || !anim_id_is_equal(&peer->world_goal, goal)
            || !objid_is_equal(peer->world_actor, obj_get_id(actor))
            || !objid_is_equal(peer->world_target, obj_get_id(target))) continue;
        if (!critter_is_active(actor)
            || coop_object_read32(peer->request + 55) != combat_turn_generation_get()
            || (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != actor)
            || (obj_field_int32_get(actor, OBJ_F_SPELL_FLAGS) & OSF_POLYMORPHED)
            || (obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) {
            fprintf(stderr, "ARCMP world use dispatch refused generation=%u expected=%u active=%d turn=%d\n",
                combat_turn_generation_get(), coop_object_read32(peer->request + 55), critter_is_active(actor),
                !combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == actor);
            return false;
        }
        if (reached) peer->world_reached = true;
        return true;
    }
    /* Untracked native NPC and single-player uses retain their own rules. */
    return true;
}
bool coop_items_native_world_allowed(CoopItems* items, int64_t actor, int64_t target, const AnimID* goal)
{
    return native_world_check(items, actor, target, goal, false);
}
bool coop_items_native_world_used(CoopItems* items, int64_t actor, int64_t target, const AnimID* goal)
{
    return native_world_check(items, actor, target, goal, true);
}
static void complete(CoopItems* items, ItemPeer* peer, unsigned status)
{
    free(peer->take_ids);
    peer->take_ids = NULL;
    peer->status = status;
    peer->revision = 0;
    if (!status) items->executed++; else items->rejected++;
    if ((!status || peer->changed) && !coop_world_net_settled(items->world)) {
        peer->capture = coop_world_net_request_capture(items->world);
        peer->phase = CAPTURING;
    } else {
        peer->revision = coop_world_net_revision(items->world);
        peer->phase = REPLY;
    }
    fprintf(stderr, "ARCMP item operation=%u sequence=%u status=%u\n", peer->request[28], peer->sequence, status);
}
static void notify_result(CoopItems* items)
{
    if (!items->pending || !items->received || coop_world_net_applied_revision(items->world) < items->waiting_revision) return;
    items->pending = false;
    items->last_result = items->submitted;
    inven_ui_coop_refresh();
    if (items->last_status && items->last_status <= ITEM_CANNOT_WIELD_TECH_ITEMS) item_error_msg(player_get_local_pc_obj(), (int)items->last_status);
    else if (items->last_status) {
        char* messages[] = { "Action unavailable.", "That item belongs to another character.", "That object or destination is no longer available.",
            "The item or recipient is too far away.", "The item action could not be completed.", "The item action was interrupted.",
            "That loot access has ended. Open the container again.", "Some items could not be transferred.",
            "That object is locked.", "That lock is jammed.", "That object is magically held." };
        intgame_message_window_display_feedback(messages[items->last_status - COOP_ITEM_UNAVAILABLE]);
    }
}
bool coop_items_poll(CoopItems* items, uint64_t now, bool accepting)
{
    if (coop_world_net_retired(items->world)) return true;
    items->now = now;
    if (items->failed || (items->pending && now - items->submitted_at > (items->pending_approach ? 105000u : 30000u))) return false;
    if (items->host) for (uint8_t sender = 0; sender < COOP_MAX_PLAYERS; sender++) {
        ItemPeer* peer = &items->peers[sender];
        int64_t owner = peer_actor(items, sender);
        if (peer->access_target.type != OID_TYPE_NULL && ((!accepting && peer->phase != STEALING) || !access_valid(items, peer, owner))) release_access(items, peer, true);
        if (peer->native_pending) {
            /* Native goal callbacks only stage the request. All lock/USE/spawn
             * processing runs here, with the correct acting PC. */
            if (accepting && owner) {
                uint8_t common[28] = { 0 };
                coop_object_write32(common, items->epoch);
                coop_object_write32(common + 8, (uint32_t)items->map);
                memcpy(common + 12, obj_get_id(owner).d.g.data, 16);
                int64_t requested = peer->phase == OPENING_LOOT ? target_object(peer->request) : OBJ_HANDLE_NULL;
                bool completing = requested && objid_is_equal(obj_get_id(requested), peer->native_target);
                /* The native use goal has already charged AP. Its callback is
                 * completion of the authorized action even if that ended the
                 * actor's combat turn; it is not a second action submission. */
                if ((completing && critter_is_active(owner)) || items->actor(items->context, sender, common)) {
                    ObjectID target_id = peer->native_target;
                    peer->native_pending = false;
                    unsigned status = grant_access(items, peer, owner, obj_pool_perm_lookup(target_id), false);
                    if (coop_world_net_retired(items->world)) return true;
                    if (completing) complete(items, peer, status);
                }
            }
            if (!accepting || now - peer->native_at > 15000) peer->native_pending = false;
        }
        if (peer->phase == STAGED) {
            int64_t pc = accepting ? items->actor(items->context, sender, peer->request) : OBJ_HANDLE_NULL;
            if (peer->request[28] == COOP_LOOT_CLOSE && owner && coop_object_read32(peer->request + 8) == (uint32_t)items->map
                && !memcmp(peer->request + 12, obj_get_id(owner).d.g.data, 16)) pc = owner;
            if (peer->request[28] != COOP_LOOT_CLOSE && coop_object_read32(peer->request + 55) != combat_turn_generation_get()) pc = OBJ_HANDLE_NULL;
            if (!pc) {
                fprintf(stderr,"ARCMP item admission sender=%u accepting=%d owner=%lld active=%d flags=%u critter_flags=%u spell_flags=%u map=%u/%d generation=%u/%u\n",
                    sender,accepting,(long long)owner,owner && critter_is_active(owner),
                    owner ? (unsigned)obj_field_int32_get(owner,OBJ_F_FLAGS) : 0,
                    owner ? (unsigned)obj_field_int32_get(owner,OBJ_F_CRITTER_FLAGS) : 0,
                    owner ? (unsigned)obj_field_int32_get(owner,OBJ_F_SPELL_FLAGS) : 0,
                    coop_object_read32(peer->request+8),items->map,coop_object_read32(peer->request+55),combat_turn_generation_get());
                complete(items, peer, COOP_ITEM_UNAVAILABLE);
            }
            else {
                unsigned status = execute(items, peer, pc);
                if (coop_world_net_retired(items->world)) return true;
                if (peer->phase != PICKING_UP && peer->phase != OPENING_LOOT && peer->phase != TAKING_ALL && peer->phase != STEALING && peer->phase != WORLD_USING) complete(items, peer, status);
            }
        }
        if ((peer->phase == WORLD_USING || peer->phase == OPENING_LOOT) && owner
            && peer->approach_location != obj_field_int64_get(owner, OBJ_F_LOCATION)) {
            peer->approach_location = obj_field_int64_get(owner, OBJ_F_LOCATION);
            peer->approach_at = now;
        }
        if (peer->phase == WORLD_USING) {
            AnimRunInfo* run;
            bool live = anim_id_to_run_info(&peer->world_goal, &run);
            bool expired = now - peer->approach_at > 15000 || now - peer->started > 90000;
            if (!owner || !critter_is_active(owner) || expired) {
                if (live && owner) sub_424070(owner, PRIORITY_3, false, true);
                complete(items, peer, COOP_ITEM_INTERRUPTED);
            } else if (!live) {
                int64_t target = obj_pool_perm_lookup(peer->world_target);
                unsigned status = peer->world_reached ? 0 : COOP_ITEM_INTERRUPTED;
                if (target && obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_PORTAL) {
                    unsigned flags = (unsigned)obj_field_int32_get(target, OBJ_F_PORTAL_FLAGS);
                    if (peer->world_open != portal_is_open(target))
                        status = 0;
                    else if (!peer->world_reached && (flags & OPF_JAMMED))
                        status = COOP_LOOT_JAMMED;
                    else if (!peer->world_reached && (flags & OPF_MAGICALLY_HELD))
                        status = COOP_LOOT_HELD;
                    else if (!peer->world_reached && object_locked_get(target))
                        status = COOP_LOOT_LOCKED;
                }
                peer->changed = true;
                if (status) fprintf(stderr, "ARCMP world use ended status=%u reached=%d actor=%lld target=%lld\n",
                    status, peer->world_reached, (long long)obj_field_int64_get(owner, OBJ_F_LOCATION),
                    (long long)(target ? obj_field_int64_get(target, OBJ_F_LOCATION) : 0));
                complete(items, peer, status);
            }
        }
        if (peer->phase == OPENING_LOOT && (!accepting || !owner || !critter_is_active(owner) || now - peer->approach_at > 15000 || now - peer->started > 90000 || (!peer->native_pending && !anim_is_current_goal_type(owner, AG_USE_CONTAINER, NULL)))) {
            int64_t target = target_object(peer->request);
            int key = target && obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? obj_field_int32_get(target, OBJ_F_CONTAINER_KEY_ID) : 0;
            fprintf(stderr, "ARCMP loot open ended accepting=%d elapsed=%llu actor=%lld target=%lld distance=%lld flags=%x key=%d available=%d\n",
                accepting, (unsigned long long)(now - peer->started), (long long)(owner ? obj_field_int64_get(owner, OBJ_F_LOCATION) : 0),
                (long long)(target ? obj_field_int64_get(target, OBJ_F_LOCATION) : 0), (long long)(owner && target ? location_dist(obj_field_int64_get(owner, OBJ_F_LOCATION), obj_field_int64_get(target, OBJ_F_LOCATION)) : -1),
                target ? obj_field_int32_get(target, OBJ_F_FLAGS) : 0, key, owner && sub_463370(owner, key));
            if (owner && anim_is_current_goal_type(owner, AG_USE_CONTAINER, NULL)) sub_424070(owner, PRIORITY_3, false, false);
            peer->native_pending = false;
            complete(items, peer, accepting ? open_failure(owner, target_object(peer->request)) : COOP_ITEM_INTERRUPTED);
        }
        if (peer->phase == STEALING) {
            unsigned status = coop_engine_theft_poll(&peer->theft, now - peer->started > 15000);
            if (coop_world_net_retired(items->world)) return true;
            if (status == COOP_THEFT_FAILURE) return false;
            if (status != COOP_THEFT_PENDING) { peer->changed = true; complete(items, peer, status); }
        }
        if (peer->phase == TAKING_ALL) {
            bool valid = accepting && items->actor(items->context, sender, peer->request) && access_valid(items, peer, owner)
                && coop_object_read32(peer->request + 51) == peer->access_generation;
            if (!valid) complete(items, peer, peer->changed ? COOP_LOOT_PARTIAL : COOP_LOOT_DENIED);
            else {
                for (unsigned work = 0; work < 8 && peer->take_index < peer->take_count; work++) {
                    if (!items->actor(items->context, sender, peer->request)) break;
                    int64_t item = obj_pool_perm_lookup(peer->take_ids[peer->take_index++]), parent = OBJ_HANDLE_NULL;
                    if (!item || !item_parent(item, &parent) || !objid_is_equal(obj_get_id(parent), peer->access_target)) continue;
                    if (loot_transfer(peer, owner, item, false, -1)) peer->take_failed = true;
                    if (coop_world_net_retired(items->world)) return true;
                    if (!access_valid(items, peer, owner)) break;
                }
                if (peer->take_index == peer->take_count) complete(items, peer, peer->take_failed ? COOP_LOOT_PARTIAL : 0);
            }
        }
        if (peer->phase == PICKING_UP) {
            /* Authorization was established before queuing the goal. Resolve
             * that PC by ID: a completed pickup may itself end its combat turn
             * or open dialogue, which must not turn success into a refusal. */
            ObjectID actor_id = { .type = OID_TYPE_GUID };
            memcpy(actor_id.d.g.data, peer->request + 12, 16);
            int64_t pc = obj_pool_perm_lookup(actor_id), item = target_item(peer->request), parent = OBJ_HANDLE_NULL;
            if (item) item_parent(item, &parent);
            bool picked = pc && parent == pc;
            if (pc && !item && (peer->item_type == OBJ_TYPE_GOLD || peer->item_type == OBJ_TYPE_AMMO)) {
                int qty = peer->item_type == OBJ_TYPE_GOLD ? item_gold_get(pc) : item_ammo_quantity_get(pc, peer->ammo_type);
                picked = peer->quantity > 0 && (int64_t)qty - peer->initial_quantity >= peer->quantity;
            }
            if (picked) complete(items, peer, 0);
            else if (!accepting || !pc || !critter_is_active(pc) || (parent && parent != pc) || !item || now - peer->started > 15000
                || !anim_is_current_goal_type(pc, AG_PICKUP_ITEM, NULL)) {
                /* A failed/timed-out receipt must not leave a pickup goal that
                 * can mutate inventory after the player has been told it ended. */
                if (pc && anim_is_current_goal_type(pc, AG_PICKUP_ITEM, NULL)) sub_424070(pc, PRIORITY_3, false, false);
                complete(items, peer, COOP_ITEM_INTERRUPTED);
            }
        }
        if (coop_world_net_retired(items->world)) return true;
        if (peer->phase == CAPTURING && coop_world_net_captures(items->world) >= peer->capture) {
            peer->revision = coop_world_net_revision(items->world);
            peer->phase = REPLY;
        }
        if (peer->phase == REPLY) {
            uint8_t p[RESULT_SIZE];
            coop_object_write32(p, items->epoch); coop_object_write32(p + 4, peer->sequence);
            coop_object_write32(p + 8, peer->status); coop_object_write32(p + 12, peer->revision);
            if (!sender) {
                items->last_status = peer->status; items->waiting_revision = peer->revision; items->received = true; peer->phase = SENT;
            } else if (coop_session_send(items->session, sender, COOP_ITEM_RESULT, p, sizeof(p))) peer->phase = SENT;
        }
        if (peer->access_event && coop_world_net_captures(items->world) >= peer->access_capture) {
            uint8_t p[ACCESS_SIZE]; CoopObjectId target = { 0 };
            if (peer->access_target.type != OID_TYPE_NULL && !coop_object_id_from_engine(peer->access_target, &target)) return false;
            coop_object_write32(p, items->epoch); coop_object_write32(p + 4, peer->access_generation);
            coop_object_write32(p + 8, coop_world_net_revision(items->world)); memcpy(p + 12, target.bytes, sizeof(target.bytes));
            p[29] = peer->access_steal;
            if (!sender) {
                items->access_received = peer->access_generation; items->access_revision = coop_world_net_revision(items->world); items->access_target = target;
                items->access_steal = peer->access_steal;
                peer->access_event = false;
            } else if (coop_session_send(items->session, sender, COOP_LOOT_ACCESS, p, sizeof(p))) peer->access_event = false;
        }
    }
    notify_result(items);
    if (items->access_received > items->access_applied && coop_world_net_applied_revision(items->world) >= items->access_revision) items->access_applied = items->access_received;
    if (items->access_applied > items->access_presented && inven_ui_coop_present_access(coop_items_loot_target(items), items->access_steal)) items->access_presented = items->access_applied;
    if (accepting && items->release_requested && !items->pending) {
        int64_t pc = player_get_local_pc_obj();
        CoopObjectId null_id = { 0 };
        if (pc && coop_items_submit_access(items, items->map, obj_get_id(pc).d.g.data, COOP_LOOT_CLOSE, null_id, 0, -1, items->release_generation)) items->release_requested = false;
    }
    return true;
}
bool coop_items_submit(CoopItems* items, int map, const uint8_t actor[16], CoopItemOperation op, CoopObjectId item, uint8_t recipient, int location)
{
    return coop_items_submit_access(items, map, actor, op, item, recipient, location, items->access_applied);
}
bool coop_items_submit_access(CoopItems* items, int map, const uint8_t actor[16], CoopItemOperation op, CoopObjectId item, uint8_t recipient, int location, uint32_t access)
{
    if (items->pending || items->submitted == UINT32_MAX || !actor || op < COOP_ITEM_PICKUP || op > COOP_ITEM_DROP_AMOUNT) return false;
    uint8_t p[REQUEST_SIZE];
    coop_object_write32(p, items->epoch); coop_object_write32(p + 4, items->submitted + 1); coop_object_write32(p + 8, (uint32_t)map);
    memcpy(p + 12, actor, 16); p[28] = (uint8_t)op; memcpy(p + 29, item.bytes, sizeof(item.bytes)); p[46] = recipient;
    coop_object_write32(p + 47, (uint32_t)location);
    coop_object_write32(p + 51, access);
    coop_object_write32(p + 55, combat_turn_generation_get());
    uint8_t slot = coop_session_local_slot(items->session);
    if (!(items->host ? coop_items_message(items, slot, COOP_ITEM_REQUEST, p, sizeof(p)) : coop_session_send(items->session, 0, COOP_ITEM_REQUEST, p, sizeof(p)))) return false;
    items->submitted++; items->pending = true; items->received = false; items->submitted_at = items->now;
    items->pending_approach = op == COOP_LOOT_OPEN || op == COOP_WORLD_USE;
    return true;
}
bool coop_items_busy(const CoopItems* items) { return items->pending; }
bool coop_items_settled(const CoopItems* items)
{
    if (items->pending) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) if ((items->peers[i].phase && items->peers[i].phase != SENT) || items->peers[i].access_event || items->peers[i].native_pending) return false;
    return true;
}
uint32_t coop_items_last_result(const CoopItems* items, unsigned* status) { *status = items->last_status; return items->last_result; }
unsigned coop_items_executed(const CoopItems* items) { return items->executed; }
unsigned coop_items_rejected(const CoopItems* items) { return items->rejected; }
int64_t coop_items_loot_target(const CoopItems* items)
{
    if (!items->access_applied || items->access_applied != items->access_received || !items->access_target.bytes[0]) return OBJ_HANDLE_NULL;
    return obj_pool_perm_lookup(coop_object_id_to_engine(items->access_target));
}
uint32_t coop_items_loot_generation(const CoopItems* items) { return items->access_applied; }
bool coop_items_has_access(const CoopItems* items, uint8_t sender, int64_t target)
{
    if (!items || !items->host || sender >= COOP_MAX_PLAYERS || !target) return false;
    const ItemPeer* peer = &items->peers[sender];
    return objid_is_equal(peer->access_target, obj_get_id(target)) && access_valid(items, peer, peer_actor(items, sender));
}
bool coop_items_stealing(const CoopItems* items) { return items->access_applied && items->access_applied == items->access_received && items->access_steal; }
void coop_items_release_loot(CoopItems* items)
{
    items->release_requested = items->access_target.bytes[0] != 0;
    items->release_generation = items->access_applied;
}
