#include "coop/services_probe.h"
#include "coop/hud.h"
#include "coop/saves_ui.h"
#include "coop/bootstrap.h"
#include "coop/engine_traps.h"
#include "game/anim.h"
#include "game/path.h"
#include "game/timeevent.h"
#include "game/tile.h"
#include "coop/runtime.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_service.h"
#include "coop/engine_craft.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_journal.h"
#include "coop/engine_notes.h"
#include "coop/lobby_ui.h"
#include "ui/charedit_ui.h"
#include "ui/textedit_ui.h"
#include "ui/wmap_ui.h"
#include "game/critter.h"
#include "game/combat.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tech.h"
#include "game/trap.h"
#include "ui/dialog_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/schematic_ui.h"
#include "ui/iso.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { STEPS = 38, PHASES = STEPS * 4, NPC_TAG = 187600, ITEM_TAG = 187601, AMMO_TAG = 187620 };
static unsigned first_step = 1, last_step = STEPS, first_owner, probe_owners = 4;
static bool recovery, callbacks_only, stack_mode;
static unsigned first_phase = 1;
static unsigned callback_checks, device_credit, device_reported;
static int device_damage[2];
static int64_t device_location;
static int64_t first_prototype(int64_t owner, int prototype);
enum { DEVICE_TARGET_TAG = 187650 };
static void damage_observed(const CombatContext* combat)
{
    if (!combat->target_obj || obj_field_int32_get(combat->target_obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || combat->total_dam <= 0) return;
    int kind = obj_field_int32_get(combat->target_obj, OBJ_F_NPC_PAD_I_1) - DEVICE_TARGET_TAG;
    if (kind < 0 || kind > 1) return;
    int member = coop_campaign_slot_for_object(combat->field_30);
    fprintf(stderr, "ARCMP native device damage kind=%d member=%d amount=%d hp_damage=%d\n", kind, member, combat->total_dam, object_hp_damage_get(combat->target_obj));
    if (member == (kind ? 0 : 3)) { device_credit |= 1u << kind; device_damage[kind] += combat->total_dam; }
}
static int64_t device_target(unsigned kind)
{
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && obj_field_int32_get(object, OBJ_F_NPC_PAD_I_1) == DEVICE_TARGET_TAG + (int)kind
            && !(obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) return object;
    } while (obj_inst_next(&object, &iterator));
    return 0;
}
static int64_t device_item(unsigned kind)
{
    return first_prototype(coop_campaign_object(kind ? 0 : 3), kind ? BP_DYNAMITE : BP_SPIKE_TRAP);
}
static CoopSession* held_service_session;
void coop_services_probe_cleanup(void)
{
    if (!held_service_session) return;
    combat_damage_observer_set(NULL);
    coop_runtime_detach();
    coop_session_destroy(held_service_session);
    held_service_session = NULL;
}
static unsigned phase_step(unsigned phase) { return (phase - 1) % (last_step - first_step + 1) + first_step; }
static unsigned phase_owner(unsigned phase) { return first_owner + (phase - 1) / (last_step - first_step + 1); }
static unsigned phase_count(void) { return (last_step - first_step + 1) * probe_owners; }
static bool component(int64_t owner, int prototype)
{
    int64_t item = 0;
    return object_create(sub_4685A0(prototype), obj_field_int64_get(owner, OBJ_F_LOCATION), &item) && item_transfer(item, owner);
}
static int count_prototype(int64_t owner, int prototype)
{
    int count = 0, length = obj_field_int32_get(owner, OBJ_F_CRITTER_INVENTORY_NUM);
    for (int index = 0; index < length; index++) {
        int64_t item = obj_arrayfield_handle_get(owner, OBJ_F_CRITTER_INVENTORY_LIST_IDX, index);
        if (obj_field_handle_get(item, OBJ_F_PROTOTYPE_HANDLE) == sub_4685A0(prototype)) count++;
    }
    return count;
}
static int64_t first_prototype(int64_t owner, int prototype)
{
    return sub_462540(owner, sub_4685A0(prototype), 7);
}
static int64_t ammunition(unsigned member)
{
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_AMMO
            && obj_field_int32_get(object, OBJ_F_ITEM_WORTH) == AMMO_TAG + (int)member) return object;
    } while (obj_inst_next(&object, &iterator));
    return 0;
}
static bool content(uint8_t id[32])
{
    FILE* f = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 };
    if (!f) return false;
    bool good = fread(hex, 1, 65, f) >= 64; fclose(f);
    for (unsigned i = 0; good && i < 32; i++) { char pair[] = { hex[2*i], hex[2*i+1], 0 }, *end; id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end; }
    return good;
}
static int64_t tagged(bool npc, unsigned member)
{
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        int type = obj_field_int32_get(object, OBJ_F_TYPE);
        if (npc ? type == OBJ_TYPE_NPC && obj_field_int32_get(object, OBJ_F_NPC_PAD_I_1) == NPC_TAG
                : obj_type_is_item(type) && obj_field_int32_get(object, OBJ_F_ITEM_WORTH) == ITEM_TAG + (int)member) return object;
    } while (obj_inst_next(&object, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool callback_destroyed(ObjectID id)
{
    int64_t obj = obj_pool_perm_lookup(id);
    return !obj || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT));
}
static unsigned callback_live_objects(void)
{
    unsigned count = 0; int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (!(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) count++;
    } while (obj_inst_next(&obj, &iterator));
    return count;
}
static bool callback_disposal_checks(int64_t pc, int64_t npc)
{
    Script veto = { .num = 999940 }, consume = { .num = 999938 }, none = {0};
    int64_t item, location = obj_field_int64_get(pc, OBJ_F_LOCATION);
    if (!object_create(sub_4685A0(BP_SWORD), location, &item) || !item_transfer(item, pc)) return false;
    ObjectID id = obj_get_id(item);
    obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &consume);
    object_destroy(item);
    if (!callback_destroyed(id)) return false;
    callback_checks++;
    for (unsigned test = 0; test < 2; test++) {
        int64_t source = test ? obj_field_handle_get(pc, OBJ_F_CRITTER_ARROWS) : obj_field_handle_get(npc, OBJ_F_CRITTER_GOLD);
        int64_t from = test ? pc : npc, to = test ? npc : pc;
        int before_from = test ? item_ammo_quantity_get(from, 0) : item_gold_get(from);
        int before_to = test ? item_ammo_quantity_get(to, 0) : item_gold_get(to);
        if (!source || before_from <= 0) return false;
        obj_arrayfield_script_set(source, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &veto);
        bool result = test ? item_ammo_transfer(from, to, before_from, 0, source) : item_gold_transfer(from, to, before_from, source);
        obj_arrayfield_script_set(source, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &none);
        if (result || before_from != (test ? item_ammo_quantity_get(from, 0) : item_gold_get(from))
            || before_to != (test ? item_ammo_quantity_get(to, 0) : item_gold_get(to))) return false;
        callback_checks++;
    }
    int arrows = item_ammo_quantity_get(pc, 0);
    item = item_ammo_create(7, 0, location);
    if (!item) return false;
    obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &veto);
    bool moved = item_transfer(item, pc);
    int64_t parent = OBJ_HANDLE_NULL;
    if (moved || item_parent(item, &parent) || item_ammo_quantity_get(pc, 0) != arrows) return false;
    obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &none);
    object_destroy(item);
    callback_checks++;
    int64_t herb = sub_462540(pc, sub_4685A0(10061), 7);
    if (!herb) return false;
    unsigned objects = callback_live_objects();
    int inventory = obj_field_int32_get(pc, OBJ_F_CRITTER_INVENTORY_NUM);
    obj_arrayfield_script_set(herb, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &veto);
    unsigned crafted = coop_engine_craft(pc, pc, (unsigned)tech_schematic_get(TECH_HERBOLOGY, 1));
    obj_arrayfield_script_set(herb, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &none);
    if (crafted != COOP_SERVICE_COMPONENTS || callback_live_objects() != objects
        || obj_field_int32_get(pc, OBJ_F_CRITTER_INVENTORY_NUM) != inventory) return false;
    callback_checks++;
    for (unsigned test = 0; test < 2; test++) {
        if (!object_create(sub_4685A0(test ? BP_DYNAMITE : BP_SPIKE_TRAP), location, &item) || !item_transfer(item, pc)) return false;
        unsigned before = callback_live_objects();
        obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &veto);
        bool placed = trap_use_at_loc(pc, item, location);
        if (placed || callback_live_objects() != before || !item_parent(item, &parent) || parent != pc) return false;
        obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &none);
        object_destroy(item);
        callback_checks++;
    }
    int64_t owner;
    if (!object_create(sub_4685A0(17067), location, &owner)) return false;
    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(owner, OBJ_F_SCRIPTS_IDX, sap, &none);
    if (!object_create(sub_4685A0(BP_SWORD), location, &item) || !item_transfer(item, owner)) return false;
    object_destroy(owner);
    sub_463B30(owner, false);
    if (obj_field_int32_get(owner, OBJ_F_CRITTER_INVENTORY_NUM)) return false;
    sub_43CF70(owner); object_delete(owner);
    callback_checks++;
    return true;
}
/* Private scripts use normal interpreter destruction and RUN_DEFAULT. These
 * negative controls deliberately avoid low-level object disposal. */
static bool callback_prepare(int64_t npc)
{
    int64_t pc = coop_campaign_object(2);
    int training = basic_skill_training_get(pc, BASIC_SKILL_HAGGLE);
    if (basic_skill_training_set(pc, BASIC_SKILL_HAGGLE, TRAINING_NONE) != TRAINING_NONE) return false;
    for (unsigned test = 0; test < 5; test++) {
        int64_t item;
        if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)) return false;
        if (test != 2 && test != 3 && !item_transfer(item, pc)) return false;
        ObjectID id = obj_get_id(item);
        Script script = { .num = 999936 + (int)(test < 3 ? test : 2) }, none = { 0 };
        int sap = test == 0 ? SAP_BUY_OBJECT : test == 1 ? SAP_TRANSFER : test == 2 ? SAP_GET : test == 3 ? SAP_INSERT_ITEM : SAP_REMOVE_ITEM;
        int64_t attached = test >= 2 ? item : npc;
        obj_arrayfield_script_set(attached, OBJ_F_SCRIPTS_IDX, sap, &script);
        bool result = test == 0 ? item_cost(item, pc, npc, false) != 0 : item_transfer(item, test == 1 || test == 4 ? npc : pc);
        bool rejected = result == (test == 3);
        if (test < 2) obj_arrayfield_script_set(attached, OBJ_F_SCRIPTS_IDX, sap, &none);
        int64_t live = obj_pool_perm_lookup(id);
        if (!rejected || (live && !(obj_field_int32_get(live, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)))) return false;
        callback_checks++;
    }
    for (unsigned test = 0; test < 2; test++) {
        int64_t source = test ? ammunition(2) : obj_field_handle_get(pc, OBJ_F_CRITTER_GOLD);
        if (!source) return false;
        ObjectID id = obj_get_id(source);
        int before = test ? item_ammo_quantity_get(npc, 0) : item_gold_get(npc);
        Script script = { .num = 999938 };
        obj_arrayfield_script_set(source, OBJ_F_SCRIPTS_IDX, SAP_INSERT_ITEM, &script);
        bool result = test ? item_ammo_transfer(pc, npc, 7, 0, source) : item_gold_transfer(pc, npc, 37, source);
        int64_t live = obj_pool_perm_lookup(id);
        if (result || (live && !(obj_field_int32_get(live, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)))
            || before != (test ? item_ammo_quantity_get(npc, 0) : item_gold_get(npc))) return false;
        if (test) {
            if (stack_mode && item_ammo_quantity_get(pc,1) && !item_ammo_transfer(pc,0,item_ammo_quantity_get(pc,1),1,0)) return false;
        int64_t ammo = item_ammo_create(100, stack_mode ? 1 : 0, obj_field_int64_get(pc, OBJ_F_LOCATION));
            if (!ammo || !item_transfer(ammo, pc)) return false;
            obj_field_int32_set(obj_field_handle_get(pc, OBJ_F_CRITTER_ARROWS), OBJ_F_ITEM_WORTH, AMMO_TAG + 2);
        } else if (!item_gold_transfer(OBJ_HANDLE_NULL, pc, 1000000, OBJ_HANDLE_NULL)) return false;
        callback_checks++;
    }
    if (callbacks_only) {
        /* An unaffordable or vetoed payment must not identify/repair the item.
         * Reuse the native destruction-veto script, then complete a paid service. */
        for (unsigned repair = 0; repair < 2; repair++) {
            int64_t item;
            if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)
                || !item_transfer(item, pc)) return false;
            obj_field_int32_set(item, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY, 50);
            item_flags_unset(item, OIF_IDENTIFIED);
            object_hp_damage_set(item, 5);
            int pc_gold = item_gold_get(pc), npc_gold = item_gold_get(npc), maximum = object_hp_max(item);
            bool (*service)(int64_t, int64_t, int64_t, int) = repair ? skill_perform_repair_service : item_perform_identify_service;
            for (unsigned rejected = 0; rejected < 3; rejected++) {
                int64_t coins = obj_field_handle_get(pc, OBJ_F_CRITTER_GOLD);
                Script veto = { .num = 999940 }, none = {0};
                if (rejected == 2) obj_arrayfield_script_set(coins, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &veto);
                bool result = service(item, npc, pc, rejected == 0 ? -1 : rejected == 1 ? pc_gold + 1 : pc_gold);
                if (rejected == 2) obj_arrayfield_script_set(coins, OBJ_F_SCRIPTS_IDX, SAP_DESTROY, &none);
                if (result || item_is_identified(item) || object_hp_damage_get(item) != 5 || object_hp_max(item) != maximum
                    || item_gold_get(pc) != pc_gold || item_gold_get(npc) != npc_gold) return false;
                callback_checks++;
            }
            if (!service(item, npc, pc, 37) || item_gold_get(pc) != pc_gold - 37 || item_gold_get(npc) != npc_gold + 37
                || (repair ? object_hp_damage_get(item) >= 5 || object_hp_max(item) != maximum : !item_is_identified(item))) return false;
            callback_checks++;
            object_destroy(item);
        }
        int64_t original_weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
        int inventory_count = obj_field_int32_get(pc, OBJ_F_CRITTER_INVENTORY_NUM);
        for (unsigned test = 0; test < 7; test++) {
            int64_t item, replacement = 0;
            if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)
                || !item_transfer(item, pc)) return false;
            if ((test == 2 || test == 5 || test == 6) && !item_wield_set(item, ITEM_INV_LOC_WEAPON)) return false;
            ObjectID id = obj_get_id(item);
            Script consume = { .num = 999938 };
            int sap = test == 0 ? SAP_DROP : test == 1 ? SAP_WIELD_ON
                : (test == 2 || test == 5 || test == 6) ? SAP_WIELD_OFF : SAP_REMOVE_ITEM;
            obj_arrayfield_script_set(item, OBJ_F_SCRIPTS_IDX, sap, &consume);
            bool result;
            if (test == 0 || test == 4) result = item_drop(item);
            else if (test == 2) result = sub_464C80(item);
            else if (test == 5) { item_rewield(pc); result = false; }
            else if (test == 6) {
                if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &replacement)
                    || !item_transfer(replacement, pc)) return false;
                result = item_wield_set(replacement, ITEM_INV_LOC_WEAPON);
                if (item_wield_get(pc, ITEM_INV_LOC_WEAPON) != replacement) return false;
            } else result = item_wield_set(item, ITEM_INV_LOC_WEAPON);
            int64_t live = obj_pool_perm_lookup(id);
            if (result != (test == 6) || (live && !(obj_field_int32_get(live, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)))) return false;
            if (replacement) object_destroy(replacement);
            callback_checks++;
        }
        /* The same native paths still complete an ordinary equip/unequip/drop. */
        int64_t item;
        if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item)
            || !item_transfer(item, pc) || !item_wield_set(item, ITEM_INV_LOC_WEAPON)
            || !sub_464C80(item) || !item_drop(item)) return false;
        object_destroy(item);
        /* RUN_DEFAULT after destruction must retain the script's result
         * without invoking or charging against a vanished native object. */
        for (unsigned test = 0; test < 3; test++) {
            int64_t actor = pc, target = pc, used;
            if (test == 1 && !object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &target)) return false;
            if (test == 2 && !object_create(sub_4685A0(BP_WOLF), obj_field_int64_get(pc, OBJ_F_LOCATION), &actor)) return false;
            if (!object_create(sub_4685A0(test == 2 ? BP_SWORD : BP_SCROLL_OF_STRENGTH_OF_EARTH), obj_field_int64_get(pc, OBJ_F_LOCATION), &used)
                || !item_transfer(used, actor)) return false;
            ObjectID used_id = obj_get_id(used), victim_id = obj_get_id(test == 0 ? used : test == 1 ? target : actor);
            Script script = { .num = test == 0 ? 999938 : test == 1 ? 999937 : 999936 };
            obj_arrayfield_script_set(used, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
            int result = item_use_on_obj_authoritative(actor, used, target, false);
            int64_t victim = obj_pool_perm_lookup(victim_id);
            if (result != COOP_MAGIC_OK || (victim && !(obj_field_int32_get(victim, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)))) {
                fprintf(stderr, "ARCMP callback use test=%u result=%d victim-live=%d\n", test, result, victim != 0);
                return false;
            }
            int64_t remaining = obj_pool_perm_lookup(used_id);
            if (remaining && !(obj_field_int32_get(remaining, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) object_destroy(remaining);
            callback_checks++;
        }
        if (!callback_disposal_checks(pc, npc)) {
            fprintf(stderr, "ARCMP extended native callback failure after=%u controls\n", callback_checks);
            return false;
        }
        if (original_weapon && !item_wield_set(original_weapon, ITEM_INV_LOC_WEAPON)) return false;
        if (obj_field_int32_get(pc, OBJ_F_CRITTER_INVENTORY_NUM) != inventory_count) return false;
        callback_checks++;
    }
    for (int tier = TRAINING_APPRENTICE; tier <= training; tier++)
        if (basic_skill_training_set(pc, BASIC_SKILL_HAGGLE, tier) != tier) return false;
    return true;
}
static uint32_t panel_crc(const CoopDialogueView* view)
{
    CoopDialogueView copy = *view; copy.conversation = copy.revision = 1;
    size_t size = 0; uint8_t* bytes = coop_dialogue_view_encode(&copy, &size);
    uint32_t crc = bytes ? coop_snapshot_crc(bytes, size) : 0; free(bytes); return crc;
}
static bool panel_step(unsigned step)
{
    return step == 1 || step == 8 || step == 13 || step == 30 || step == 33 || step == 39 || step == 41;
}
static bool prepare(void)
{
    int64_t npc = 0, object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        Script script = { 0 }; obj_arrayfield_script_get(object, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) npc = object;
        object_flags_set(object, OF_OFF);
    } while (obj_inst_next(&object, &iterator));
    if (!npc) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
    Script none = { 0 };
    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(npc, OBJ_F_SCRIPTS_IDX, sap, &none);
    object_flags_unset(npc, OF_OFF); critter_leader_set(npc, OBJ_HANDLE_NULL);
    obj_field_int32_set(npc, OBJ_F_NPC_PAD_I_1, NPC_TAG);
    stat_base_set(npc, STAT_INTELLIGENCE, 20);
    /* A nonzero replicated counter catches reporting after authority teardown. */
    if (!sub_4B6C90(true) || !sub_4B6C90(false) || !combat_turn_generation_get()) return false;
    if (stack_mode && item_gold_get(npc) && !item_gold_transfer(npc,0,item_gold_get(npc),0)) return false;
    if (!item_gold_transfer(OBJ_HANDLE_NULL, npc, 1000000, 0) || tech_degree_inc(npc, TECH_HERBOLOGY) != 1) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
    if (tech_skill_points_set(npc, TECH_SKILL_REPAIR, 5) != 5) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
    for (int tier = TRAINING_APPRENTICE; tier <= TRAINING_MASTER; tier++) if (tech_skill_training_set(npc, TECH_SKILL_REPAIR, tier) != tier) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i), item = 0;
        stat_base_set(pc, STAT_INTELLIGENCE, 20);
        stat_base_set(pc, basic_skill_stat(BASIC_SKILL_HAGGLE), 20);
        int64_t origin = obj_field_int64_get(npc, OBJ_F_LOCATION);
        sub_43E770(pc, location_make(location_get_x(origin) + 2 + (int)i % 2, location_get_y(origin) + 2 + (int)i / 2), 0, 0);
        if (!tech_degree_get(pc, TECH_HERBOLOGY) && tech_degree_inc(pc, TECH_HERBOLOGY) != 1) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        if (basic_skill_points_set(pc, BASIC_SKILL_HAGGLE, 5) != 5) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        for (int tier = TRAINING_APPRENTICE; tier <= TRAINING_EXPERT; tier++)
            if (basic_skill_training_set(pc, BASIC_SKILL_HAGGLE, tier) != tier) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        int gold = item_gold_get(pc);
        if (gold && !item_gold_transfer(pc, OBJ_HANDLE_NULL, gold, OBJ_HANDLE_NULL)) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        if (!item_gold_transfer(OBJ_HANDLE_NULL, pc, 1000000, OBJ_HANDLE_NULL)) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(pc, OBJ_F_LOCATION), &item) || !item_transfer(item, pc)) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        obj_field_int32_set(item, OBJ_F_ITEM_WORTH, ITEM_TAG + (int)i);
        obj_field_int32_set(item, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY, 50);
        item_flags_unset(item, OIF_IDENTIFIED); object_hp_damage_set(item, 5);
        if (first_step > 12) { item_flags_set(item, OIF_IDENTIFIED); object_hp_damage_set(item, 0); }
        int64_t ammo = item_ammo_create(100, stack_mode ? 1 : 0, obj_field_int64_get(pc, OBJ_F_LOCATION));
        if (!ammo || !item_transfer(ammo, pc)) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
        ammo = sub_462540(pc,sub_4685A0(stack_mode ? BP_BULLET : BP_ARROW),0); obj_field_int32_set(ammo, OBJ_F_ITEM_WORTH, AMMO_TAG + (int)i);
        if (!component(pc, 10061) || !component(pc, 10062)) { fprintf(stderr, "ARCMP service prepare failed line=%d\n", __LINE__); return false; }
    }
    if (stack_mode) {
        int64_t pc=coop_campaign_object(1),loc=obj_field_int64_get(pc,OBJ_F_LOCATION),chest=0;
        int64_t ammo=item_ammo_create(50,1,loc);
        if (!ammo || !item_transfer(ammo,npc) || !object_create(sub_4685A0(BP_WOOD_CHEST_1),location_make(location_get_x(loc)+1,location_get_y(loc)),&chest)) return false;
        obj_field_int32_set(chest,OBJ_F_CONTAINER_PAD_I_1,187630);
        obj_field_int32_set(chest,OBJ_F_CONTAINER_INVENTORY_SOURCE,0); obj_field_int32_set(chest,OBJ_F_CONTAINER_FLAGS,0);
        ammo=item_ammo_create(37,1,loc); int64_t coins=item_gold_create(100,loc);
        if (!ammo || !coins || !item_transfer(ammo,chest) || !item_transfer(coins,chest)) return false;
    }
    return !recovery || callback_prepare(npc);
}
static bool device_prepare(unsigned kind)
{
    if (kind && device_target(0)) object_destroy(device_target(0));
    int64_t owner = coop_campaign_object(kind ? 0 : 3), from = obj_field_int64_get(owner, OBJ_F_LOCATION);
    int64_t spawn = 0;
    for (int dy = -12; dy <= 12 && !spawn; dy++) for (int dx = -12; dx <= 12 && !spawn; dx++) {
        if (abs(dx) < 6 && abs(dy) < 6) continue;
        int64_t loc = location_make(location_get_x(from) + dx, location_get_y(from) + dy);
        int64_t start = kind ? loc : location_make(location_get_x(loc) + 1, location_get_y(loc));
        int8_t steps[128]; PathCreateInfo path = { .obj = owner, .from = from, .to = loc, .max_rotations = 128, .rotations = steps };
        if (tile_is_blocking(loc, false) || tile_is_blocking(start, false) || path_make(&path) <= 0) continue;
        path.from = start;
        if (!kind && path_make(&path) <= 0) continue;
        device_location = loc; spawn = start;
    }
    int64_t target;
    if (!spawn || !object_create(sub_4685A0(17067), spawn, &target)) { fprintf(stderr, "ARCMP device setup failed kind=%u spawn=%lld from=%lld,%lld\n", kind, (long long)spawn, (long long)location_get_x(from), (long long)location_get_y(from)); return false; }
    Script none = { 0 }; for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(target, OBJ_F_SCRIPTS_IDX, sap, &none);
    critter_leader_set(target, OBJ_HANDLE_NULL);
    object_flags_unset(target, OF_INVULNERABLE); object_flags_set(target, OF_OFF);
    obj_field_int32_set(target, OBJ_F_NPC_PAD_I_1, DEVICE_TARGET_TAG + (int)kind);
    obj_field_int32_set(target, OBJ_F_HP_PTS, 500); object_hp_damage_set(target, 0);
    obj_field_int32_set(target, OBJ_F_CRITTER_FATIGUE_PTS, 500); critter_fatigue_damage_set(target, 0);
    for (unsigned member = 0; member < 4; member++) {
        int64_t pc = coop_campaign_object(member);
        obj_field_int32_set(pc, OBJ_F_HP_PTS, 5000); object_hp_damage_set(pc, 0);
        obj_field_int32_set(pc, OBJ_F_CRITTER_FATIGUE_PTS, 5000); critter_fatigue_damage_set(pc, 0);
    }
    bool supplied = component(owner, kind ? BP_DYNAMITE : BP_SPIKE_TRAP);
    fprintf(stderr, "ARCMP device prepared kind=%u supplied=%d location=%lld,%lld target=%lld\n", kind, supplied, (long long)location_get_x(device_location), (long long)location_get_y(device_location), (long long)target);
    return supplied;
}
static bool bomb_event(TimeEvent* event)
{
    return event->params[0].integer_value == 176 && event->params[3].integer_value == 1;
}
static bool placed_trap(void)
{
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_TRAP && coop_engine_trap_owner(object) == coop_campaign_object(3)
            && (obj_field_int32_get(object, OBJ_F_FLAGS) & OF_TRAP_PC)) return true;
    } while (obj_inst_next(&object, &iterator));
    return false;
}
static bool announce(CoopSession* session, unsigned phase)
{
    unsigned step = phase_step(phase), member = phase_owner(phase);
    if (panel_step(step) && (!stack_mode || step==13)) {
        int64_t pc = coop_campaign_object(member), npc = tagged(true, 0);
        if (!pc || !npc) return false;
        int64_t origin = obj_field_int64_get(npc, OBJ_F_LOCATION);
        sub_43E770(pc, location_make(location_get_x(origin) + 2, location_get_y(origin) + 2), 0, 0);
        if (step == 1) critter_leader_set(npc, OBJ_HANDLE_NULL);
        if (step == 30) {
            critter_leader_set(npc, pc);
            if (!component(npc, 10061) || !component(npc, 10062)) return false;
        }
        dialog_ui_start_dialog(pc, npc, step >= 39 ? 999939 : step == 1 ? 3029 : step == 30 ? 1046 : 1824, 0, step == 30 ? 400 : 1);
    }
    if (step == 58 || step == 62) {
        if (!device_prepare(step == 62)) return false;
        char text[100]; snprintf(text, sizeof(text), "service-device:%lld:%lld", (long long)location_get_x(device_location), (long long)location_get_y(device_location));
        if (!coop_session_chat(session, text)) return false;
    }
    if (step == 66) {
        if (device_target(1)) object_destroy(device_target(1));
        for (unsigned i = 0; i < 4; i++) if (combat_critter_is_combat_mode_active(coop_campaign_object(i))) combat_critter_deactivate_combat_mode(coop_campaign_object(i));
    }
    if (stack_mode && step>=27 && step<=31) script_global_var_set(0,step<=30 ? 60000+(int)step : 99999);
    if (stack_mode && step==33) script_global_var_set(0,99933);
    char message[64]; snprintf(message, sizeof(message), "service-phase:%u", phase);
    return coop_session_chat(session, message);
}
static bool image(unsigned phase, bool framebuffer)
{
    if (!framebuffer) return true;
    gamelib_redraw(); iso_redraw(); tig_window_display();
    char name[80]; snprintf(name, sizeof(name), "arcmp-service-%u.bmp", phase);
    return tig_video_screenshot_save(name) == TIG_OK;
}
static void key(unsigned index)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD }; m.data.keyboard.scancode = SDL_SCANCODE_1 + (int)index; m.data.keyboard.key = SDLK_1 + (int)index;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static bool item_click(int64_t item, bool buy)
{
    int slot = item_inventory_location_get(item); if (slot < 0 || slot >= 120) return false;
    TigMessage m = { .type = TIG_MESSAGE_MOUSE };
    int64_t parent = 0; item_parent(item, &parent);
    bool own = parent == player_get_local_pc_obj();
    m.data.mouse.x = (own ? 368 : 8) + (slot % 10) * 32 + 8; m.data.mouse.y = (own ? 49 : 209) + (slot / 10) * 32 + 8;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_IDLE; tig_message_enqueue(&m);
    if (buy) { m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m); m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m); }
    return true;
}
static void keyboard(SDL_Scancode code, SDL_Keycode key)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD }; m.data.keyboard.scancode = code; m.data.keyboard.key = key;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static void number(unsigned value)
{
    char digits[16]; snprintf(digits, sizeof(digits), "%u", value);
    for (const char* p = digits; *p; p++) keyboard(*p == '0' ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + *p - '1', SDLK_0 + *p - '0');
    keyboard(SDL_SCANCODE_RETURN, SDLK_RETURN);
}
static void combine(void)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE }; m.data.mouse.x = 100; m.data.mouse.y = 385;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
static bool note_click(uint32_t id)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE }; m.data.mouse.x = 300; m.data.mouse.y = 250;
    if (id && !wmap_ui_coop_note_position(id, &m.data.mouse.x, &m.data.mouse.y)) return false;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
    return true;
}
static void note_text(const char* text, size_t previous)
{
    keyboard(SDL_SCANCODE_HOME, SDLK_HOME);
    for (size_t i = 0; i < previous; i++) keyboard(SDL_SCANCODE_DELETE, SDLK_DELETE);
    TigMessage m = { .type = TIG_MESSAGE_TEXT_INPUT }; m.data.text.text = text; tig_message_enqueue(&m);
}
static const CoopMapNote* note_text_find(const char* text)
{
    const CoopMapNotes* notes = coop_engine_notes();
    for (unsigned i = 0; i < notes->count; i++) if (!strcmp(notes->records[i].text, text)) return &notes->records[i];
    return NULL;
}
bool coop_services_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false; unsigned port = 35742, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-commerce")) { first_step = 13; probe_owners = 1; }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-followers")) { first_step = 30; probe_owners = 1; }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-service-recovery")) { recovery = true; first_owner = 2; probe_owners = 1; last_step = 68; }
        if (!strncmp(argv[i], "--coop-service-first=", 21)) first_phase = (unsigned)atoi(argv[i] + 21);
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-callbacks")) { recovery = callbacks_only = true; }
        if (!strcmp(argv[i], "--coop-probe-stack-confirm")) { stack_mode=true; first_step=13; last_step=33; first_owner=1; probe_owners=1; }
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0) || (first_phase != 1 && (!recovery || (first_phase != 40 && first_phase != 43 && first_phase != 58)))) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Service player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    CoopSession* session = coop_session_create(&config); if (!session) return false;
    held_service_session = session;
    if (stack_mode) coop_runtime_probe_enable();
    if (recovery) { coop_runtime_probe_enable(); if (host) combat_damage_observer_set(damage_observed); }
    uint64_t start = SDL_GetTicks(), playing = 0, at = 0, chats = 0, quiet = 0, ended = 0;
    bool good = (host ? coop_session_host(session, (uint16_t)port, start) : coop_session_join(session, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(session, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    bool selected = false, initialized = false, sent = false, done = false, finishing = false, replayed = false, guarded = false, quantity_input = false;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, replays = 0, denials = 0, stack_services=0, stack_items=0;
    int member = -1, gold = 0, fee = 0, npc_gold = 0, ammo_before = 0, other_before = 0, product_before = 0, component1 = 0, component2 = 0;
    uint32_t expected = 0, item_expected = 0; CoopServiceRequest request = { 0 };
    unsigned baseline_epoch = 0, recoveries = 0, drops = 0, panel_pages = 0, conflicts = 0;
    uint32_t crc = 0, note_expected = 0, original_note = 0;
    uint64_t panel_seen = 0;
    bool dropped = false, voted = false, panel_verified = false, quick_layout=false;
    while (good && SDL_GetTicks() - start < 360000) {
        uint64_t now = SDL_GetTicks(); tig_ping(); coop_session_poll(session, now); if (recovery) coop_lobby_tick(); else coop_runtime_tick(now);
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP service probe runtime: %s\n", coop_runtime_status()); good = false; break; }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name, sizeof(name), "Service hero %u", player); selected = coop_session_character(session, 16067 + player, name); }
            else if (coop_session_players(session)[coop_session_local_slot(session)].prototype && !coop_session_players(session)[coop_session_local_slot(session)].ready) coop_session_ready(session, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(session)[i].connected;
            if (host && count == 4 && coop_session_all_ready(session)) good = first_phase > 1 ? coop_runtime_recover_checkpoint() : coop_runtime_start();
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            CoopServices* service = coop_gameplay_services(game); CoopDialogue* dialogue = coop_gameplay_dialogue(game);
            const CoopDialogueView* view = coop_dialogue_current(dialogue);
            if (!playing) { playing = now; member = coop_campaign_slot_for_owner(config.identity); }
            if (stack_mode && !quick_layout) {
                TigRect rect={600,340,156,44}; good &= coop_hud_store(HUD_LOG_BUTTON,&rect); quick_layout=true;
            }
            if (!initialized && host && (first_phase == 40 || !view->active) && now - playing > 1300) {
                initialized = true; good = first_phase > 1 || prepare();
                if (good) { if (callbacks_only) { good = coop_gameplay_finish(game); finishing = true; } else good = announce(session, first_phase); }
            }
            if (chats > coop_session_chat_count(session)) chats = 0;
            for (; chats < coop_session_chat_count(session); chats++) {
                const CoopChat* chat = coop_session_chat_at(session, chats); unsigned value; long long x, y;
                if (!chat->slot && sscanf(chat->text, "service-device:%lld:%lld", &x, &y) == 2) device_location = location_make(x, y);
                unsigned credit; int trap_damage, bomb_damage;
                if (!chat->slot && sscanf(chat->text, "service-credit:%u:%d:%d", &credit, &trap_damage, &bomb_damage) == 3) {
                    device_credit = credit; device_damage[0] = trap_damage; device_damage[1] = bomb_damage;
                }
                if (!chat->slot && sscanf(chat->text, "service-phase:%u", &value) == 1) {
                    if (recovery && value <= phase) continue;
                    if (value != (phase ? phase + 1 : first_phase) || value > phase_count()) { good = false; break; }
                    phase = value; at = now; baseline_epoch = coop_runtime_epoch(); dropped = voted = panel_verified = false; crc = 0; panel_seen = 0; quiet = 0; sent = done = replayed = quantity_input = false;
                    unsigned status; expected = coop_services_last_result(service, &status) + 1;
                    note_expected = coop_notes_last_result(coop_gameplay_notes(game), &status) + 1;
                    item_expected = coop_items_last_result(coop_gameplay_items(game), &status) + 1;
                    fprintf(stderr, "ARCMP service probe phase=%u member=%d expected=%u\n", phase, member, expected);
                }
                if (chat->slot < 4 && sscanf(chat->text, "service-done:%u", &value) == 1) ack[chat->slot] = value;
            }
            unsigned step = phase ? phase_step(phase) : 0, owner = phase ? phase_owner(phase) : 0;
            bool own = member >= 0 && (unsigned)member == owner;
            int64_t pc = phase ? coop_campaign_object(owner) : 0, item = phase ? tagged(false, owner) : 0;
            if (pc && item && !host && !guarded) {
                CoopServiceQuote q = { 0 };
                if (coop_engine_service_quote(pc, tagged(true, 0), item, view->mode, &q) != COOP_SERVICE_UNAVAILABLE
                    || coop_engine_service_buy(pc, tagged(true, 0), item, &q) != COOP_SERVICE_UNAVAILABLE
                    || coop_engine_craft(pc, pc, 2000) != COOP_SERVICE_UNAVAILABLE) { good = false; break; }
                guarded = true;
            }
            if (!phase && view->active && view->chooser == coop_session_local_slot(session)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (phase && step <= 42 && !done && now - at > 500 && pc && item && (!stack_mode || step<18)) {
                unsigned wanted = step == 4 ? COOP_SERVICE_IDENTIFIED : step == 5 || step == 6 || step == 25 ? COOP_SERVICE_OWNERSHIP
                    : step == 11 ? COOP_SERVICE_UNDAMAGED : step == 23 ? COOP_SERVICE_COMPONENTS : step == 24 ? COOP_SERVICE_KNOWLEDGE
                    : step == 29 ? COOP_ITEM_TARGET : 0;
                bool purchase = step == 3 || step == 10 || step == 15 || step == 17 || step == 19 || step == 35 || step == 37;
                bool craft = step == 22 || step == 23 || step == 24 || step == 25 || step == 31;
                int64_t npc = tagged(true, 0), recipient = coop_campaign_object((owner + 1) % 4), ammo = ammunition(owner);
                if (own && !sent) {
                    gold = item_gold_get(pc); npc_gold = item_gold_get(npc);
                    ammo_before = item_ammo_quantity_get(pc, 0);
                    int64_t crafter = step == 31 ? npc : pc;
                    product_before = count_prototype(crafter, 10059); component1 = count_prototype(crafter, 10061); component2 = count_prototype(crafter, 10062);
                    if (panel_step(step)) {
                        if (!view->active || view->chooser != coop_session_local_slot(session) || !view->option_count) { SDL_Delay(2); continue; }
                        /* B: is the first option of both controlled retail
                         * pages. Its rendered text is randomized by native MES. */
                        unsigned choice = step >= 39 ? step == 39 ? 0 : 1 : step == 13 || step == 33 ? 0 : step == 30 ? 3 : 5;
                        if (choice == 5) for (unsigned i = 0; i < view->option_count; i++) if (strstr(view->options[i], step == 1 ? "identify" : step == 8 ? "repair" : "make")) { choice = i; break; }
                        if (choice == 5) { fprintf(stderr, "ARCMP service missing option step=%u\n", step); for (unsigned i=0;i<view->option_count;i++) fprintf(stderr,"option=%s\n",view->options[i]); good = false; break; } key(choice);
                    } else if (step == 40 || step == 42) keyboard(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                    else if (step == 7 || step == 12 || step == 20 || step == 38) inven_ui_destroy();
                    else if (step == 21) keyboard(SDL_SCANCODE_T, SDLK_T);
                    else if (step == 26 || step == 32) schematic_ui_close();
                    else if (step >= 27 && step <= 29) {
                        int64_t given = step == 27 ? obj_field_handle_get(pc, OBJ_F_CRITTER_GOLD) : ammo;
                        if (!given) { good = false; break; }
                        other_before = step == 27 ? item_gold_get(recipient) : item_ammo_quantity_get(recipient, 0);
                        if (step == 29) {
                            CoopObjectId id; coop_object_id_from_engine(obj_get_id(given), &id);
                            good = coop_items_submit(coop_gameplay_items(game), map_current_map(), obj_get_id(pc).d.g.data, COOP_ITEM_GIVE_AMOUNT, id, (uint8_t)((owner + 1) % 4), INT32_MAX);
                        } else {
                            sub_573630(given); TargetDescriptor target; target_descriptor_set_obj(&target, recipient);
                            good = inven_ui_coop_world_click(&target);
                        }
                    }
                    else {
                        if (!craft && !inven_ui_is_created()) { good = false; break; }
                        request = (CoopServiceRequest) { .epoch = coop_runtime_epoch(), .sequence = expected, .map = (uint32_t)map_current_map(),
                            .conversation = craft && step != 31 ? 0 : view->conversation, .page = craft && step != 31 ? 0 : view->revision,
                            .operation = craft ? COOP_SERVICE_CRAFT : purchase ? COOP_SERVICE_BUY : COOP_SERVICE_QUOTE,
                            .quantity = step == 18 || step == 19 ? 7 : step == 36 || step == 37 ? 13 : 1, .recipe = craft ? step == 24 ? 999999 : 2000 : 0 };
                        int64_t selected_item = step == 18 || step == 19 ? ammo : step == 34 || step == 35 ? first_prototype(npc, 10059)
                            : step == 36 || step == 37 ? obj_field_handle_get(pc, OBJ_F_CRITTER_GOLD) : item;
                        if (!selected_item) { good = false; break; }
                        memcpy(request.actor, obj_get_id(pc).d.g.data, 16); coop_object_id_from_engine(obj_get_id(item), &request.item);
                        if (!craft) coop_object_id_from_engine(obj_get_id(selected_item), &request.item);
                        if (craft) coop_object_id_from_engine(obj_get_id(crafter), &request.item);
                        if (step == 18 || step == 19) coop_object_id_from_engine(obj_get_id(ammo), &request.item);
                        if (purchase) {
                            int price; request.quote = coop_services_quote(service, &price);
                            fee = price; npc_gold = item_gold_get(tagged(true, 0));
                            if (!request.quote || !item_click(selected_item, true)) { good = false; break; }
                        } else if (step == 2 || step == 9 || step == 14 || step == 16 || step == 18 || step == 34 || step == 36) {
                            if (!item_click(selected_item, step == 18 || step == 36)) { good = false; break; }
                        } else if (step == 22 || step == 31) combine();
                        else {
                            if (step == 5) coop_object_id_from_engine(obj_get_id(tagged(false, (owner + 1) % 4)), &request.item);
                            if (step == 6 || step == 25) memcpy(request.actor, obj_get_id(recipient).d.g.data, 16);
                            if (!coop_services_submit(service, &request)) { good = false; break; }
                        }
                    }
                    sent = true;
                }
                if (own && sent && !quantity_input && (step == 18 || step == 27 || step == 28 || step == 36) && intgame_mode_get() == INTGAME_MODE_QUANTITY) {
                    if ((owner == 0 || owner == 2) && step == 27 && !image(phase, framebuffer)) { good = false; break; }
                    number(step == 18 ? 7 : step == 27 ? 37 : step == 36 ? 13 : 11); quantity_input = true;
                }
                bool ready = !own || sent;
                if (step == 1 || step == 8 || step == 13 || step == 33) ready &= view->active && view->mode == (step == 1 ? 8u : step == 8 ? 9u : 3u)
                    && (inven_ui_is_created() != 0) == own;
                else if (step == 39 || step == 41) ready &= view->active && view->mode == (step == 39 ? 5u : 6u)
                    && (step == 39 ? charedit_is_created() != 0 : wmap_ui_is_created() != 0) == own;
                else if (step == 40 || step == 42) ready &= !view->active && !charedit_is_created() && !wmap_ui_is_created();
                else if (step == 30) ready &= view->active && view->mode == 7 && (intgame_mode_get() == INTGAME_MODE_SCHEMATIC) == own;
                else if (step == 7 || step == 12 || step == 20 || step == 32 || step == 38) ready &= !view->active && !inven_ui_is_created();
                else if (step == 21) { if (own) ready &= intgame_mode_get() == INTGAME_MODE_SCHEMATIC; }
                else if (step == 26) { if (own) ready &= intgame_mode_get() != INTGAME_MODE_SCHEMATIC; }
                else if (step >= 27 && step <= 29) {
                    if (own) {
                        unsigned status; ready &= coop_items_last_result(coop_gameplay_items(game), &status) == item_expected;
                        if (ready && status != wanted) { good = false; break; }
                        if (step == 27) ready &= quantity_input && item_gold_get(pc) == gold - 37 && item_gold_get(recipient) == other_before + 37;
                        else if (step == 28) ready &= quantity_input && item_ammo_quantity_get(pc, 0) == ammo_before - 11 && item_ammo_quantity_get(recipient, 0) == other_before + 11;
                        else ready &= item_ammo_quantity_get(pc, 0) == ammo_before && item_ammo_quantity_get(recipient, 0) == other_before;
                    }
                }
                else if (own) {
                    unsigned status; uint32_t result = coop_services_last_result(service, &status);
                    ready &= result == expected;
                    if (ready && status != wanted) { fprintf(stderr, "ARCMP service probe wrong status phase=%u status=%u wanted=%u\n", phase, status, wanted); good = false; break; }
                    if (ready && wanted) { ready &= item_gold_get(pc) == gold; }
                    if (ready && purchase) {
                        bool selling = step == 15 || step == 19;
                        if (step == 37) ready &= !fee && item_gold_get(pc) == gold - 13 && item_gold_get(npc) == npc_gold + 13;
                        else ready &= (step == 35 ? fee == 0 : fee > 0) && item_gold_get(pc) == gold + (selling ? fee : -fee) && item_gold_get(npc) == npc_gold + (selling ? -fee : fee);
                        int64_t parent = 0; item_parent(item, &parent);
                        if (step == 15 || step == 17) ready &= parent == (step == 15 ? npc : pc);
                        if (step == 19) ready &= item_ammo_quantity_get(pc, 0) == ammo_before - 7;
                        if (step == 35) ready &= count_prototype(pc, 10059) == product_before + 1;
                    }
                    if (ready && (step == 22 || step == 31)) {
                        int64_t crafter = step == 31 ? npc : pc;
                        ready &= count_prototype(crafter, 10059) == product_before + 5 && count_prototype(crafter, 10061) == component1 - 1 && count_prototype(crafter, 10062) == component2 - 1;
                    }
                    if (ready && (purchase || step == 22 || step == 31) && !replayed) {
                        uint8_t bytes[COOP_SERVICE_REQUEST_BYTES];
                        if (!coop_service_request_encode(&request, bytes) || !(host ? coop_services_message(service, 0, COOP_SERVICE_REQUEST, bytes, sizeof(bytes)) : coop_session_send(session, 0, COOP_SERVICE_REQUEST, bytes, sizeof(bytes)))) { good = false; break; }
                        replayed = true; replays++; quiet = now; ready = false;
                    }
                    if (replayed && now - quiet < 500) ready = false;
                }
                if (step >= 3 && step <= 7) ready &= item_is_identified(item);
                if (step >= 10) ready &= object_hp_damage_get(item) == 0;
                if (recovery && panel_step(step)) {
                    if (ready && !panel_seen) {
                        crc = panel_crc(view); panel_pages = coop_journal_count(coop_engine_journal()); panel_seen = now;
                    }
                    if (own && ready && !dropped && panel_seen && now - panel_seen > 700 && coop_runtime_epoch() == baseline_epoch) {
                        coop_runtime_probe_retry_delay(1000);
                        good &= !host && coop_session_drop_transport(session); dropped = true; drops++;
                    }
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && panel_seen;
                    if (ready && !panel_verified) {
                        panel_verified = true;
                        good &= panel_crc(view) == crc && coop_journal_count(coop_engine_journal()) == panel_pages;
                        recoveries++;
                        fprintf(stderr, "ARCMP restored panel step=%u mode=%u crc=%u pages=%u epoch=%u\n", step, view->mode, crc, panel_pages, coop_runtime_epoch());
                        if (own) good &= image(phase, framebuffer);
                    }
                    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                    if (host && panel_verified && control && control->paused && !voted)
                        voted = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    ready &= panel_verified && control && !control->paused;
                }
                if (ready) {
                    if (own && wanted) denials++;
                    bool capture = (owner == 0 || owner == 2) && ((own && (step == 3 || step == 10 || step == 14 || step == 18 || step == 22 || step == 30 || step == 31))
                        || (!own && member == (int)((owner + 1) % 4) && (step == 13 || step == 30)));
                    if (capture
                        && !image(phase, framebuffer)) { good = false; break; }
                    char text[64]; snprintf(text, sizeof(text), "service-done:%u", phase); good = good && coop_session_chat(session, text); done = true; completed = phase;
                }
            }
            if (recovery && phase && step >= 43 && step <= 57 && !done && now - at > 600) {
                bool actor = member == ((step == 48 || step == 49 || step == 50 || step == 51 || step == 53 || step == 54 || step == 56 || step == 57) ? 3 : 1);
                bool both = step == 45 || step == 46;
                if (both) actor = member == 1 || member == 3;
                if (step == 55) actor = wmap_ui_is_created() != 0;
                const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                if (!sent && actor) {
                    bool issued = true;
                    if (step == 43 || step == 45 || step == 50) {
                        if (!wmap_ui_is_created()) { keyboard(SDL_SCANCODE_W, SDLK_W); keyboard(SDL_SCANCODE_N, SDLK_N); issued = false; }
                        else issued = note_click(step == 43 ? 0 : original_note);
                    } else if (step == 44) { note_text("Shared native note", 0); keyboard(SDL_SCANCODE_RETURN, SDLK_RETURN); }
                    else if (step == 46) note_text(member == 1 ? "First draft" : "Second draft", strlen("Shared native note"));
                    else if (step == 51) note_text("Recreated draft", strlen("Second draft"));
                    else if (step == 52) keyboard(SDL_SCANCODE_DELETE, SDLK_DELETE);
                    else if (step == 55) keyboard(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
                    else if (step == 56 || step == 57) {
                        if (coop_runtime_control(step == 56 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0)) sent = true;
                    } else {
                        if (step == 49 || step == 54) SDL_SetModState(SDL_KMOD_CTRL);
                        keyboard(SDL_SCANCODE_RETURN, SDLK_RETURN);
                    }
                    if (step < 56 && issued) sent = true;
                }
                if (step == 57 && member != 3 && !voted && control && control->phase == COOP_CONTROL_VOTING)
                    voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                bool ready = !actor || sent;
                unsigned status = 0;
                bool result = coop_notes_last_result(coop_gameplay_notes(game), &status) == note_expected;
                if (step == 43 || step == 45 || step == 50) { if (actor) ready &= textedit_ui_is_focused(); }
                else if (step == 44 || step == 47 || step == 49 || step == 54) {
                    const char* text = step == 44 ? "Shared native note" : step == 47 ? "First draft" : step == 49 ? "Second draft" : "Recreated draft";
                    const CoopMapNote* note = note_text_find(text); ready &= note != NULL;
                    if (actor) ready &= result && status == COOP_NOTE_OK && !textedit_ui_is_focused();
                    if (ready && step == 44) original_note = note->id;
                    if (ready && step == 54) good &= note->id != original_note;
                } else if (step == 48 || step == 53) {
                    ready &= step == 48 ? note_text_find("First draft") != NULL : !coop_map_note_find(coop_engine_notes(), original_note);
                    if (actor) ready &= result && status == COOP_NOTE_CHANGED && textedit_ui_is_focused();
                    if (ready && actor) { conflicts++; good &= image(phase, framebuffer); }
                } else if (step == 46 || step == 51) { if (actor) ready &= textedit_ui_is_focused() && now - at > 900; }
                else if (step == 52) { ready &= !coop_map_note_find(coop_engine_notes(), original_note); if (actor) ready &= result && status == COOP_NOTE_OK; }
                else if (step == 55) ready &= !wmap_ui_is_created() && !textedit_ui_is_focused();
                else if (step == 56 || step == 57) ready &= coop_runtime_epoch() == baseline_epoch + 1 && control && control->phase == COOP_CONTROL_IDLE
                    && control->notice == COOP_CONTROL_OK && control->action == (step == 56 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD)
                    && note_text_find("Recreated draft");
                if (ready) {
                    char text[64]; snprintf(text, sizeof(text), "service-done:%u", phase);
                    good &= coop_session_chat(session, text); done = true; completed = phase;
                }
            }
            if (recovery && phase && step >= 58 && !done && now - at > 600) {
                const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                bool ready = control != NULL;
                bool saving = step == 59 || step == 63 || step == 67;
                bool loading = step == 60 || step == 64 || step == 68;
                if (saving || loading) {
                    if (member == 3 && !sent) sent = coop_runtime_control(saving ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0);
                    if (loading && member != 3 && !voted && control->phase == COOP_CONTROL_VOTING)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && control->phase == COOP_CONTROL_IDLE && control->notice == COOP_CONTROL_OK
                        && control->action == (saving ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD) && note_text_find("Recreated draft");
                    if (ready && host && (step == 59 || step == 60)) ready &= placed_trap();
                    if (ready && host && (step == 63 || step == 64)) ready &= timeevent_any(TIMEEVENT_TYPE_COOP_TRAPS, bomb_event);
                    if (ready && step >= 67) ready &= !device_target(0) && !device_target(1) && device_credit == 3;
                } else if (step == 58 || step == 62) {
                    unsigned kind = step == 62;
                    if (host && control->paused && !voted) voted = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    if (!control->paused && member == (kind ? 0 : 3) && !sent && device_item(kind) && device_location) {
                        TargetDescriptor target = { .is_loc = true, .loc = device_location };
                        sent = coop_runtime_use_item(device_item(kind), &target, false);
                    }
                    ready &= !control->paused && !device_item(kind) && device_target(kind);
                    if (host && ready) ready &= kind ? timeevent_any(TIMEEVENT_TYPE_COOP_TRAPS, bomb_event) : placed_trap();
                } else if (step == 61 || step == 65) {
                    unsigned kind = step == 65;
                    int64_t target = device_target(kind);
                    if (host && control->paused && !voted) voted = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    if (host && !control->paused && !sent && target) {
                        object_flags_unset(target, OF_OFF);
                        sent = kind ? coop_runtime_control(COOP_CONTROL_REST, 1, 0, 0) : anim_goal_run_to_tile(target, device_location);
                    }
                    if (kind && !host && control->phase == COOP_CONTROL_VOTING && !quantity_input)
                        quantity_input = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    if (host && (device_credit & (1u << kind)) && !(device_reported & (1u << kind))) {
                        char text[100]; snprintf(text, sizeof(text), "service-credit:%u:%d:%d", device_credit, device_damage[0], device_damage[1]);
                        good &= coop_session_chat(session, text); device_reported |= 1u << kind;
                        if (target) object_flags_set(target, OF_OFF);
                    }
                    ready &= target && object_hp_damage_get(target) > 0 && (device_credit & (1u << kind)) && device_damage[kind] > 0;
                    if (kind) ready &= control->phase == COOP_CONTROL_IDLE;
                    if (ready) good &= image(phase, framebuffer);
                } else if (step == 66) ready &= !device_target(0) && !device_target(1);
                if (ready) {
                    char text[64]; snprintf(text, sizeof(text), "service-done:%u", phase);
                    good &= coop_session_chat(session, text); done = true; completed = phase;
                }
            }
            if (stack_mode && phase && step>=18 && !done && now-at>500) {
                bool ready=false;
                int64_t npc=tagged(true,0),chest=0,obj; int iterator;
                if (obj_inst_first(&obj,&iterator)) do {
                    if (obj_field_int32_get(obj,OBJ_F_TYPE)==OBJ_TYPE_CONTAINER && obj_field_int32_get(obj,OBJ_F_CONTAINER_PAD_I_1)==187630) { chest=obj; break; }
                } while (obj_inst_next(&obj,&iterator));
                if (!chest || !npc) { good=false; break; }
                CoopItems* items=coop_gameplay_items(game);
                const CoopControlView* control=coop_controls_current(coop_runtime_controls());
                if (!sent) {
                    gold=item_gold_get(pc); npc_gold=item_gold_get(npc); ammo_before=item_ammo_quantity_get(pc,1);
                    other_before=step==23 ? item_ammo_quantity_get(chest,1) : item_gold_get(chest);
                    if (own) {
                        if (step==18 || step==19) {
                            int64_t ammo=sub_462540(step==18 ? pc : npc,sub_4685A0(BP_BULLET),0);
                            good &= ammo && item_click(ammo,true);
                        } else if (step==20 || step==26) inven_ui_destroy();
                        else if (step==21) good &= coop_runtime_item(COOP_LOOT_OPEN,chest,0,-1);
                        else if (step>=22 && step<=25) {
                            int64_t selected=step==23 ? sub_462540(chest,sub_4685A0(BP_BULLET),0) : sub_462540(step==24 ? pc : chest,sub_4685A0(BP_GOLD),0);
                            if (!selected) { good=false; break; }
                            int slot=item_inventory_location_get(selected); bool mine=step==24;
                            TigMessage mouse={.type=TIG_MESSAGE_MOUSE};
                            mouse.data.mouse.x=(mine ? 368 : 8)+(slot%10)*32+8; mouse.data.mouse.y=(mine ? 49 : 177)+(slot/10)*32+8;
                            mouse.data.mouse.event=TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP; tig_message_enqueue(&mouse);
                        } else if (step<=30) {
                            TigMessage mouse={.type=TIG_MESSAGE_MOUSE}; mouse.data.mouse.x=678; mouse.data.mouse.y=358;
                            mouse.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&mouse);
                            mouse.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&mouse);
                        }
                        else if (step==31 || step==33) {
                            if (step==31) {
                                CoopSaveSet party; char path[COOP_SAVE_PATH];
                                good &= coop_runtime_save_set(&party) && coop_save_path(&party,0,path);
                                if (good && rename(path,"probe-hidden-quick.acsave")) good=false;
                            }
                            TigMessage mouse={.type=TIG_MESSAGE_MOUSE}; mouse.data.mouse.x=714; mouse.data.mouse.y=358;
                            mouse.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&mouse);
                            mouse.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&mouse);
                        } else if (step==32) {
                            CoopSaveSet party; CoopSaveEntry newest={0}; bool found=false;
                            good &= coop_runtime_save_set(&party);
                            for (unsigned i=0;i<COOP_SAVE_QUICK_HISTORY;i++) {
                                CoopSaveEntry entry; coop_save_inspect(&party,COOP_SAVE_QUICK_HISTORY_FIRST+i,&entry);
                                if (entry.status==COOP_CHECKPOINT_OK && (!found || entry.info.epoch>newest.info.epoch)) { newest=entry; found=true; }
                            }
                            good &= found && coop_runtime_checkpoint_request(COOP_CONTROL_LOAD,&newest,"");
                        }
                    }
                    sent=true;
                }
                if (own && !quantity_input && (step==18 || step==19 || (step>=22 && step<=25)) && intgame_mode_get()==INTGAME_MODE_QUANTITY) {
                    number(step==18 ? 7 : step==19 || step==23 ? 11 : step==22 ? 37 : step==24 ? 13 : 1000000);
                    TigMessage mouse={.type=TIG_MESSAGE_MOUSE}; mouse.data.mouse.x=740; mouse.data.mouse.y=560;
                    mouse.data.mouse.event=TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&mouse);
                    mouse.data.mouse.event=TIG_MESSAGE_MOUSE_IDLE; tig_message_enqueue(&mouse);
                    quantity_input=true;
                }
                if (step==18 || step==19) {
                    ready=!own || (quantity_input && !coop_services_busy(service) && intgame_mode_get()!=INTGAME_MODE_QUANTITY
                        && item_ammo_quantity_get(pc,1)==ammo_before+(step==18 ? -7 : 11)
                        && item_gold_get(pc)+item_gold_get(npc)==gold+npc_gold
                        && (step==18 ? item_gold_get(pc)>gold : item_gold_get(pc)<gold));
                } else if (step==20 || step==26) ready=!view->active && (!own || !inven_ui_is_created());
                else if (step==21) ready=!own || (inven_ui_is_created() && coop_items_loot_target(items)==chest && !coop_items_busy(items));
                else if (step>=22 && step<=25) {
                    ready=!own || (quantity_input && !coop_items_busy(items) && intgame_mode_get()!=INTGAME_MODE_QUANTITY
                        && (step==23 ? item_ammo_quantity_get(pc,1)==ammo_before+11 && item_ammo_quantity_get(chest,1)==other_before-11
                            : item_gold_get(pc)==gold+(step==24 ? -13 : step==22 ? 37 : other_before)
                                && item_gold_get(chest)==(step==24 ? other_before+13 : step==22 ? other_before-37 : 0)));
                } else {
                    if (step>=31 && control->phase==COOP_CONTROL_VOTING && !voted)
                        voted=coop_runtime_control(COOP_CONTROL_VOTE,step==33 && member==2 ? 0 : 1,control->proposal,0);
                    ready=control->phase==COOP_CONTROL_IDLE && control->notice==(step==33 ? COOP_CONTROL_DECLINED : COOP_CONTROL_OK)
                        && coop_runtime_epoch()==baseline_epoch+(step==33 ? 0 : 1)
                        && script_global_var_get(0)==(step<=30 ? 60000+(int)step : step==31 ? 60030 : step==32 ? 60029 : 99933);
                    if (ready && step==30) {
                        CoopSaveSet party; CoopSaveEntry entries[2]; good &= coop_runtime_save_set(&party);
                        for (unsigned i=0;i<2;i++) coop_save_inspect(&party,COOP_SAVE_QUICK_HISTORY_FIRST+i,&entries[i]);
                        ready &= entries[0].status==COOP_CHECKPOINT_OK && entries[1].status==COOP_CHECKPOINT_OK
                            && entries[0].info.epoch!=entries[1].info.epoch
                            && entries[0].info.epoch+3>=coop_runtime_epoch() && entries[1].info.epoch+3>=coop_runtime_epoch();
                    }
                    if (ready) ready &= item_ammo_quantity_get(pc,1)==115 && item_ammo_quantity_get(npc,1)==46 && item_ammo_quantity_get(chest,1)==26
                        && !item_gold_get(chest) && item_gold_get(pc)+item_gold_get(npc)==2000100;
                }
                if (ready) {
                    if (step==26 && host) {
                        stack_services=coop_services_executed(service); stack_items=coop_items_executed(items);
                        good &= stack_services==4 && stack_items==5;
                    }
                    if (step==30 && own) {
                        if (!panel_seen) {
                            good &= coop_saves_ui_open(COOP_SAVES_LOAD); panel_seen=now;
                            TigMessage mouse={.type=TIG_MESSAGE_MOUSE}; mouse.data.mouse.x=(hrp_iso_window_width_get()-760)/2+680;
                            mouse.data.mouse.y=hrp_iso_window_height_get()-518; mouse.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&mouse);
                            ready=false;
                        } else {
                            const CoopSavePage* page=coop_save_catalog_current(coop_runtime_save_catalog());
                            ready=!coop_save_catalog_pending(coop_runtime_save_catalog()) && page && page->page==COOP_SAVE_QUICK_HISTORY_FIRST/COOP_SAVE_PAGE_ROWS && page->count==2;
                            if (ready) { good &= image(phase,framebuffer); coop_saves_ui_close(); }
                        }
                    }
                    if (ready && step==25 && !image(phase,framebuffer)) good=false;
                    if (ready) {
                    fprintf(stderr,"ARCMP stack/quick step=%u pc_gold=%d npc_gold=%d bullets=%d marker=%d epoch=%u\n",step,item_gold_get(pc),item_gold_get(npc),item_ammo_quantity_get(pc,1),script_global_var_get(0),coop_runtime_epoch());
                    char text[64]; snprintf(text,sizeof(text),"service-done:%u",phase);
                    good &= coop_session_chat(session,text); done=true; completed=phase;
                    }
                }
            }
            if (phase && !done && now - at > (recovery ? step == 65 ? 125000u : 30000u : 12000u)) {
                image(phase, framebuffer);
                fprintf(stderr, "ARCMP service probe timeout phase=%u step=%u sent=%d done=%d view=%u/%u tokenbusy=%d mode=%u quantity=%d\n", phase, step, sent, done, view->revision, view->mode, coop_services_busy(service), intgame_mode_get(), quantity_input);
                good = false; break;
            }
            if (host && phase && done && !finishing && coop_services_settled(service) && coop_dialogue_settled(dialogue) && coop_notes_settled(coop_gameplay_notes(game))) {
                bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == phase;
                if (all) {
                    if (!quiet) quiet = now;
                    if (now - quiet > 400) {
                        if (phase < phase_count()) good = good && announce(session, phase + 1);
                        else { good = good && coop_gameplay_finish(game); finishing = true; }
                    }
                }
            }
            if (coop_gameplay_finished(game)) { if (!ended) ended = now; if (now - ended > 600) break; }
        }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage msg; while (tig_message_dequeue(&msg) == TIG_OK) { if (msg.type == TIG_MESSAGE_QUIT) good = false; else if (msg.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else {
            intgame_process_event(&msg);
            /* Match the ordinary main.c Escape dispatcher after UI filters. */
            if (recovery && msg.type == TIG_MESSAGE_KEYBOARD && !msg.data.keyboard.pressed && msg.data.keyboard.scancode == SDL_SCANCODE_ESCAPE) {
                IntgameMode mode = intgame_mode_get();
                if (mode != INTGAME_MODE_MAIN && mode != INTGAME_MODE_DIALOG && mode != INTGAME_MODE_BARTER) intgame_mode_set(INTGAME_MODE_MAIN);
            }
        } }
        if (recovery) SDL_SetModState(SDL_KMOD_NONE);
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay(); CoopServices* service = game ? coop_gameplay_services(game) : NULL;
    bool acts = member >= 0 && (unsigned)member >= first_owner && (unsigned)member < first_owner + probe_owners;
    unsigned expected_actions = first_phase > 1 ? 0 : first_step == 1 ? 9 : first_step == 13 ? 7 : 3;
    unsigned expected_denials = first_phase > 1 ? 0 : first_step == 1 ? 8 : first_step == 13 ? 4 : 0;
    if (stack_mode) good=good && game && completed==phase_count() && coop_gameplay_finished(game)
        && guarded==!host && stack_services==(host ? 4u : 0u) && stack_items==(host ? 5u : 0u);
    else if (callbacks_only) good = good && game && coop_gameplay_finished(game) && callback_checks == (host ? 34u : 0u);
    else {
    good = good && game && completed == phase_count() && replays == (acts ? expected_actions : 0) && denials == (acts ? expected_denials : 0)
        && guarded == !host && coop_gameplay_finished(game)
        && (recovery ? recoveries == (first_phase == 1 ? 7u : first_phase == 40 ? 1u : 0u) && drops == (acts ? recoveries : 0u) && callback_checks == (host && first_phase == 1 ? 7u : 0u) && conflicts == (member == 3 && first_phase <= 43 ? 2u : 0u) && device_credit == 3 && device_damage[0] > 0 && device_damage[1] > 0
            : coop_services_executed(service) == (host ? expected_actions * probe_owners : 0u)
                && coop_items_executed(coop_gameplay_items(game)) == (host && first_step < 30 ? 2 * probe_owners : 0u));
    }
    FILE* report = fopen("arcmp-services-result.json", "wb");
    if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"first_phase\":%u,\"replays\":%u,\"denials\":%u,\"replica_guard\":%s,\"executed\":%u,\"rejected\":%u,\"transfers\":%u,\"member\":%d,\"recoveries\":%u,\"drops\":%u,\"callback_guards\":%u,\"conflicts\":%u,\"device_credit\":%u,\"device_damage\":[%d,%d],\"stack_trades\":%u,\"stack_moves\":%u}\n",
            good ? "true" : "false", completed, first_phase, replays, denials, guarded ? "true" : "false", service ? coop_services_executed(service) : 0, service ? coop_services_rejected(service) : 0,
            game ? coop_items_executed(coop_gameplay_items(game)) : 0, member, recoveries, drops, callback_checks, conflicts, device_credit, device_damage[0], device_damage[1], stack_services, stack_items);
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP service probe %s phases=%u replays=%u denials=%u\n", good ? "PASS" : "FAIL", completed, replays, denials);
    return good;
}
