#include "coop/runtime.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_documents.h"
#include "coop/engine_object.h"
#include "coop/engine_probe.h"
#include "coop/lobby_ui.h"
#include "coop/bootstrap.h"
#include "game/anim.h"
#include "game/ai.h"
#include "game/area.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/description.h"
#include "game/fate.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/jumppoint.h"
#include "game/location.h"
#include "game/map.h"
#include "game/magictech.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/portal.h"
#include "game/quest.h"
#include "game/rumor.h"
#include "game/sector.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/tile.h"
#include "game/townmap.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/mainmenu_ui.h"
#include "ui/slide_ui.h"
#include "ui/wmap_ui.h"
#include "ui/written_ui.h"
#include "tig/message.h"
#include "tig/memory.h"
#include "tig/movie.h"
#include "tig/file.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Original starting-party continuation. Actions use the normal ownership path.
 * Explicit ARCMP_PROBE_STRENGTHEN_PARTY diagnostics alter combat stats only;
 * their reports must never be presented as ordinary-party acceptance. */
enum { LAST = 283, RECEIPT_BYTES = 128 };
static CoopSession* held;
static int initial_gold[4], paid[4];
static CoopObjectId bought[4];
static int64_t destination;
static unsigned kit_stage = 4;
static uint32_t kit_epoch;
static void kit_next(void)
{
    char message[64]; snprintf(message, sizeof(message), "ordinary-kit-request:%u", kit_stage + 1);
    coop_session_chat(held, message);
}
static void damage_observed(const CombatContext* combat)
{
    int victim = coop_campaign_slot_for_object(combat->target_obj);
    if (victim >= 0) fprintf(stderr, "ARCMP ordinary damage member=%d amount=%d hp=%d source=%lld name=%d\n", victim, combat->total_dam, object_hp_current(combat->target_obj), (long long)combat->attacker_obj, combat->attacker_obj ? obj_field_int32_get(combat->attacker_obj, OBJ_F_NAME) : -1);
    static unsigned critters;
    if (combat->target_obj && obj_field_int32_get(combat->target_obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && critters++ < 384)
        fprintf(stderr, "ARCMP ordinary NPC damage target=%lld name=%d amount=%d hp=%d/%d source-member=%d flags=%x\n", (long long)combat->target_obj,
            obj_field_int32_get(combat->target_obj, OBJ_F_NAME), combat->total_dam, object_hp_current(combat->target_obj), object_hp_max(combat->target_obj),
            coop_campaign_slot_for_object(combat->attacker_obj), combat->flags);
    static unsigned doors;
    if (combat->target_obj && obj_field_int32_get(combat->target_obj, OBJ_F_TYPE) == OBJ_TYPE_PORTAL && doors++ < 16)
        fprintf(stderr, "ARCMP ordinary original door impact damage=%d normal=%d hp=%d material=%d\n", combat->total_dam, combat->dam[DAMAGE_TYPE_NORMAL], object_hp_current(combat->target_obj), obj_field_int32_get(combat->target_obj, OBJ_F_MATERIAL));
}
static void resources(int64_t actor, int member)
{
    fprintf(stderr, "ARCMP ordinary resources member=%d hp=%d/%d fatigue=%d/%d aptitude=%d fate=%d strength=%d dexterity=%d gold=%d melee=%d difficulty=%d\n", member,
        object_hp_current(actor), object_hp_max(actor), critter_fatigue_current(actor), critter_fatigue_max(actor),
        stat_level_get(actor, STAT_MAGICK_TECH_APTITUDE), stat_base_get(actor, STAT_FATE_POINTS), stat_level_get(actor, STAT_STRENGTH),
        stat_level_get(actor, STAT_DEXTERITY), item_gold_get(actor), basic_skill_points_get(actor, BASIC_SKILL_MELEE), gamelib_game_difficulty_get());
    int count = obj_field_int32_get(actor, OBJ_F_CRITTER_INVENTORY_NUM);
    for (int i = 0; i < count && i < 64; i++) {
        int64_t item = obj_arrayfield_handle_get(actor, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        if (item) fprintf(stderr, "ARCMP ordinary owned item member=%d name=%d type=%d slot=%d\n", member,
            obj_field_int32_get(item, OBJ_F_NAME), obj_field_int32_get(item, OBJ_F_TYPE), obj_field_int32_get(item, OBJ_F_ITEM_INV_LOCATION));
    }
    int64_t weapon = item_wield_get(actor, ITEM_INV_LOC_WEAPON);
    if (weapon) {
        int low, high; item_weapon_damage(weapon, actor, DAMAGE_TYPE_NORMAL, SKILL_MELEE, false, &low, &high);
        fprintf(stderr, "ARCMP ordinary weapon member=%d description=%d damage=%d-%d range=%d\n", member,
            obj_field_int32_get(weapon, OBJ_F_DESCRIPTION), low, high, item_weapon_range(weapon, actor));
    }
}
void coop_ordinary_probe_cleanup(void)
{
    coop_runtime_present_observer(NULL,NULL);
    combat_damage_observer_set(NULL);
    if (held) { coop_runtime_detach(); coop_session_destroy(held); held = NULL; }
}
static int64_t npc(int script)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) continue;
        Script s = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &s);
        if (s.num == script) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return 0;
}
static int64_t magnus_actor(void)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NAME) == 6411
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return npc(1046);
}
/* Read the actual original item/parent; prototype description numbers are
 * distinct from the retail instance's script-visible NAME. */
static int64_t named_item(int name)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_type_is_item(obj_field_int32_get(obj, OBJ_F_TYPE))
            && obj_field_int32_get(obj, OBJ_F_NAME) == name
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return 0;
}
static int bridge_key_id;
static int bridge_key_code(void)
{
    int64_t key = named_item(4402);
    if (key) bridge_key_id = obj_field_int32_get(key, OBJ_F_KEY_KEY_ID);
    if (!bridge_key_id && item_get_keys(coop_campaign_object(0), NULL) == 1)
        item_get_keys(coop_campaign_object(0), &bridge_key_id);
    return bridge_key_id;
}
static bool bridge_key_owned(void)
{
    int code = bridge_key_code();
    if (!code) return false;
    int64_t actor = coop_campaign_object(0), key = item_find_by_name(actor, 4402);
    if (key && obj_field_int32_get(key, OBJ_F_KEY_KEY_ID) == code) return true;
    int count = item_get_keys(actor, NULL);
    if (count <= 0 || count > 4096) return false;
    int* ids = malloc((size_t)count * sizeof(*ids)); if (!ids) return false;
    item_get_keys(actor, ids); bool found = false;
    for (int i = 0; i < count; i++) found |= ids[i] == code;
    free(ids); return found;
}
static int64_t bridge_gate(void)
{
    int key_id = bridge_key_code();
    int64_t lukan = npc(1041), obj, best = 0; int iterator, distance = 100;
    if (!key_id || !lukan) return 0;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_PORTAL
            || obj_field_int32_get(obj, OBJ_F_PORTAL_KEY_ID) != key_id) continue;
        int d = object_dist(lukan, obj); if (d < distance) { distance = d; best = obj; }
    } while (obj_inst_next(&obj, &iterator));
    return best;
}
static bool gather_phase(unsigned phase) { return phase == 1 || phase == 6 || phase == 28 || phase == 36 || phase == 51 || phase == 54 || phase == 56 || phase == 58 || phase == 62 || phase == 65; }
static bool exit_phase(unsigned phase) { return phase == 4 || phase == 34 || phase == 40 || phase == 45 || phase == 70 || phase == 84 || phase == 95 || phase == 107 || phase == 112 || phase == 123 || phase == 143 || phase == 159 || phase == 166 || phase == 203 || phase == 207 || phase == 211 || phase == 214 || phase == 244 || phase == 263 || phase == 268; }
static unsigned journey_area(unsigned phase) {
    switch (phase) { case 167: case 188: return 18; case 169: return 16; case 176: return 6; case 186: return 17; case 190: return 4; case 204: return 24; case 208: case 212: return 50; case 215: case 264: return 20; case 220: return 11; case 245: return 22; case 266: return 8; case 269: return 13; }
    return phase == 144 || phase == 145 || phase == 146 || phase == 147 ? 18 : phase == 149 || phase == 150 || phase == 151 || phase == 152 ? 4 : phase == 160 || phase == 161 ? 13 : phase == 134 ? 17 : phase == 136 ? 21 : phase == 121 ? 12 : phase == 113 ? 17 : phase == 115 || phase == 124 ? 6 : phase == 35 ? 2 : phase == 46 || phase == 78 || phase == 104 ? 21 : phase == 108 ? 10 : phase == 96 ? 9 : phase == 85 ? 8 : phase == 71 ? 7 : phase == 5 || phase == 41 ? 1 : 0; }
static bool route_save(unsigned phase) { return phase == 32 || phase == 39 || phase == 43 || phase == 47 || phase == 50 || phase == 53 || phase == 61 || phase == 67 || phase == 72 || phase == 75 || phase == 81 || phase == 87 || phase == 93 || phase == 101 || phase == 110 || phase == 118 || phase == 131 || phase == 164 || phase == 174 || phase == 183 || phase == 200 || phase == 216 || phase == 242 || phase == 261 || phase == 271 || phase == 280; }
static bool route_load(unsigned phase) { return phase == 44 || phase == 48 || phase == 68 || phase == 73 || phase == 76 || phase == 82 || phase == 88 || phase == 94 || phase == 102 || phase == 111 || phase == 119 || phase == 132 || phase == 165 || phase == 175 || phase == 184 || phase == 201 || phase == 217 || phase == 243 || phase == 262 || phase == 272 || phase == 281; }
static unsigned city_dialogue(unsigned phase) {
    switch (phase) { case 172: return 1087; case 178: case 182: return 1074; case 180: return 1073; case 192: return script_global_flag_get(2087) ? 1090 : 1089; case 197: return 1090; case 198: return 1091; case 202: return 1749; case 206: return 2510; case 209: case 210: return 1108; case 213: return 2511; }
    switch (phase) { case 218: return 1077; case 221: return 2348; case 226: return 1835; case 228: return 1890; case 241: return 1921; case 267: return 1749; case 270: return 1171; }
    if (phase == 274) return 1116;
    if (phase == 283) return 1105;
    return phase == 153 || phase == 158 ? 1098 : phase == 155 ? 1083 : phase == 138 ? 1498 : phase == 141 ? 1078 : phase == 122 ? 1080 : phase == 128 ? 1073 : phase == 117 || phase == 126 || phase == 130 ? 1074 : phase == 105 ? 1079 : phase == 109 ? 1076 : phase == 98 ? 1067 : phase == 100 ? 1071 : phase == 52 || phase == 63 ? 1060 : phase == 55 ? 2075 : phase == 57 ? 2074 : phase == 66 || phase == 80 ? 1047 : phase == 86 || phase == 92 ? 1061 : phase == 90 ? 1069 : phase == 91 ? 1066 : 0; }
static int64_t journey_destination(unsigned phase)
{
    switch (phase) {
    case 144: return location_make(89888, 84192);
    case 145: return location_make(92704, 89312);
    case 146: return location_make(93664, 90336);
    case 147: return location_make(95648, 92256);
    case 149: return location_make(98976, 96416);
    case 150: return location_make(99168, 96928);
    case 151: return location_make(99296, 97440);
    case 160: return location_make(107936, 100960);
    case 167: return location_make(96160, 92256);
    case 186: return location_make(82784, 45600);
    case 188: return location_make(95648, 92256);
    case 208: return location_make(46112, 120624);
    case 212: return location_make(40224, 102048);
    case 269: return location_make(111981, 96786);
    }
    return phase == 134 ? location_make(82784, 45600)
        : area_get_location(journey_area(phase));
}
static int64_t nearest_speaker(int script, int64_t pc)
{
    int64_t obj, best = 0; int iterator, distance = 512;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC
            || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
            || (obj_field_int32_get(obj, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)) continue;
        Script s = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &s);
        if (script == 1096 && !critter_is_active(obj)) continue;
        if (s.num == script && object_dist(pc, obj) < distance) { distance = object_dist(pc, obj); best = obj; }
    } while (obj_inst_next(&obj, &iterator));
    return best;
}
static int64_t investigation_paper(void)
{
    int64_t leader = coop_campaign_object(0);
    int count = obj_field_int32_get(leader, OBJ_F_CRITTER_INVENTORY_NUM);
    for (int i = 0; i < count; i++) {
        int64_t item = obj_arrayfield_handle_get(leader, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        if (item && obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_WRITTEN
            && obj_field_int32_get(item, OBJ_F_WRITTEN_SUBTYPE) == 2) return item;
    }
    return 0;
}
static int64_t saboteur(void)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || !critter_is_active(obj)
            || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DONTDRAW))) continue;
        Script s = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DYING, &s);
        if (s.num == 1253) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return 0;
}
static bool contains(const char* text, const char* part)
{
    size_t n = strlen(part);
    for (; *text; text++) if (!SDL_strncasecmp(text, part, n)) return true;
    return false;
}
static void key(unsigned choice)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD };
    m.data.keyboard.scancode = SDL_SCANCODE_1 + (int)choice; m.data.keyboard.key = SDLK_1 + (int)choice;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static int dialogue_choice(const CoopDialogueView* view, unsigned goal)
{
    static const char* recruit[] = { "let's go talk", "can not hurt to search", "can't hurt to talk", "play along", "join me again", "okay, let's go", "let's go.", "confused", "slow down", "really need to explain", "getting the gist", "yes?", "dying gnome", "ludicrous", "this whole", "agreed" };
    static const char* trade[] = { "barter", "services", "trade", "buy", "sell", "wares", "inventory", "goods", "business", "stock", "business matters", "few questions", "questions", "and you are" };
    static const char* story[] = { "imprint", "schuyler", "ring", "helpful", "looking for exactly", "look for one", "what exactly", "what things", "fine.  what", "what do you want", "few questions", "and you are" };
    static const char* toll[] = { "here you are", "here's your money", "business is business", "free to pass", "pleasure doing business", "what's the charge", "how much", "cross this bridge", "cross the bridge", "across the bridge", "who are you", "spare a moment" };
    static const char* camera[] = { "camera", "let's trade", "imprint", "schuyler", "ring", "what do you want", "greetings", "thank you" };
    static const char* factory[] = { "take care of them", "piqued my interest", "attempt on his life", "some way i could see", "gain an audience", "whose house", "help stop", "help with the saboteur", "help with the saboteurs", "should know", "why do you say", "will do", "removed", "killed the saboteurs" };
    static const char* bates[] = { "ask you about this ring", "ask you about a ring", "zephyr", "dying gnome", "scar over", "he had a scar", "what did you do", "escape", "warn us", "betray", "learn more", "interested in solving", "accepted", "return with the information", "give him the ring", "and that would be", "what happened", "black mountain", "clan", "stennar", "dwarf", "yes" };
    const char** words = goal == 0 ? recruit : goal == 1 ? trade : goal == 3 ? toll : goal == 4 ? camera : goal == 5 ? factory : goal == 6 ? bates : story;
    unsigned count = goal == 0 ? SDL_arraysize(recruit) : goal == 1 ? SDL_arraysize(trade) : goal == 3 ? SDL_arraysize(toll) : goal == 4 ? SDL_arraysize(camera) : goal == 5 ? SDL_arraysize(factory) : goal == 6 ? SDL_arraysize(bates) : SDL_arraysize(story);
    for (unsigned w = 0; w < count; w++) for (unsigned i = 0; i < view->option_count; i++)
        if (contains(view->options[i], words[w])) return (int)i;
    for (unsigned i = 0; i < view->option_count; i++) {
        const char* s = view->options[i];
        if (!contains(s, "leave") && !contains(s, "goodbye") && !contains(s, "attack") && !contains(s, "kill") && !contains(s, "not going")) return (int)i;
    }
    return -1;
}
static int64_t enemy(int64_t pc)
{
    int64_t obj, best = 0; int iterator, score = 0x7fffffff;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || !critter_is_active(obj) || critter_party_same(pc, obj)
            || (!combat_critter_is_combat_mode_active(obj) && !ai_is_fighting(obj))) continue;
        int64_t focus = obj_field_handle_get(obj, OBJ_F_NPC_COMBAT_FOCUS);
        if (!focus || (coop_campaign_slot_for_object(focus) < 0 && coop_campaign_slot_for_object(critter_pc_leader_get(focus)) < 0)) continue;
        int d = object_dist(pc, obj);
        /* Finish a wounded nearby threat instead of changing targets whenever
         * the front human moves one tile. Prefer reachable local threats. */
        int value = object_hp_current(obj) * 100 + d;
        if (d < 40 && value < score) { score = value; best = obj; }
    } while (obj_inst_next(&obj, &iterator));
    return best;
}
static bool harm_available(int64_t pc, int64_t target)
{
    if (!target || !spell_is_known(pc, SPELL_HARM) || critter_fatigue_current(pc) <= 7
        || object_dist(pc, target) > magictech_get_range(SPELL_HARM) || !combat_check_cast_spell(pc)) return false;
    MagicTechInvocation invocation;
    magictech_invocation_init(&invocation, pc, SPELL_HARM);
    sub_4440E0(target, &invocation.target_obj);
    return magictech_check_los(&invocation);
}
static bool enter_door(int64_t pc, int64_t door, int64_t target, unsigned member)
{
    bool jammed = (obj_field_int32_get(door, OBJ_F_PORTAL_FLAGS) & OPF_JAMMED) != 0;
    if (object_locked_get(door) && !jammed && spell_is_known(pc,SPELL_UNLOCKING_CANTRIP)
        && !(obj_field_int32_get(door,OBJ_F_PORTAL_FLAGS)&OPF_MAGICALLY_HELD)
        && stat_level_get(pc,STAT_MAGICK_TECH_APTITUDE)>=obj_field_int32_get(door,OBJ_F_PORTAL_LOCK_DIFFICULTY)
        && object_dist(pc,door)<=magictech_get_range(SPELL_UNLOCKING_CANTRIP)
        && !coop_magic_busy(coop_gameplay_magic(coop_runtime_gameplay()))) {
        TargetDescriptor descriptor; target_descriptor_set_obj(&descriptor,door);
        return coop_runtime_cast(pc,SPELL_UNLOCKING_CANTRIP,&descriptor,false);
    }
    if (!member && quest_state_get(pc, 1063) == QUEST_STATE_ACCEPTED
        && (object_locked_get(door) || jammed)) {
        int64_t art = named_item(5627), parent = 0; if (art) item_parent(art, &parent);
        if (parent && target == obj_field_int64_get(parent, OBJ_F_LOCATION)
            && (jammed || !sub_463370(pc, obj_field_int32_get(door, OBJ_F_PORTAL_KEY_ID)))) {
            bool stance = combat_critter_is_combat_mode_active(pc);
            return !coop_battle_busy(coop_gameplay_battle(coop_runtime_gameplay()))
                && coop_runtime_combat(stance ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE,
                    stance ? COOP_ATTACK_FORCE | (COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT) : 1, stance ? door : 0);
        }
    }
    return coop_runtime_world_use(door);
}
static bool ordinary_door(int64_t obj)
{
    if (obj_field_int32_get(obj,OBJ_F_TYPE)!=OBJ_TYPE_PORTAL) return false;
    Script use={0}; obj_arrayfield_script_get(obj,OBJ_F_SCRIPTS_IDX,SAP_USE,&use);
    /* Scripted exits and quest passages are selected explicitly. A nearby
     * cave exit is not an ordinary door on the way to a same-map speaker. */
    return !use.num;
}
static bool walk(int64_t pc, int64_t target, unsigned member, int range)
{
    int64_t from = obj_field_int64_get(pc, OBJ_F_LOCATION);
    if (location_dist(from, target) <= range) return true;
    for (unsigned i = 0; i < (range ? 8u : 1u); i++) {
        int64_t loc = target; if ((range && !location_in_range(target, (int)((i + member * 2) % 8), range == 1 ? 1 : 2, &loc)) || tile_is_blocking(loc, false)) continue;
        int8_t steps[512]; PathCreateInfo path = { .obj = pc, .from = from, .to = loc, .max_rotations = 512, .rotations = steps };
        int count = path_make(&path);
        if (count > 0) {
            int64_t at = from;
            for (int step = 0; step < count && step < 8; step++) {
                int64_t door = 0;
                if (object_traversal_check_blocking_door(pc, at, steps[step], 0, &door) && door && ordinary_door(door) && !portal_is_open(door) && !object_is_busted(door))
                    return enter_door(pc, door, target, member);
                if (!location_in_dir(at, steps[step], &at)) return false;
            }
            /* The native goal replans its own bounded path segments. Submit
             * its real destination instead of clicking every eight steps. */
            if (coop_runtime_move(loc, true)) return true;
        }
    }
    /* Original unlocked doors are used through the same native world adapter. */
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (ordinary_door(obj) && !portal_is_open(obj) && !object_is_busted(obj) && object_dist(pc, obj) < 6
            && (!object_locked_get(obj) || sub_463370(pc, obj_field_int32_get(obj, OBJ_F_PORTAL_KEY_ID))))
            return enter_door(pc, obj, target, member);
    } while (obj_inst_next(&obj, &iterator));
    /* A nearby chest can be across a warehouse wall. Approach an actual
     * reachable entrance, rather than repeatedly trying loot through the wall.
     * Only the accepted stolen-painting route chooses a native forced attack. */
    if (range && !member) {
        static int64_t previous_target, previous_from, refused[8], logged;
        static unsigned refused_count;
        if (previous_target != target || location_dist(previous_from,from)>3) {
            previous_target = target; previous_from=from; refused_count = 0; logged = 0;
        }
        int64_t entry = 0; int best = 0x7fffffff;
        if (obj_inst_first(&obj, &iterator)) do {
            if (!ordinary_door(obj) || portal_is_open(obj) || object_is_busted(obj)
                || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            int64_t loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
            int distance = (int)location_dist(loc, target); if (distance > 48) continue;
            bool skip = false; for (unsigned i = 0; i < refused_count; i++) skip |= refused[i] == loc;
            int score = distance * 4 + (int)object_dist(pc, obj);
            if (!skip && score < best) { entry = obj; best = score; }
        } while (obj_inst_next(&obj, &iterator));
        if (entry) {
            int64_t loc = obj_field_int64_get(entry, OBJ_F_LOCATION);
            if (logged != loc) { logged = loc; fprintf(stderr, "ARCMP ordinary native entrance x=%lld y=%lld locked=%d key=%d\n",
                (long long)location_get_x(loc), (long long)location_get_y(loc), object_locked_get(entry), obj_field_int32_get(entry, OBJ_F_PORTAL_KEY_ID)); }
            if (object_dist(pc, entry) <= 6) {
                return enter_door(pc, entry, target, member);
            }
            if (object_dist(pc,entry)<=30) return coop_runtime_world_use(entry);
            for (unsigned i = 0; i < 8; i++) {
                int64_t approach;
                if (!location_in_dir(loc, (int)i, &approach) || tile_is_blocking(approach, false)) continue;
                int8_t steps[512]; PathCreateInfo path = { .obj = pc, .from = from, .to = approach, .max_rotations = 512,
                    .rotations = steps, .flags = PATH_FLAG_0x0002 };
                if (path_make(&path) > 0) return coop_runtime_move(approach, true);
            }
            if (refused_count < SDL_arraysize(refused)) refused[refused_count++] = loc;
        }
    }
    /* Ordinary same-map building stairs are native jump points, not a path
     * through a disconnected upper floor. Read the unchanged native graph and
     * walk onto a reachable original entrance; never place an actor. */
    if (range && member == 0) {
        char *base, *save, path[TIG_MAX_PATH]; map_paths(&base, &save);
        snprintf(path, sizeof(path), "%s\\map.jmp", save);
        TigFile* f = tig_file_fopen(path, "rb");
        if (!f) { snprintf(path, sizeof(path), "%s\\map.jmp", base); f = tig_file_fopen(path, "rb"); }
        int count = 0;
        if (f && tig_file_fread(&count, sizeof(count), 1, f) == 1 && count > 0 && count <= 4096) {
            for (int i = 0; i < count; i++) {
                JumpPoint jump;
                if (tig_file_fread(&jump, sizeof(jump), 1, f) != 1) break;
                if (jump.dst_map == map_current_map() && location_dist(jump.dst_loc, target) <= 48
                    && location_dist(from, jump.loc) > 1 && walk(pc, jump.loc, member, 0)) {
                    fprintf(stderr, "ARCMP ordinary native building stairs from=%lld to=%lld\n", (long long)jump.loc, (long long)jump.dst_loc);
                    tig_file_fclose(f); return true;
                }
            }
        }
        if (f) tig_file_fclose(f);
    }
    return false;
}
/* Rotate a hurt front human behind a healthier teammate using native movement
 * and AP. A failed route falls through to the ordinary attack policy. */
static bool withdraw(int64_t pc, int64_t foe, unsigned member)
{
    int64_t cover = 0; int health = object_hp_current(pc);
    for (unsigned i = 0; i < 4; i++) {
        int64_t ally = coop_campaign_object(i);
        if (ally != pc && critter_is_active(ally) && object_hp_current(ally) > health
            && object_dist(ally, foe) >= object_dist(pc, foe) + 2
            && object_dist(ally, pc) > 3 && object_dist(ally, pc) <= 12) {
            health = object_hp_current(ally); cover = ally;
        }
    }
    return cover && walk(pc, obj_field_int64_get(cover, OBJ_F_LOCATION), member, 2);
}
/* Scan one perimeter candidate per frame so native sector I/O never holds
 * the host away from transport polling for an entire route search. */
static bool return_route_open(int64_t loc)
{
    int8_t rotations[5000];
    WmapPathInfo route = { .from=sector_id_from_loc(loc), .to=sector_id_from_loc(location_make(43552,115488)),
        .max_rotations=SDL_arraysize(rotations), .rotations=rotations };
    return wmap_path_make(&route)>0;
}
static int escape_destination(unsigned context)
{
    /* Tulla's original inside tile supplies the passage through its walls.
     * The next phase walks that tile before continuing to this exterior. */
    if (context == 244) { destination=location_make(28512,31328); return 1; }
    static unsigned previous;
    static int radius = 1, dx = -1, dy = -1;
    if (context != previous) { radius = 1; dx = dy = -1; previous = context; }
    int64_t pc = coop_campaign_object(0), from = obj_field_int64_get(pc, OBJ_F_LOCATION), centre = sector_id_from_loc(from);
    if (!sector_is_blocked(centre) && townmap_get(centre) == TOWNMAP_NONE
        && (context != 212 || return_route_open(from))) { destination = from; return 1; }
    if (radius > 6) return -1;
    int xoff = dx, yoff = dy, ring = radius;
    if (++dx > radius) { dx = -radius; if (++dy > radius) { radius++; dx = dy = -radius; } }
    if (abs(xoff) != ring && abs(yoff) != ring) return 0;
    int64_t limit_x, limit_y; sector_limits_get(&limit_x, &limit_y);
    int64_t x = SECTOR_X(centre) + xoff, y = SECTOR_Y(centre) + yoff;
    if (x < 0 || y < 0 || x >= limit_x || y >= limit_y) return 0;
    int64_t id = SECTOR_MAKE(x, y);
    if (sector_is_blocked(id) || townmap_get(id) != TOWNMAP_NONE) return 0;
    int64_t loc = sector_loc_from_id(id); loc = location_make(location_get_x(loc) + 32, location_get_y(loc) + 32);
    if (context == 212 && !return_route_open(loc)) return 0;
    if (tile_is_blocking(loc, false)) return 0;
    int8_t steps[512];
    PathCreateInfo path = { .obj = pc, .from = from, .to = loc, .max_rotations = 512, .rotations = steps };
    if (path_make(&path) <= 0) return 0;
    destination = loc; return 1;
}
static int64_t attached(int script, int sap)
{
    int64_t obj; int iterator;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)) continue;
        Script value = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, sap, &value);
        if (value.num == script) return obj;
    } while (obj_inst_next(&obj, &iterator));
    return 0;
}
typedef struct OriginalPassage {
    unsigned phase;
    int script, map, x, y, approach_x, approach_y;
} OriginalPassage;
static const OriginalPassage* later_passage(unsigned phase)
{
    static const OriginalPassage passages[] = {
        {223,2356,32,507,221,28376,31171}, {227,2353,32,168,218,282,218},
        {229,2354,32,284,217,168,218}, {231,2355,1,28378,31171,507,221},
        {246,2379,33,2051,2038,19689,37487}, {247,2362,33,1951,2033,2094,1982},
        {248,2363,33,1803,2032,1963,1998}, {249,2380,33,1708,1994,1882,1998},
        {250,2381,33,1882,1998,1708,1994}, {251,2365,33,1836,1930,1814,2053},
        {252,2383,33,1561,1990,1818,1940}, {253,2388,33,1444,2042,1530,2008},
        {255,2389,33,1530,2008,1444,2042}, {256,2384,33,1818,1940,1561,1990},
        {257,2366,33,1814,2053,1836,1930}, {258,2364,33,1963,1998,1803,2032},
        {259,2361,33,2094,1982,1951,2033}, {260,2387,1,19689,37487,2051,2038},
        {273,2642,72,361,349,932,1126}, {277,2644,73,696,93,1052,871}
    };
    for (unsigned i = 0; i < SDL_arraysize(passages); i++)
        if (passages[i].phase == phase) return &passages[i];
    return NULL;
}
static int64_t original_poem(void)
{
    ObjectID id = { .type = OID_TYPE_GUID, .d.g.data = {
        0x00,0xb5,0xb8,0xa3,0x50,0x75,0x2b,0x4e,0x88,0xe2,0x0d,0x20,0xf7,0xf1,0x05,0x4c } };
    return obj_pool_perm_lookup(id);
}
typedef struct OriginalEnding {
    bool framebuffer, good, terminal, slide_captured, original_device, farewell;
    unsigned phase, story, epoch, sequence, slides, terminal_id;
    uint64_t at;
} OriginalEnding;
static void original_ending_observe(void* context)
{
    OriginalEnding* ending = context;
    const CoopPresentView* view = coop_present_current(coop_runtime_presentation());
    if (!view || !view->sequence) return;
    unsigned epoch = coop_runtime_epoch();
    if (epoch && ending->epoch != epoch) { ending->epoch = epoch; ending->sequence = 0; }
    uint64_t now = SDL_GetTicks();
    if (ending->sequence != view->sequence) {
        ending->sequence = view->sequence; ending->at = now;
        if (ending->phase == 283 && view->kind == COOP_PRESENT_SLIDE) {
            ending->slides++;
            fprintf(stderr,"ARCMP connected original ending slide sequence=%u id=%u\n",view->sequence,view->id);
        }
    }
    if (ending->phase == 283 && view->kind == COOP_PRESENT_END && view->phase == COOP_PRESENT_COMPLETE) {
        ending->terminal = view->participants == 15 && view->ready == 15 && view->done == 15;
        ending->terminal_id = view->id;
    }
    if (ending->phase == 283 && ending->framebuffer && !ending->slide_captured
        && view->kind == COOP_PRESENT_SLIDE && view->phase == COOP_PRESENT_PLAYING
        && slide_ui_coop_current() == (int)view->id && now-ending->at>500) {
        ending->good &= tig_video_screenshot_save("arcmp-connected-ending-slide.bmp") == TIG_OK;
        ending->slide_captured = true;
    }
    if ((view->kind == COOP_PRESENT_MOVIE || view->kind == COOP_PRESENT_SLIDE) && now-ending->at>1000)
        coop_runtime_present_input();
}
/* Follow all original Schuyler stairs and the living family's offered ritual.
 * No story writes, visibility overrides, quieting, resources or placement. */
static void investigate(CoopGameplay* game, int64_t pc, unsigned member, uint64_t now)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    static uint32_t spoken_epoch, spoken_conversation, spoken_revision;
    static uint64_t talk_at, attack_at, door_at;
    if (view->active) {
        if (view->chooser != coop_session_local_slot(held)) return;
        if (spoken_epoch == coop_runtime_epoch() && spoken_conversation == view->conversation && spoken_revision == view->revision) return;
        int64_t speaker = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc)); Script script = { 0 };
        if (speaker) obj_arrayfield_script_get(speaker, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        int goal = script.num == 1059 ? 25 : script.num == 1049 ? -15 : script.num == 1054 ? 50 : 0;
        int option = coop_campaign_probe_option(view, script.num, goal);
        if (script.num == 1049 && view->option_count > 1) {
            /* The graph-only shortcut ignores the changing insult counter.
             * Follow the original peaceful exchange, not its shorter threats. */
            static const char* peaceful[] = {
                "Forgive the intrusion", "utmost importance", "identity of one of your clients",
                "not here to make trouble", "I'd appreciate any knowledge", "information about a ring I once had",
                "Just take a look", "listen for a moment", "I guessed as much", "But exactly WHAT",
                "What exactly do you mean", "That explains the dwarf zombies", "built your store",
                "No offense, but I can see", "as you said, we are", "dwarves might consider",
                "I'm convinced", "Agreed."
            };
            option = -1;
            for (unsigned p = 0; p < SDL_arraysize(peaceful) && option < 0; p++)
                for (unsigned i = 0; i < view->option_count; i++)
                    if (contains(view->options[i], peaceful[p])) { option = (int)i; break; }
            if (option < 0) return;
        }
        if (option < 0 && view->option_count == 1) option = 0;
        if (option < 0 && goal) option = dialogue_choice(view, 2);
        if (option < 0 && !goal) { coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); return; }
        if (option >= 0) {
            fprintf(stderr, "ARCMP ordinary original Schuyler choice script=%d option=%d text=%s\n", script.num, option, view->options[option]);
            key((unsigned)option); spoken_epoch = coop_runtime_epoch(); spoken_conversation = view->conversation; spoken_revision = view->revision;
        }
        return;
    }
    int map = map_current_map(), script = 0; bool talk = false;
    if (map == 1 && !script_global_flag_get(1018)) { script = 1059; talk = true; }
    else if (map == 1) script = 1205;
    else if (map == 3) script = script_story_state_get() < 2 ? 1207 : 1206;
    else if (map == 4) script = script_story_state_get() < 2 ? 1209 : 1208;
    else if (map == 5 && script_story_state_get() >= 2) script = 1210;
    else if (map == 5) {
        int64_t father = npc(1054);
        script = father && (obj_field_int32_get(father, OBJ_F_SPELL_FLAGS) & OSF_SPOKEN_WITH_DEAD) ? 1054 : 1049; talk = true;
    }
    int64_t target = attached(script, talk ? SAP_DIALOG : SAP_USE), leader = coop_campaign_object(0);
    if (!target) return;
    int64_t goal = obj_field_int64_get(target, OBJ_F_LOCATION);
    int64_t magnus = magnus_actor();
    if (map == 4 && magnus && critter_pc_leader_get(magnus) == coop_campaign_object(1)) {
        bool waiting = (obj_field_int32_get(magnus, OBJ_F_NPC_FLAGS) & ONF_AI_WAIT_HERE) != 0;
        bool park = script_story_state_get() < 2;
        if (waiting != park) {
            if (member == 1 && !coop_followers_busy(coop_gameplay_followers(game))) {
                if (object_dist(pc, magnus) > 8) walk(pc, obj_field_int64_get(magnus, OBJ_F_LOCATION), member, 5);
                else coop_runtime_follower_order(magnus, park ? COOP_FOLLOW_WAIT : COOP_FOLLOW_RESUME, 0, 0, false);
            }
            return;
        }
    }
    if (member) {
        if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), member, 3);
        return;
    }
    /* The original basement door can be broken using ordinary force attacks
     * if its owner refuses access. Native damage, crime/reaction and wear apply. */
    int64_t door = 0, obj; int iterator;
    if (map == 1 && script == 1205 && object_dist(pc, target) > 6 && obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_PORTAL && !portal_is_open(obj) && !object_is_busted(obj)
            && object_locked_get(obj) && object_dist(pc, obj) <= 6
            && location_dist(obj_field_int64_get(obj, OBJ_F_LOCATION), goal) < 24) { door = obj; break; }
    } while (obj_inst_next(&obj, &iterator));
    if (door) {
        if (!member && now - door_at > 10000) {
            AnimID id; AnimRunInfo* run = NULL;
            if (sub_423300(pc, &id)) anim_id_to_run_info(&id, &run);
            int64_t weapon = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
            fprintf(stderr, "ARCMP ordinary original basement door hp=%d/%d material=%d lock=%d distance=%lld weapon-hp=%d root=%d goal=%d state=%d\n",
                object_hp_current(door), object_hp_max(door), obj_field_int32_get(door, OBJ_F_MATERIAL), obj_field_int32_get(door, OBJ_F_PORTAL_LOCK_DIFFICULTY), (long long)object_dist(pc, door),
                weapon ? object_hp_current(weapon) : 0, run ? run->goals[0].type : -1, run && run->cur_stack_data ? run->cur_stack_data->type : -1, run ? run->current_state : -1);
            door_at = now;
        }
        if (!coop_battle_busy(coop_gameplay_battle(game)) && now - attack_at > 4000) {
            coop_runtime_combat(combat_critter_is_combat_mode_active(pc) ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE,
                combat_critter_is_combat_mode_active(pc) ? COOP_ATTACK_FORCE | (COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT) : 1,
                combat_critter_is_combat_mode_active(pc) ? door : 0);
            attack_at = now;
        }
        return;
    }
    if (!talk && object_dist(pc, target) > 6) { walk(pc, goal, 0, 4); return; }
    if (now - talk_at < 4000 || coop_items_busy(coop_gameplay_items(game))) return;
    fprintf(stderr, "ARCMP ordinary original Schuyler interaction map=%d script=%d talk=%d distance=%lld story=%d\n", map, script, talk, (long long)object_dist(pc, target), script_story_state_get());
    if (talk) coop_runtime_talk(target); else coop_runtime_world_use(target);
    talk_at = now;
}
static bool receipts(bool write)
{
    uint8_t bytes[RECEIPT_BYTES] = { 0 }; memcpy(bytes, "ACORD01", 8);
    memcpy(bytes + 8, obj_get_id(coop_campaign_object(0)).d.g.data, 16);
    if (write) {
        for (unsigned i = 0; i < 4; i++) {
            coop_object_write32(bytes + 24 + i * 4, (uint32_t)initial_gold[i]);
            coop_object_write32(bytes + 40 + i * 4, (uint32_t)paid[i]); memcpy(bytes + 56 + i * 17, bought[i].bytes, 17);
        }
        coop_object_write32(bytes + 124, coop_snapshot_crc(bytes, 124));
        FILE* f = fopen("arcmp-ordinary-receipts.bin", "wb"); if (!f) return false;
        bool good = fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes); if (fclose(f)) good = false; return good;
    }
    uint8_t previous[RECEIPT_BYTES]; FILE* f = fopen("arcmp-ordinary-receipts.bin", "rb"); if (!f) return false;
    bool good = fread(previous, 1, sizeof(previous), f) == sizeof(previous) && fgetc(f) == EOF; fclose(f);
    good &= !memcmp(previous, bytes, 24) && coop_object_read32(previous + 124) == coop_snapshot_crc(previous, 124);
    if (good) for (unsigned i = 0; i < 4; i++) { initial_gold[i] = (int)coop_object_read32(previous + 24 + i * 4); paid[i] = (int)coop_object_read32(previous + 40 + i * 4); memcpy(bought[i].bytes, previous + 56 + i * 17, 17); }
    return good;
}
static bool announce(unsigned phase)
{
    if (gather_phase(phase)) {
        int script = phase == 1 ? 1324 : phase == 28 ? 1041 : phase == 51 || phase == 62 ? 1060 : phase == 54 ? 2075 : phase == 56 ? 2074 : phase == 65 ? 1047 : 1040;
        int64_t target = phase == 36 ? named_item(5607) : npc(script);
        if (phase == 65 && !target) target = map_current_map() == 13 ? npc(1937)
            : attached(map_current_map() == 1 ? 1267 : 1266, SAP_USE);
        if (phase == 58) { destination = location_make(61944, 65706); target = 0; }
        if (phase == 36 && target) { int64_t parent; if (item_parent(target, &parent)) target = parent; }
        if (!target && phase != 58) { fprintf(stderr, "ARCMP ordinary missing original NPC script=%d\n", script); return false; }
        if (target) destination = obj_field_int64_get(target, OBJ_F_LOCATION);
    }
    if (phase == 33) {
        int64_t gate = bridge_gate(); if (!gate) { fprintf(stderr, "ARCMP ordinary missing original bridge gate\n"); return false; }
        destination = obj_field_int64_get(gate, OBJ_F_LOCATION);
    }
    char text[100]; snprintf(text, sizeof(text), "ordinary-phase:%u:%lld:%lld", phase, (long long)location_get_x(destination), (long long)location_get_y(destination));
    return coop_session_chat(held, text);
}
static int64_t affordable_weapon(int64_t actor, int64_t merchant, const CoopObjectId* excluded, unsigned count)
{
    int64_t stock = critter_substitute_inventory_get(merchant); if (!stock) stock = merchant;
    int type = obj_field_int32_get(stock, OBJ_F_TYPE), best_score = -1; int64_t best = 0;
    int num = obj_field_int32_get(stock, type == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_INVENTORY_NUM : OBJ_F_CRITTER_INVENTORY_NUM);
    for (int i = 0; i < num; i++) {
        int64_t item = obj_arrayfield_handle_get(stock, type == OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_INVENTORY_LIST_IDX : OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        if (obj_field_int32_get(item, OBJ_F_TYPE) != OBJ_TYPE_WEAPON || item_inventory_location_get(item) >= 120
            || item_weapon_range(item, actor) > 2 || item_weapon_min_strength(item, actor) > stat_level_get(actor, STAT_STRENGTH)) continue;
        CoopObjectId id; coop_object_id_from_engine(obj_get_id(item), &id); bool reject = false;
        for (unsigned j = 0; j < count; j++) reject |= !memcmp(id.bytes, excluded[j].bytes, 17);
        if (reject) continue;
        int low, high; item_weapon_damage(item, actor, DAMAGE_TYPE_NORMAL, SKILL_MELEE, false, &low, &high);
        int score = high * 100 + low * 10 - item_worth(item) / 10;
        if (score > best_score) { best_score = score; best = item; }
    }
    return best;
}
static int weapon_score(int64_t item, int64_t actor)
{
    if (!item) return 0;
    int low, high; item_weapon_damage(item, actor, DAMAGE_TYPE_NORMAL, SKILL_MELEE, false, &low, &high);
    return high * 100 + low * 10 - item_worth(item) / 10;
}
static int64_t best_owned_weapon(int64_t pc)
{
    int64_t best = item_wield_get(pc, ITEM_INV_LOC_WEAPON);
    int count = obj_field_int32_get(pc, OBJ_F_CRITTER_INVENTORY_NUM);
    for (int i = 0; i < count; i++) {
        int64_t item = obj_arrayfield_handle_get(pc, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        int slot = ITEM_INV_LOC_WEAPON;
        if (obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_WEAPON && item_weapon_range(item, pc) <= 2
            && weapon_score(item, pc) > weapon_score(best, pc) && !item_check_insert(item, pc, &slot)) best = item;
    }
    return best;
}
static int64_t kit_merchant(void)
{
    static ObjectID id;
    int64_t found = id.type ? obj_pool_perm_lookup(id) : 0;
    if (found) return found;
    int64_t obj, best = 0; int iterator, distance = 100000;
    if (obj_inst_first(&obj, &iterator)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC || !critter_is_active(obj)
            || (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
        Script s = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &s);
        if (s.num != 1824 && s.num != 1819 && s.num != 1991) continue;
        int d = object_dist(coop_campaign_object(0), obj);
        if (d < distance) { best = obj; distance = d; }
    } while (obj_inst_next(&obj, &iterator));
    if (best) { id = obj_get_id(best); fprintf(stderr, "ARCMP ordinary native outfitter name=%d distance=%d\n", obj_field_int32_get(best, OBJ_F_NAME), distance); }
    return best;
}
/* Preparation is ordinary campaign play: pool the party's actual money, buy
 * original stock, distribute/equip it, ask Magnus to join an independent human,
 * rest by consent and retain the resulting checkpoint. No supplies are granted. */
static bool prepare(CoopGameplay* game, int64_t pc, unsigned member)
{
    static const unsigned owners[] = { 2, 0, 1, 3 };
    static CoopObjectId selected, acquired[4], excluded[32];
    static unsigned cursor, rejected, buying, delivery;
    static uint32_t quote_sequence, voted;
    static int purse, fee;
    static uint32_t conversation, revision;
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    int64_t leader = coop_campaign_object(0), magnus = magnus_actor(), merchant = kit_merchant();
    if (kit_stage >= 2) {
        if (coop_runtime_epoch() > kit_epoch && control->phase == COOP_CONTROL_IDLE && !control->paused) {
            if (!member) kit_next();
            return true;
        }
        if (!member && control->phase == COOP_CONTROL_IDLE && !control->paused && !coop_controls_pending(coop_runtime_controls()))
            coop_runtime_control(kit_stage == 2 ? COOP_CONTROL_REST : COOP_CONTROL_SAVE, kit_stage == 2 ? 2 : 0, 0, 0);
        if (member && control->phase == COOP_CONTROL_VOTING && control->proposal != voted
            && coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0)) voted = control->proposal;
        if (control->phase == COOP_CONTROL_IDLE && control->paused && coop_session_is_host(held)) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
        return true;
    }
    if (member && object_dist(pc, leader) > 3 && !view->active) {
        walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), member, 3); return true;
    }
    if (member && !view->active && !coop_items_busy(coop_gameplay_items(game))) {
        int64_t weapon = best_owned_weapon(pc);
        if (weapon && item_wield_get(pc, ITEM_INV_LOC_WEAPON) != weapon) {
            coop_runtime_item(COOP_ITEM_MOVE, weapon, 0, ITEM_INV_LOC_WEAPON); return true;
        }
    }
    if (kit_stage == 1) {
        if (!magnus) return false;
        if (!member && object_dist(pc, magnus) > 4) walk(pc, obj_field_int64_get(magnus, OBJ_F_LOCATION), member, 4);
        if (member != 1) return true;
        bool joined = critter_pc_leader_get(magnus) == pc;
        if (joined && !view->active) { resources(magnus, -2); kit_next(); return true; }
        if (!view->active) {
            int reason = ai_check_follow(magnus, pc, false);
            if (reason != AI_FOLLOW_OK) { fprintf(stderr, "ARCMP ordinary native Magnus recruitment unavailable reason=%d level=%d human-level=%d\n", reason, stat_level_get(magnus, STAT_LEVEL), stat_level_get(pc, STAT_LEVEL)); kit_next(); return true; }
            if (object_dist(pc, magnus) <= 6) coop_runtime_talk(magnus);
            return true;
        }
        if (view->chooser != coop_session_local_slot(held)) return true;
        if (joined) { coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); return true; }
        if (view->conversation == conversation && view->revision == revision) return true;
        int choice = coop_campaign_probe_option(view, 1046, 198);
        if (choice < 0) choice = coop_campaign_probe_option(view, 1046, 209);
        if (choice < 0) { fprintf(stderr, "ARCMP ordinary Magnus offered refusal reply=%s native-reason=%d\n", view->reply, ai_check_follow(magnus, pc, false)); coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); kit_next(); }
        else { fprintf(stderr, "ARCMP ordinary Magnus original choice=%s\n", view->options[choice]); key((unsigned)choice); conversation = view->conversation; revision = view->revision; }
        return true;
    }
    if (!merchant) return false;
    if (member) {
        int gold = item_gold_get(pc);
        if (gold && !view->active && !coop_items_busy(coop_gameplay_items(game)))
            coop_runtime_item(COOP_ITEM_GIVE_AMOUNT, item_find_first_of_type(pc, OBJ_TYPE_GOLD), 0, gold);
        return true;
    }
    if (object_dist(pc, merchant) > 5 && !view->active) { walk(pc, obj_field_int64_get(merchant, OBJ_F_LOCATION), 0, 4); return true; }
    for (unsigned i = 1; i < 4; i++) if (object_dist(pc, coop_campaign_object(i)) > 6 || item_gold_get(coop_campaign_object(i))) return true;
    if (cursor >= 4) {
        if (view->active) { if (view->mode == 3) inven_ui_destroy(); else coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); return true; }
        while (delivery < 4) {
            unsigned owner = owners[delivery];
            int64_t item = acquired[owner].bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(acquired[owner])) : 0, parent = 0;
            if (!item) { delivery++; continue; }
            item_parent(item, &parent);
            if (parent == pc && owner && !coop_items_busy(coop_gameplay_items(game))) { coop_runtime_item(COOP_ITEM_GIVE, item, owner, -1); return true; }
            if (parent != coop_campaign_object(owner)) return false;
            if (item_wield_get(parent, ITEM_INV_LOC_WEAPON) != item) {
                if (!owner && !coop_items_busy(coop_gameplay_items(game))) coop_runtime_item(COOP_ITEM_MOVE, item, 0, ITEM_INV_LOC_WEAPON);
                return true;
            }
            delivery++;
        }
        for (unsigned i = 0; i < 4; i++) resources(coop_campaign_object(i), (int)i);
        if (!item_wield_get(coop_campaign_object(2), ITEM_INV_LOC_WEAPON)) return false;
        kit_next(); return true;
    }
    if (!view->active) { coop_runtime_talk(merchant); return true; }
    if (view->chooser != coop_session_local_slot(held)) return true;
    if (view->mode != 3) {
        int choice = dialogue_choice(view, 1);
        if (choice < 0) return false;
        key((unsigned)choice); return true;
    }
    CoopServices* service = coop_gameplay_services(game); unsigned status;
    uint32_t result = coop_services_last_result(service, &status);
    if (coop_services_busy(service)) return true;
    unsigned owner = owners[cursor];
    int64_t item = selected.bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(selected)) : 0;
    if (!buying) {
        item = affordable_weapon(coop_campaign_object(owner), merchant, excluded, rejected);
        if (!item) { cursor++; rejected = 0; return true; }
        int64_t recipient = coop_campaign_object(owner);
        if (weapon_score(item, recipient) <= weapon_score(item_wield_get(recipient, ITEM_INV_LOC_WEAPON), recipient)) {
            cursor++; rejected = 0; return true;
        }
        coop_object_id_from_engine(obj_get_id(item), &selected); purse = item_gold_get(pc);
        quote_sequence = result + 1; coop_services_hover(service, item); buying = 1;
    } else if (buying == 1 && result == quote_sequence) {
        uint32_t quote = coop_services_quote(service, &fee);
        if (status || !quote || fee <= 0 || fee > item_gold_get(pc)) {
            if (rejected >= SDL_arraysize(excluded)) { cursor++; rejected = 0; }
            else excluded[rejected++] = selected;
            buying = 0; memset(&selected, 0, sizeof(selected));
        } else if (coop_services_buy(service, item)) buying = 2;
    } else if (buying == 2 && result == quote_sequence + 1) {
        int64_t parent = 0; if (item) item_parent(item, &parent);
        if (status || parent != pc || item_gold_get(pc) != purse - fee) return false;
        acquired[owner] = selected;
        fprintf(stderr, "ARCMP ordinary native kit purchase recipient=%u description=%d score=%d fee=%d gold=%d\n", owner, obj_field_int32_get(item, OBJ_F_DESCRIPTION), weapon_score(item, coop_campaign_object(owner)), fee, item_gold_get(pc));
        cursor++; rejected = buying = 0; memset(&selected, 0, sizeof(selected));
    }
    return true;
}
static bool recovery_needed(void)
{
    for (unsigned i = 0; i < 4; i++) {
        int64_t actor = coop_campaign_object(i);
        if (object_hp_current(actor) < object_hp_max(actor) || critter_fatigue_current(actor) * 2 < critter_fatigue_max(actor)) return true;
    }
    int64_t healer = npc(1324);
    return healer && !critter_is_dead(healer) && object_hp_current(healer) < object_hp_max(healer);
}
static bool painting(CoopGameplay* game, int64_t pc, unsigned member)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    static uint32_t conversation, revision, epoch;
    int64_t leader = coop_campaign_object(0), patron = npc(1291), item = named_item(5627), parent = 0;
    if (item) item_parent(item, &parent);
    if (member) { if (!view->active && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), member, 3); return true; }
    if (parent == pc && coop_items_loot_target(coop_gameplay_items(game))) {
        if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1);
        return true;
    }
    int state = quest_state_get(pc, 1063);
    if (view->active) {
        if (view->chooser != coop_session_local_slot(held)) return true;
        if (state == QUEST_STATE_ACCEPTED && parent != pc) { coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); return true; }
        if (epoch == coop_runtime_epoch() && conversation == view->conversation && revision == view->revision) return true;
        static const char* polite[] = { "My name is", "I've nothing to hide", "Robbery!", "Have the police", "offer my services", "honored to take", "I've brought back your painting" };
        int choice = -1;
        for (unsigned i = 0; i < SDL_arraysize(polite) && choice < 0; i++)
            for (unsigned j = 0; j < view->option_count; j++) if (strstr(view->options[j], polite[i])) { choice = (int)j; break; }
        if (choice < 0) choice = coop_campaign_probe_option(view, 1291, parent == pc ? 102 : 58);
        if (choice < 0) { fprintf(stderr, "ARCMP ordinary unhandled painting dialogue reply=%s\n", view->reply); return false; }
        fprintf(stderr, "ARCMP ordinary original painting choice=%s\n", view->options[choice]); key((unsigned)choice);
        epoch = coop_runtime_epoch(); conversation = view->conversation; revision = view->revision; return true;
    }
    if (state != QUEST_STATE_ACCEPTED || parent == pc) {
        if (!patron) return false;
        if (object_dist(pc, patron) > 6) walk(pc, obj_field_int64_get(patron, OBJ_F_LOCATION), member, 4);
        else coop_runtime_talk(patron);
        return true;
    }
    if (!item) return false;
    CoopItems* items = coop_gameplay_items(game);
    if (coop_items_busy(items)) return true;
    int64_t target = parent ? parent : item;
    if (object_dist(pc, target) > 1) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), member, 1);
    else if (!parent) coop_runtime_item(COOP_ITEM_PICKUP, item, 0, -1);
    else if (coop_items_loot_target(items) != parent) coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1);
    else coop_runtime_item(COOP_LOOT_TAKE, item, 0, -1);
    return true;
}
static bool camp_needed(void)
{
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        if (object_hp_current(pc) * 5 < object_hp_max(pc) * 4 || critter_fatigue_current(pc) * 4 < critter_fatigue_max(pc) * 3) return true;
    }
    return false;
}
static void dungeon_recovery(int64_t pc, unsigned member);
static void camp(int64_t pc, unsigned member)
{
    static int64_t goal;
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    int64_t leader = coop_campaign_object(0), loc = obj_field_int64_get(leader, OBJ_F_LOCATION);
    bool outside = true;
    for (unsigned i = 0; i < 4; i++) {
        int64_t sector = sector_id_from_loc(obj_field_int64_get(coop_campaign_object(i), OBJ_F_LOCATION));
        outside &= !sector_is_blocked(sector) && townmap_get(sector) == TOWNMAP_NONE;
    }
    if (outside || control->phase != COOP_CONTROL_IDLE || control->paused) { dungeon_recovery(pc, member); return; }
    if (!goal) {
        int result = escape_destination(52);
        if (result > 0) goal = destination;
        return;
    }
    if (member) { if (object_dist(pc, leader) > 3) walk(pc, loc, member, 3); }
    else walk(pc, goal, member, 4);
}
static void dungeon_recovery(int64_t pc, unsigned member)
{
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    static uint32_t voted;
    int64_t leader = coop_campaign_object(0); bool gathered = true;
    for (unsigned i = 1; i < 4; i++) gathered &= object_dist(leader, coop_campaign_object(i)) <= 6;
    if (control->phase == COOP_CONTROL_IDLE && !control->paused) {
        if (member && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), member, 3);
        if (!member && gathered && !coop_controls_pending(coop_runtime_controls())) coop_runtime_control(COOP_CONTROL_REST, 7, 0, 0);
    }
    if (member && control->phase == COOP_CONTROL_VOTING && control->proposal != voted
        && coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0)) voted = control->proposal;
    if (control->phase == COOP_CONTROL_IDLE && control->paused && coop_session_is_host(held)) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
}
static bool equipped(unsigned member)
{
    int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(bought[member]));
    return item && item_wield_get(coop_campaign_object(member), ITEM_INV_LOC_WEAPON) == item;
}
static bool image(unsigned phase, bool framebuffer)
{
    if (!framebuffer) return true;
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION)); gamelib_redraw(); iso_redraw(); tig_window_display();
    char path[80]; snprintf(path, sizeof(path), "arcmp-ordinary-%u.bmp", phase); return tig_video_screenshot_save(path) == TIG_OK;
}
bool coop_ordinary_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, manual = false; unsigned player = 4, port = 35750, first = 1, last = 27;
    const char* route = getenv("ARCMP_ORDINARY_MAINLINE");
    bool mainline = route && !strcmp(route, "1");
    const char* strength = getenv("ARCMP_PROBE_STRENGTHEN_PARTY");
    bool strengthened = strength && !strcmp(strength, "1");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-ordinary-first=", 22)) first = (unsigned)atoi(argv[i] + 22);
        if (!strncmp(argv[i], "--coop-ordinary-last=", 21)) last = (unsigned)atoi(argv[i] + 21);
    }
    if (player >= 4 || host != (player == 0) || !port || port > 65535 || first < 1 || first > last || (last != 27 && last != 48 && last != 73 && last != 76 && last != 82 && last != 88 && last != 94 && last != 102 && last != 111 && last != 119 && last != 132 && last != 142 && last != 165 && last != 175 && last != 184 && last != 201 && last != 217 && last != 243 && last != 262 && last != 272 && last != 281 && last != LAST)) return false;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Traveller %u", player);
    FILE* f = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 }; if (!f) return false;
    bool good = fread(hex, 1, 65, f) >= 64; fclose(f);
    for (unsigned i = 0; good && i < 32; i++) { char pair[] = { hex[i*2], hex[i*2+1], 0 }, *end; config.content_id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end; }
    good &= coop_identity_load_or_create("arcmp-player.id", config.identity);
    held = good ? coop_session_create(&config) : NULL; if (!held) return false; coop_runtime_probe_enable();
    if (host) combat_damage_observer_set(damage_observed);
    uint64_t start = SDL_GetTicks(), playing = 0, chats = 0, at = 0, next = 0, ended = 0, progress = 0;
    unsigned journeys = 0, pending_phase = 0, control_sequence = 0, vote_proposal = 0, shop_wait_epoch = 0;
    int starting_hp[4] = { 0 };
    int starting_points[4] = { 0 }, starting_xp[4] = { 0 };
    bool ring_initial = false;
    uint32_t withdrawing_turn = 0;
    uint64_t plan_at = 0;
    unsigned phase = 0, completed = 0, baseline = 0, ack[4] = { 0 }, choices = 0, purchases = 0, quote_sequence = 0, attempts = 0;
    CoopObjectId selected = { 0 }, excluded[32] = { 0 };
    int member = -1, fee = 0, purse = 0; bool advance_sent = false;
    bool registered = false, initialized = false, announced = false, sent = false, voted = false, done = false, finishing = false, bought_once = false, gear_checked = false, bindings_checked = false, control_started = false;
    OriginalEnding ending = { .framebuffer = framebuffer, .good = true };
    bool ending_reported = false;
    uint32_t phase_save_sequence = 0;
    unsigned save_connection = 0;
    if (last == 283) { tig_movie_probe_mode(false); coop_runtime_present_observer(original_ending_observe,&ending); }
    good &= (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    while (good && SDL_GetTicks() - start < 1800000) {
        uint64_t now = SDL_GetTicks();
        static uint64_t previous;
        if (previous && now - previous > 30000) { fprintf(stderr, "ARCMP ordinary clock jump; retain accepted checkpoints\n"); good = false; break; }
        previous = now; tig_ping(); coop_session_poll(held, now); coop_lobby_tick();
        if (last == 283 && ending.terminal && coop_runtime_phase() == COOP_RUNTIME_OFF) {
            good &= !coop_campaign_active() && mainmenu_ui_is_active() && mainmenu_ui_window_type_get() == MM_WINDOW_MAINMENU;
            if (framebuffer) { tig_window_display(); good &= tig_video_screenshot_save("arcmp-probe.bmp") == TIG_OK; }
            break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP ordinary runtime: %s\n", coop_runtime_status()); good = false; break; }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            uint8_t slot = coop_session_local_slot(held);
            if (!registered) { char name[32]; snprintf(name, sizeof(name), "Traveller %u", player); registered = coop_session_character(held, 16067 + player, name); }
            else if (coop_session_players(held)[slot].prototype && !coop_session_players(held)[slot].ready) coop_session_ready(held, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(held)[i].connected;
            if (host && count == 4 && coop_session_all_ready(held)) good &= coop_runtime_recover_checkpoint();
        }
        if (chats > coop_session_chat_count(held)) chats = 0;
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats); if (!chat) continue;
            unsigned value, owner; long long x, y; char id[35]; int price;
            if (!chat->slot && sscanf(chat->text, "ordinary-phase:%u:%lld:%lld", &value, &x, &y) == 3 && value > phase) {
                good &= value == (phase ? phase + 1 : first) && value <= last;
                phase = value; baseline = coop_runtime_epoch(); destination = location_make(x, y); at = now; next = now + 400;
                for (unsigned slot=0;slot<4;slot++)
                    if (coop_gameplay_member_for_connection(coop_runtime_gameplay(),slot)==3) save_connection=slot;
                phase_save_sequence = coop_controls_current(coop_runtime_controls())->sequences[save_connection];
                ending.phase = phase;
                sent = voted = done = bought_once = advance_sent = control_started = false; choices = quote_sequence = attempts = journeys = 0; memset(&selected, 0, sizeof(selected));
                fprintf(stderr, "ARCMP ordinary phase=%u member=%d epoch=%u map=%d target=%lld,%lld\n", phase, member, baseline, map_current_map(), x, y);
            }
            if (chat->slot < 4 && sscanf(chat->text, "ordinary-done:%u", &value) == 1 && value <= phase) ack[chat->slot] = value;
            if (host && phase == 52 && kit_stage < 4 && chat->slot < 4
                && sscanf(chat->text, "ordinary-kit-request:%u", &value) == 1 && value == kit_stage + 1
                && coop_campaign_slot_for_owner(coop_session_players(held)[chat->slot].identity) == (kit_stage == 1 ? 1 : 0)) {
                kit_stage = value; kit_epoch = coop_runtime_epoch();
                char message[64]; snprintf(message, sizeof(message), "ordinary-kit-stage:%u:%u", kit_stage, kit_epoch);
                good &= coop_session_chat(held, message);
            }
            unsigned epoch;
            if (chat->slot == 0 && phase == 52 && sscanf(chat->text, "ordinary-kit-stage:%u:%u", &value, &epoch) == 2
                && value > kit_stage && value <= 4) {
                kit_stage = value; kit_epoch = epoch;
                fprintf(stderr, "ARCMP ordinary native kit stage=%u epoch=%u\n", kit_stage, kit_epoch);
            }
            if (chat->slot < 4 && sscanf(chat->text, "ordinary-purchase:%u:%34s:%d", &owner, id, &price) == 3) {
                /* Only the host retains other profiles' private identities.
                 * Replicas authenticate the receipt against the shared chooser;
                 * native parent/currency checks below establish the outcome. */
                CoopGameplay* active = coop_runtime_gameplay();
                const CoopDialogueView* shop = active ? coop_dialogue_current(coop_gameplay_dialogue(active)) : NULL;
                int sender = host ? coop_campaign_slot_for_owner(coop_session_players(held)[chat->slot].identity) : (int)owner;
                bool valid = phase >= 9 && phase <= 21 && (phase - 9) % 4 == 0 && owner == (phase - 9) / 4
                    && shop && shop->active && shop->mode == 3 && shop->chooser == chat->slot
                    && owner < 4 && sender == (int)owner && strlen(id) == 34 && price > 0;
                if (!valid) fprintf(stderr, "ARCMP ordinary invalid purchase metadata phase=%u sender=%u owner=%u text=%s\n", phase, chat->slot, owner, chat->text);
                good &= valid;
                if (good) { paid[owner] = price; for (unsigned i = 0; i < 17; i++) { char pair[] = { id[i*2], id[i*2+1], 0 }, *end; bought[owner].bytes[i] = (uint8_t)strtoul(pair, &end, 16); good &= !*end; } }
            }
        }
        CoopGameplay* game = coop_runtime_gameplay();
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            int64_t pc = player_get_local_pc_obj(), virgil = npc(1324), merchant = npc(phase >= 12 && phase <= 23 ? 1140 : 1040);
            const CoopControlView* control = coop_controls_current(coop_runtime_controls());
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (last == 283 && phase >= 282) {
                ending.story = (unsigned)script_story_state_get();
                ending.original_device |= item_find_by_name(coop_campaign_object(0),5616) != OBJ_HANDLE_NULL;
                ending.farewell |= view->active && contains(view->reply,"My road ends here");
                if (phase == 283 && !ending_reported) {
                    good &= ending.story == 22 && ending.original_device && script_global_flag_get(2042)
                        && script_global_flag_get(2718) && quest_state_get(pc,1057) == QUEST_STATE_ACCEPTED
                        && coop_engine_probe_report("runtime-ordinary");
                    ending_reported = true;
                }
            }
            if (!playing && strengthened) {
                for (unsigned i = 0; i < 4; i++) {
                    int64_t actor = coop_campaign_object(i);
                    stat_base_set(actor, STAT_STRENGTH, 20);
                    stat_base_set(actor, STAT_DEXTERITY, 20);
                    basic_skill_points_set(actor, BASIC_SKILL_MELEE, 20);
                    obj_field_int32_set(actor, OBJ_F_HP_PTS, 12000);
                    obj_field_int32_set(actor, OBJ_F_HP_DAMAGE, 0);
                    critter_fatigue_pts_set(actor, 12000);
                    critter_fatigue_damage_set(actor, 0);
                }
                fprintf(stderr, "ARCMP CONTROLLED continuation: four human combat stats strengthened; original quests/items/world retained; NOT ordinary-party acceptance\n");
            }
            if (!playing && first >= 28) {
                int64_t camera = named_item(5607), parent = 0;
                if (camera) item_parent(camera, &parent);
                fprintf(stderr, "ARCMP ordinary original camera item=%lld parent=%lld parent-dead=%d x=%lld y=%lld bridge-key=%d\n", (long long)camera, (long long)parent,
                    parent && obj_type_is_critter(obj_field_int32_get(parent, OBJ_F_TYPE)) && critter_is_dead(parent),
                    camera ? (long long)location_get_x(obj_field_int64_get(camera, OBJ_F_LOCATION)) : -1,
                    camera ? (long long)location_get_y(obj_field_int64_get(camera, OBJ_F_LOCATION)) : -1, bridge_key_code());
                int owner = coop_campaign_slot_for_owner(config.identity);
                if (owner > 0 && bridge_key_owned() && !item_find_by_name(pc, 4402) && !item_get_keys(pc, NULL)) {
                    int distance = object_dist(pc, coop_campaign_object(0));
                    bool borrowed = sub_463370(pc, bridge_key_code());
                    fprintf(stderr, "ARCMP ordinary native shared-key member=%d holder=0 key=%d distance=%d expected=%d actual=%d\n", owner, bridge_key_code(), distance, distance <= 8, borrowed);
                    good &= borrowed == (distance <= 8);
                }
            }
            if (!playing) { playing = now; member = coop_campaign_slot_for_owner(config.identity); if (first == 1) { for (unsigned i = 0; i < 4; i++) initial_gold[i] = item_gold_get(coop_campaign_object(i)); } else good &= receipts(false); for (unsigned i = 0; i < 4; i++) { int64_t actor = coop_campaign_object(i); starting_hp[i] = object_hp_current(actor); starting_points[i] = stat_level_get(actor, STAT_UNSPENT_POINTS); starting_xp[i] = stat_level_get(actor, STAT_EXPERIENCE_POINTS); if (host) resources(actor, (int)i); } if (host && virgil) resources(virgil, -1); ring_initial = item_find_by_name(coop_campaign_object(0), 2804) != 0; if (first >= 49 && map_current_map() == 1 && !script_global_flag_get(1018) && script_story_state_get() < 2) { kit_stage = 0; kit_epoch = coop_runtime_epoch(); } initialized = true; }
            if (!bindings_checked && coop_gameplay_running(game)) {
                unsigned mask = 0;
                for (unsigned slot = 0; slot < 4; slot++) {
                    int bound = coop_gameplay_member_for_connection(game, slot);
                    good &= bound >= 0 && bound < 4;
                    if (bound >= 0 && bound < 4) {
                        good &= !(mask & (1u << bound)); mask |= 1u << bound;
                        if (slot == coop_session_local_slot(held)) good &= bound == member;
                        char* actual = NULL;
                        obj_field_string_get(coop_campaign_object((unsigned)bound), OBJ_F_PC_PLAYER_NAME, &actual);
                        good &= actual && !strcmp(actual, coop_campaign_member((unsigned)bound)->character);
                        fprintf(stderr, "ARCMP ordinary public binding connection=%u member=%d character=%s\n", slot, bound,
                            coop_campaign_member((unsigned)bound)->character);
                        FREE(actual);
                    }
                }
                good &= mask == 15; bindings_checked = true;
            }
            if (host && initialized && !announced && !view->active && now-playing > 1000) {
                if (exit_phase(first)) pending_phase = first; else good &= announce(first);
                announced = true;
            }
            if (host && exit_phase(pending_phase) && now >= plan_at) {
                int result = escape_destination(pending_phase); plan_at = now + 30;
                if (result < 0) good = false;
                else if (result) { good &= announce(pending_phase); pending_phase = 0; }
            }
            if (!phase && view->active && view->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (critter_is_dead(pc)) { fprintf(stderr, "ARCMP ordinary casualty member=%d phase=%u hp=%d flags=%x x=%lld y=%lld\n", member, phase, object_hp_current(pc), obj_field_int32_get(pc, OBJ_F_CRITTER_FLAGS), (long long)location_get_x(obj_field_int64_get(pc, OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(pc, OBJ_F_LOCATION))); good = false; }
            if (now >= next) {
                if (first > 23 && !gear_checked && member >= 0 && !view->active && !coop_items_busy(coop_gameplay_items(game))) {
                    int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(bought[member])), parent = 0;
                    int slot = ITEM_INV_LOC_WEAPON, status = -1;
                    if (item) { item_parent(item, &parent); status = item_check_insert(item, pc, &slot); }
                    fprintf(stderr, "ARCMP ordinary restored weapon member=%d exists=%d owned=%d equipped=%d native-insert=%d slot=%d\n", member, item != 0, parent == pc, equipped((unsigned)member), status, slot);
                    if (!item_wield_get(pc, ITEM_INV_LOC_WEAPON) && parent == pc && !status)
                        coop_runtime_item(COOP_ITEM_MOVE, item, 0, ITEM_INV_LOC_WEAPON);
                    gear_checked = true;
                }
                FILE* input = fopen("arcmp-ordinary-input.txt", "rb");
                if (input) {
                    char command[16]; unsigned expected, revision = 0, option = 0;
                    int fields = fscanf(input, "%15s %u %u %u", command, &expected, &revision, &option);
                    fclose(input); remove("arcmp-ordinary-input.txt"); next = now + 1000;
                    if (fields == 4 && !strcmp(command, "choice") && view->conversation == expected && view->revision == revision
                        && view->active && view->chooser == coop_session_local_slot(held) && option < view->option_count) key(option);
                    else if (fields == 2 && !strcmp(command, "close") && expected == phase && view->active && view->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                    else if (fields == 2 && !strcmp(command, "save") && expected == phase) coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                    else if (fields == 2 && !strcmp(command, "end") && expected == phase) coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                    else if (fields == 2 && !strcmp(command, "halt") && expected == phase) coop_runtime_halt(obj_field_int64_get(pc, OBJ_F_LOCATION));
                    else if (fields == 2 && !strcmp(command, "vote") && expected == phase && control->phase == COOP_CONTROL_VOTING)
                        coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    else if (fields == 2 && !strcmp(command, "frame") && expected == phase) image(phase, true);
                    else if (fields == 3 && !strcmp(command, "rest") && expected == phase && revision < 3)
                        coop_runtime_control(COOP_CONTROL_REST, revision, 0, 0);
                    else if (fields == 3 && !strcmp(command, "difficulty") && expected == phase && revision < 3)
                        coop_runtime_difficulty(revision);
                    else if (fields == 3 && !strcmp(command, "timing") && expected == phase && revision < 3)
                        coop_runtime_combat(COOP_COMBAT_POLICY, (uint8_t)revision, 0);
                    else if (fields == 2 && !strcmp(command, "pause") && expected == phase) coop_runtime_control(COOP_CONTROL_PAUSE, 1, 0, 0);
                    else if (fields == 2 && !strcmp(command, "resume") && expected == phase) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    else if (fields == 2 && !strcmp(command, "escape") && expected == phase) {
                        TigMessage m = { .type = TIG_MESSAGE_KEYBOARD };
                        m.data.keyboard.scancode = SDL_SCANCODE_ESCAPE; m.data.keyboard.key = SDLK_ESCAPE;
                        m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
                    }
                    else if (fields == 4 && !strcmp(command, "move") && expected == phase)
                        coop_runtime_move(location_make(revision, option), true);
                    else if (fields == 4 && !strcmp(command, "travel") && expected == phase)
                        coop_runtime_journey(location_make(revision, option));
                    else if (fields == 3 && !strcmp(command, "auto") && expected == phase && revision < 2) {
                        manual = !revision;
                        fprintf(stderr, "ARCMP ordinary manual steering=%d phase=%u\n", manual, phase);
                    }
                    else if (fields == 4 && (!strcmp(command, "use") || !strcmp(command, "force")) && expected == phase) {
                        ObjectList targets;
                        object_list_location(location_make(revision, option), OBJ_TM_PORTAL | OBJ_TM_SCENERY, &targets);
                        int64_t target = targets.head ? targets.head->obj : 0;
                        object_list_destroy(&targets);
                        fprintf(stderr, "ARCMP ordinary manual %s phase=%u x=%u y=%u target-type=%d hp=%d busted=%d\n",
                            command, phase, revision, option, target ? obj_field_int32_get(target, OBJ_F_TYPE) : -1,
                            target ? object_hp_current(target) : 0, target ? object_is_busted(target) : 0);
                        if (target && !strcmp(command, "use")) coop_runtime_world_use(target);
                        else if (target) {
                            bool stance = combat_critter_is_combat_mode_active(pc);
                            coop_runtime_combat(stance ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE,
                                stance ? COOP_ATTACK_FORCE | (COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT) : 1, stance ? target : 0);
                        }
                    }
                    else if (fields == 3 && !strcmp(command, "inspect") && expected == phase) {
                        int64_t item = named_item((int)revision), parent = 0; if (item) item_parent(item, &parent);
                        int64_t target = parent ? parent : item; Script script = { 0 };
                        if (target) obj_arrayfield_script_get(target, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
                        fprintf(stderr, "ARCMP ordinary inspect name=%u exists=%d parent-type=%d parent-name=%d script=%d locked=%d x=%lld y=%lld paused=%u\n", revision,
                            item != 0, parent ? obj_field_int32_get(parent, OBJ_F_TYPE) : -1, parent ? obj_field_int32_get(parent, OBJ_F_NAME) : -1, script.num,
                            target ? object_locked_get(target) : 0, target ? (long long)location_get_x(obj_field_int64_get(target, OBJ_F_LOCATION)) : -1,
                            target ? (long long)location_get_y(obj_field_int64_get(target, OBJ_F_LOCATION)) : -1, control->paused);
                    }
                }
            }
            if (phase && !done && !manual && now >= next) {
                /* The four ordinary owners focus the threat nearest their
                 * leader. All damage still comes from native accepted actions. */
                bool ready = false; int64_t foe = !view->active ? enemy(coop_campaign_object(0)) : 0;
                if (!foe && !view->active) foe = enemy(pc);
                /* This controlled pass continuation chooses native running
                 * instead of repeatedly duelling every roadside orc. AI,
                 * collision and incoming damage stay active. The preceding
                 * combat/crossing inputs are retained as separate evidence. */
                if (strengthened && (phase == 148 || phase == 168 || phase == 187 || phase == 189)) foe = 0;
                if (!foe && phase == 60 && !view->active && quest_state_get(pc, 1040) == QUEST_STATE_ACCEPTED) foe = saboteur();
                bool planned_dialogue = phase == 170 || phase == 205 || phase == 276 || phase == 282 || phase == 177 || phase == 116 || phase == 125 || phase == 2 || phase == 26 || phase == 31 || phase == 42 || city_dialogue(phase) || (phase >= 8 && phase <= 23);
                bool investigation = phase == 52 && (script_story_state_get() < 2 || map_current_map() == 3 || map_current_map() == 4 || map_current_map() == 5);
                bool painting_job = !mainline && phase == 52 && map_current_map() == 1 && script_story_state_get() < 2 && quest_state_get(pc, 1063) != QUEST_STATE_COMPLETED;
                int64_t scheduled = phase == 42 ? merchant : phase == 52 ? investigation ? map_current_map() == 1 && !script_global_flag_get(1018) ? npc(1059) : 0 : npc(1060) : phase == 63 ? npc(1060) : phase == 66 || phase == 80 ? npc(1047) : phase == 105 ? npc(1079) : phase == 109 ? npc(1076) : phase == 117 || phase == 126 || phase == 130 ? npc(1074) : 0;
                if (painting_job && (quest_state_get(pc, 1063) != QUEST_STATE_ACCEPTED || item_find_by_name(coop_campaign_object(0), 5627))) scheduled = npc(1291);
                bool shop_rest = ((phase == 156 || phase == 162) && game_time_is_day())
                    || ((phase == 140 || phase == 141) && map_current_map() == 1 && !game_time_is_day())
                    || (scheduled && phase != 141 && ((obj_field_int32_get(scheduled, OBJ_F_CRITTER_FLAGS) & OCF_SLEEPING)
                    || (phase < 133 && (phase >= 109 ? !game_time_is_day() : datetime_current_hour() < (phase == 42 ? 7 : 8) || datetime_current_hour() >= 20))));
                if (phase == 172) scheduled = npc(1087);
                if (phase == 192 || phase == 197) scheduled = npc((int)city_dialogue(phase));
                if (phase == 202 || phase == 209 || phase == 210) scheduled = nearest_speaker((int)city_dialogue(phase), coop_campaign_object(0));
                int64_t pelojian = phase == 241 ? nearest_speaker(1921,coop_campaign_object(0)) : 0;
                shop_rest |= phase == 241 && !area_is_known(pc, 22) && datetime_current_hour() != 0
                    && (!pelojian || (obj_field_int32_get(pelojian,OBJ_F_FLAGS)&OF_DONTDRAW));
                shop_rest |= scheduled && phase >= 166 && (obj_field_int32_get(scheduled, OBJ_F_CRITTER_FLAGS) & OCF_SLEEPING);
                unsigned recovery_status = COOP_CONTROL_OK;
                for (unsigned slot = 0; slot < 4; slot++)
                    if (coop_gameplay_member_for_connection(game, slot) == 0) recovery_status = control->statuses[slot];
                if (phase == 52 && kit_stage < 4) {
                    good &= prepare(game, pc, (unsigned)member); next = now + 700;
                } else if (phase >= 49 && host && !view->active
                    && combat_is_turn_based() != (foe != 0 && !(strengthened && phase >= 202))
                    && !coop_battle_busy(coop_gameplay_battle(game))) {
                    coop_runtime_combat(COOP_COMBAT_POLICY, foe && !(strengthened && phase >= 202) ? 2 : 0, 0); next = now + 500;
                } else if (phase == 52 && !foe && !view->active && (map_current_map() == 3 || map_current_map() == 4)
                    && !combat_turn_based_is_active() && ((recovery_needed() && !recovery_status && control->notice == COOP_CONTROL_OK) || (control->action == COOP_CONTROL_REST
                        && (control->phase != COOP_CONTROL_IDLE || control->paused)))) {
                    dungeon_recovery(pc, (unsigned)member); next = now + 600;
                } else if (phase == 52 && !foe && !view->active && map_current_map() == 1 && quest_state_get(pc, 1063) == QUEST_STATE_COMPLETED
                    && !combat_turn_based_is_active() && (camp_needed() || (control->action == COOP_CONTROL_REST
                        && (control->phase != COOP_CONTROL_IDLE || control->paused)))) {
                    camp(pc, (unsigned)member); next = now + 150;
                } else if (shop_rest && !view->active) {
                    int64_t leader = coop_campaign_object(0); bool gathered = true;
                    for (unsigned i = 1; i < 4; i++) gathered &= object_dist(leader, coop_campaign_object(i)) <= 6;
                    if (control->phase == COOP_CONTROL_IDLE && !control->paused) {
                        if (member && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        if (!member && gathered && !coop_controls_pending(coop_runtime_controls()) && shop_wait_epoch != coop_runtime_epoch()) {
                            fprintf(stderr, "ARCMP ordinary native scheduled-NPC wait phase=%u hour=%d flags=%x epoch=%u\n", phase, datetime_current_hour(), scheduled ? obj_field_int32_get(scheduled, OBJ_F_CRITTER_FLAGS) : 0, coop_runtime_epoch());
                            int hours = 24 - datetime_current_hour();
                            unsigned duration = phase == 241 ? (hours >= 4 ? 2 : hours >= 2 ? 1 : 0) : 2;
                            if (coop_runtime_control(COOP_CONTROL_REST, duration, 0, 0)) shop_wait_epoch = coop_runtime_epoch();
                        }
                    }
                    if (control->proposal != vote_proposal) { vote_proposal = control->proposal; voted = false; }
                    if (member && control->phase == COOP_CONTROL_VOTING && !voted)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    if (control->phase == COOP_CONTROL_IDLE && control->paused && host) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    next = now + 800;
                } else if (view->active && !planned_dialogue) {
                    if (view->chooser == coop_session_local_slot(held) && view->mode == 6) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                    else if (view->chooser == coop_session_local_slot(held) && view->mode == 0) {
                        int choice = dialogue_choice(view, 0);
                        if (choice < 0 || choices > 100) good = false;
                        else { fprintf(stderr, "ARCMP ordinary ambient phase=%u reply=%s option=%d text=%s\n", phase, view->reply, choice, view->options[choice]); key((unsigned)choice); choices++; }
                    }
                    next = now + 1500;
                } else if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != pc) {
                    next = now + 250;
                } else if (combat_turn_based_is_active() && withdrawing_turn == combat_turn_generation_get()) {
                    if (!coop_battle_busy(coop_gameplay_battle(game))) coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                    next = now + 500;
                } else if (foe && combat_turn_based_is_active() && object_hp_current(pc) * 3 < object_hp_max(pc)
                    && object_dist(pc, foe) < 8 && combat_action_points_get() >= 5
                    && withdraw(pc, foe, (unsigned)member)) {
                    withdrawing_turn = combat_turn_generation_get(); next = now + 600;
                } else if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() == pc
                    && (!foe || (!harm_available(pc, foe) && !combat_check_attack(pc, foe)))) {
                    if (!coop_battle_busy(coop_gameplay_battle(game))) coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                    next = now + 500;
                } else if (foe && object_hp_current(pc) * 3 <= object_hp_max(pc) && stat_base_get(pc, STAT_FATE_POINTS) > 0) {
                    coop_runtime_fate(pc, FATE_FULL_HEAL, true); next = now + 1100;
                } else if (foe && spell_is_known(pc, SPELL_MINOR_HEALING) && critter_fatigue_current(pc) > 10 && !coop_magic_busy(coop_gameplay_magic(game))
                    && object_hp_current(pc) * 2 < object_hp_max(pc)) {
                    TargetDescriptor target; target_descriptor_set_obj(&target, pc);
                    coop_runtime_cast(pc, SPELL_MINOR_HEALING, &target, false); next = now + 1100;
                } else if (harm_available(pc, foe) && !coop_magic_busy(coop_gameplay_magic(game))) {
                    TargetDescriptor target; target_descriptor_set_obj(&target, foe);
                    coop_runtime_cast(pc, SPELL_HARM, &target, true); next = now + 1100;
                } else if (foe && !coop_battle_busy(coop_gameplay_battle(game))) {
                    if (object_dist(pc, foe) > 25) walk(pc, obj_field_int64_get(foe, OBJ_F_LOCATION), (unsigned)member, 20);
                    else coop_runtime_combat(combat_critter_is_combat_mode_active(pc) ? COOP_COMBAT_ATTACK : COOP_COMBAT_STANCE,
                        combat_critter_is_combat_mode_active(pc) ? COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT : 1, combat_critter_is_combat_mode_active(pc) ? foe : 0);
                    next = now + 1000;
                } else if (!control->paused && control->phase == COOP_CONTROL_IDLE) {
                    if (painting_job) {
                        good &= painting(game, pc, (unsigned)member); next = now + 650;
                    } else if (!strengthened && investigation && !view->active && (map_current_map() == 3 || map_current_map() == 4 || quest_state_get(pc, 1063) == QUEST_STATE_COMPLETED)
                        && (stat_level_get(pc, STAT_UNSPENT_POINTS) > 0 || coop_progression_lease(coop_gameplay_progression(game)))) {
                        CoopProgression* progression = coop_gameplay_progression(game);
                        int points = stat_level_get(pc, STAT_UNSPENT_POINTS);
                        if (points > 0 || coop_progression_lease(progression)) coop_runtime_progression_editor(points > 0);
                        if (points > 0 && coop_progression_editing(progression) && !coop_progression_busy(progression)) {
                            bool heal = !spell_is_known(pc, SPELL_MINOR_HEALING);
                            coop_runtime_progression_change(pc, heal ? COOP_PROGRESS_COLLEGE : COOP_PROGRESS_STAT,
                                heal ? COLLEGE_NECROMANTIC_WHITE : STAT_STRENGTH, true);
                        }
                        next = now + 650;
                    } else if (investigation) {
                        investigate(game, pc, (unsigned)member, now); next = now + 650;
                    } else if (phase == 74) {
                        ready = script_global_flag_get(1007) && script_story_state_get() >= 5 && !view->active;
                        int64_t leader = coop_campaign_object(0);
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            /* Original BMC corridor and passage to the pillar.
                             * Coordinates guide walking only; native traps,
                             * encounters, doors and original scripts remain active. */
                            int64_t here = obj_field_int64_get(pc, OBJ_F_LOCATION);
                            int passage = location_get_x(here) > 600 ? (location_get_y(here) > 85 ? 2327 : 2331)
                                : location_get_x(here) > 300 ? 2330 : 0;
                            int64_t target = passage ? attached(passage, SAP_USE) : npc(1168);
                            int64_t goal = target ? obj_field_int64_get(target, OBJ_F_LOCATION)
                                : passage == 2327 ? location_make(694, 86) : passage == 2331 ? location_make(671, 70)
                                : passage == 2330 ? location_make(358, 67) : location_make(87, 151);
                            if (!target || object_dist(pc, target) > 5) walk(pc, goal, 0, 4);
                            else if (target == npc(1168)) coop_runtime_talk(target);
                            else coop_runtime_world_use(target);
                        }
                        next = now + 800;
                    } else if (phase == 97 || phase == 99) {
                        /* Walk from the earned world-map arrival; original tile
                         * and scenery callbacks perform every reveal/transition. */
                        ready = phase == 97 ? map_current_map() == 7 : coop_runtime_epoch() > baseline;
                        int64_t leader = coop_campaign_object(0);
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            int64_t glasses = item_find_by_name(pc, 2806);
                            if (phase == 97 && item_wield_get(pc, ITEM_INV_LOC_HELMET) != glasses) {
                                if (!glasses) good = false;
                                else coop_runtime_item(COOP_ITEM_MOVE, glasses, 0, ITEM_INV_LOC_HELMET);
                            } else if (phase == 97 && !script_global_flag_get(2095)) {
                                coop_runtime_move(location_make(61042, 42558), true);
                            } else {
                                int64_t target = attached(phase == 97 ? 1201 : 1190, SAP_USE);
                                if (!target) walk(pc, phase == 97 ? location_make(61042, 42558) : location_make(480, 223), 0, 3);
                                else if (object_dist(pc, target) > 5) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), 0, 3);
                                else coop_runtime_world_use(target);
                            }
                        }
                        next = now + 800;
                    } else if (phase == 244) {
                        int64_t leader=coop_campaign_object(0),here=obj_field_int64_get(leader,OBJ_F_LOCATION);
                        ready=map_current_map()==1 && location_dist(here,destination)<=3
                            && townmap_get(sector_id_from_loc(here))==TOWNMAP_NONE
                            && !sector_is_blocked(sector_id_from_loc(here)) && object_dist(pc,leader)<=6;
                        if (!ready && !member) walk(pc,location_get_x(here)<28445 ? location_make(28437,31171) : destination,0,0);
                        else if (!ready && member && object_dist(pc,leader)>3) walk(pc,here,(unsigned)member,3);
                        next=now+700;
                    } else if (phase == 275) {
                        int64_t leader=coop_campaign_object(0),here=obj_field_int64_get(leader,OBJ_F_LOCATION);
                        ready=coop_runtime_epoch()>baseline && map_current_map()==8
                            && location_dist(here,location_make(1077,875))<=8;
                        if (!ready && !member && map_current_map()==72) walk(pc,location_make(351,307),0,0);
                        else if (!ready && member && object_dist(pc,leader)>3) walk(pc,here,(unsigned)member,3);
                        next=now+700;
                    } else if (phase == 278 || phase == 279 || phase == 282) {
                        int64_t here = obj_field_int64_get(coop_campaign_object(0),OBJ_F_LOCATION);
                        ready = phase == 278 ? map_current_map() == 73 && location_get_x(here)<600
                            : phase == 279 ? map_current_map() == 73 && location_get_x(here)<500
                            : view->active && npc(1105) && objid_is_equal(coop_object_id_to_engine(view->npc),obj_get_id(npc(1105)));
                        if (!ready && !member && !view->active)
                            walk(pc,phase==278 ? location_make(653,93) : phase==279 ? location_make(553,106) : location_make(428,112),0,0);
                        next = now + 700;
                    } else if (later_passage(phase)) {
                        const OriginalPassage* passage = later_passage(phase);
                        int64_t leader = coop_campaign_object(0), entry = attached(passage->script, SAP_USE);
                        int64_t origin=obj_field_int64_get(leader,OBJ_F_LOCATION);
                        if (phase==273 && map_current_map()==8 && location_get_x(origin)>1000 && location_get_y(origin)>1140) {
                            /* Original tile2552 leaves the banishment ring for
                             * the cave approach. These are separate regions. */
                            if (!member) walk(pc,location_make(1037,1191),0,0);
                            else if (object_dist(pc,leader)>3) walk(pc,origin,(unsigned)member,3);
                            next=now+700; continue;
                        }
                        if (!member && phase>=247 && phase<=260 && !view->active) {
                            /* Unlocking Cantrip checks aptitude against the
                             * original lock difficulty, not a random roll.
                             * Spend earned points through the native editor
                             * when this party needs more magic aptitude. */
                            int64_t door; int iterator, difficulty=0;
                            if (obj_inst_first(&door,&iterator)) do {
                                if (obj_field_int32_get(door,OBJ_F_TYPE)==OBJ_TYPE_PORTAL
                                    && object_dist(pc,door)<=8 && object_locked_get(door)
                                    && !object_is_busted(door)
                                    && !(obj_field_int32_get(door,OBJ_F_PORTAL_FLAGS)&(OPF_JAMMED|OPF_MAGICALLY_HELD))) {
                                    int needed=obj_field_int32_get(door,OBJ_F_PORTAL_LOCK_DIFFICULTY);
                                    if (needed>difficulty) difficulty=needed;
                                }
                            } while (obj_inst_next(&door,&iterator));
                            CoopProgression* progression=coop_gameplay_progression(game);
                            bool trained=stat_level_get(pc,STAT_MAGICK_TECH_APTITUDE)>=difficulty;
                            if (!trained || coop_progression_lease(progression)) {
                                coop_runtime_progression_editor(!trained);
                                if (!trained && coop_progression_editing(progression) && !coop_progression_busy(progression)) {
                                    int college=-1;
                                    for (int c=0;c<COLLEGE_COUNT;c++)
                                        if (spell_college_level_get(pc,c)<2) { college=c; break; }
                                    if (college<0 || stat_level_get(pc,STAT_UNSPENT_POINTS)<=0) {
                                        fprintf(stderr,"ARCMP ordinary original lock requires aptitude=%d current=%d; retain checkpoint\n",difficulty,stat_level_get(pc,STAT_MAGICK_TECH_APTITUDE));
                                        good=false;
                                    } else coop_runtime_progression_change(pc,COOP_PROGRESS_COLLEGE,(unsigned)college,true);
                                }
                                next=now+700; continue;
                            }
                        }
                        ready = coop_runtime_epoch() > baseline && map_current_map() == passage->map
                            && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), location_make(passage->x, passage->y)) <= 8;
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && entry && !coop_items_busy(coop_gameplay_items(game))) {
                            if (object_dist(pc, entry) <= 5) coop_runtime_world_use(entry);
                            else walk(pc, obj_field_int64_get(entry, OBJ_F_LOCATION), 0, 4);
                        } else if (!ready && !entry && !member) {
                            /* Approach the unchanged original region until its
                             * entrance is in the native sector cache. */
                            int64_t approach = location_make(passage->approach_x,passage->approach_y);
                            if (approach != obj_field_int64_get(pc, OBJ_F_LOCATION)) walk(pc, approach, 0, 3);
                        }
                        next = now + 700;
                    } else if (phase == 219 || phase == 222 || phase == 225 || phase == 230 || phase == 265
                        || (phase >= 236 && phase <= 240)) {
                        static const int runes[][2] = {{28393,31167},{28384,31169},{28387,31174},{28387,31165},{28393,31173}};
                        int64_t leader = coop_campaign_object(0), here = obj_field_int64_get(leader, OBJ_F_LOCATION);
                        int64_t goal = phase == 219 ? location_make(29863,47345)
                            : phase == 222 ? location_make(28450,31171)
                            : phase == 225 ? location_make(436,215) : phase == 230 ? location_make(307,219)
                            : phase == 265 ? area_get_location(20) : location_make(runes[phase-236][0],runes[phase-236][1]);
                        ready = phase == 219 ? location_dist(here,goal)<=1
                                && !sector_is_blocked(sector_id_from_loc(area_get_location(20)))
                            : phase == 222 ? map_current_map() == 1 && location_get_x(here) < 28445
                            : phase == 225 ? map_current_map() == 32 && location_get_x(here) < 330
                            : phase == 230 ? map_current_map() == 32 && location_get_x(here) > 400
                            : phase >= 236 && phase <= 240 ? (phase == 240 ? script_global_flag_get(2552) : script_global_var_get(1071) >= (int)phase-235)
                            : location_dist(here, goal) <= 3;
                        if (!ready && !member) walk(pc, goal, 0, 0);
                        else if (!ready && member && object_dist(pc, leader) > 3)
                            walk(pc, here, (unsigned)member, 3);
                        next = now + 700;
                    } else if (phase == 224 || phase == 235) {
                        int64_t leader = coop_campaign_object(0);
                        int64_t target = phase == 224 ? attached(2686, SAP_USE) : original_poem();
                        unsigned type; int number; bool variant;
                        if (!member && !sent && target && !coop_items_busy(coop_gameplay_items(game))) {
                            if (phase == 224 && object_dist(pc, target) > 30) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), 0, 4);
                            else sent = phase == 224 ? coop_runtime_world_use(target) : coop_runtime_use_item(target, NULL, false);
                        }
                        if (!member && written_ui_coop_state(&type, &number, &variant)) { bought_once = true; written_ui_coop_stop(); }
                        ready = coop_engine_documents()->count >= (phase == 224 ? 3u : 4u)
                            && (phase != 224 || script_global_flag_get(2375)) && (member || (sent && bought_once));
                        if (!ready && member && !view->active && object_dist(pc, leader) > 3)
                            walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        next = now + 700;
                    } else if (phase == 232 || phase == 234 || phase == 254) {
                        int64_t leader = coop_campaign_object(0), item = phase == 234 ? original_poem() : named_item(phase == 232 ? 2848 : 5616), parent = 0;
                        if (item) item_parent(item, &parent);
                        int64_t target = parent && parent != leader ? parent : item;
                        CoopItems* items = coop_gameplay_items(game);
                        ready = item && parent == leader && (phase != 254 || script_global_flag_get(2027) && script_story_state_get() >= 19);
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!coop_items_busy(items)) {
                            if (ready && coop_items_loot_target(items)) { coop_runtime_item(COOP_LOOT_CLOSE,0,0,-1); ready = false; }
                            else if (!ready && !target) walk(pc, phase == 232 ? location_make(28380,31142) : phase == 234 ? location_make(28379,31142) : location_make(1444,2042),0,2);
                            else if (!ready && target) {
                                if (object_dist(pc,target)>3) walk(pc,obj_field_int64_get(target,OBJ_F_LOCATION),0,2);
                                else if (!parent) coop_runtime_item(COOP_ITEM_PICKUP,item,0,-1);
                                else if (coop_items_loot_target(items)!=parent) coop_runtime_item(COOP_LOOT_OPEN,parent,0,-1);
                                else coop_runtime_item(COOP_LOOT_TAKE,item,0,-1);
                            }
                        }
                        next = now + 700;
                    } else if (phase == 233) {
                        int64_t item = item_find_by_name(coop_campaign_object(0),2848);
                        ready = item && item_inventory_location_get(item) == ITEM_INV_LOC_MEDALLION;
                        if (!ready && !member && item && !coop_items_busy(coop_gameplay_items(game)))
                            coop_runtime_item(COOP_ITEM_MOVE,item,0,ITEM_INV_LOC_MEDALLION);
                        next = now + 700;
                    } else if (phase == 89 || phase == 137) {
                        /* Spend the party leader's earned chapter points through
                         * the native editor. No persuasion/IQ/charisma injection. */
                        CoopProgression* progression = coop_gameplay_progression(game);
                        int attribute = phase == 137 ? STAT_WILLPOWER : STAT_CHARISMA;
                        int intelligence = phase == 137 ? 10 : 9;
                        int skill = phase == 137 ? BASIC_SKILL_HAGGLE : BASIC_SKILL_PERSUATION;
                        bool trained = stat_level_get(pc, attribute) >= 12
                            && stat_level_get(pc, STAT_INTELLIGENCE) >= intelligence && basic_skill_level(pc, skill) >= 12;
                        ready = member != 0;
                        if (!member) {
                            if (!trained || coop_progression_lease(progression)) coop_runtime_progression_editor(!trained);
                            if (!trained && coop_progression_editing(progression) && !coop_progression_busy(progression)) {
                                if (stat_level_get(pc, STAT_UNSPENT_POINTS) <= 0) good = false;
                                else {
                                    CoopProgressionKind kind = stat_level_get(pc, attribute) < 12 || stat_level_get(pc, STAT_INTELLIGENCE) < intelligence ? COOP_PROGRESS_STAT : COOP_PROGRESS_SKILL;
                                    unsigned index = kind == COOP_PROGRESS_SKILL ? skill : stat_level_get(pc, attribute) < 12 ? attribute : STAT_INTELLIGENCE;
                                    coop_runtime_progression_change(pc, kind, index, true);
                                }
                            }
                            ready = trained && !coop_progression_editing(progression) && !coop_progression_busy(progression);
                        }
                        next = now + 650;
                    } else if (phase == 103) {
                        ready = map_current_map() == 1;
                        int64_t leader = coop_campaign_object(0);
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            int64_t here = obj_field_int64_get(pc, OBJ_F_LOCATION);
                            int passage = location_get_x(here) < 200 ? 1212 : location_get_x(here) < 500 ? 2324
                                : location_get_y(here) > 160 ? 2308 : 2304;
                            int64_t target = attached(passage, SAP_USE);
                            int64_t goal = target ? obj_field_int64_get(target, OBJ_F_LOCATION)
                                : passage == 1212 ? location_make(79, 370) : passage == 2324 ? location_make(487, 224)
                                : passage == 2308 ? location_make(638, 223) : location_make(699, 96);
                            if (!target || object_dist(pc, target) > 5) walk(pc, goal, 0, 3);
                            else coop_runtime_world_use(target);
                        }
                        next = now + 800;
                    } else if (phase == 171 || phase == 173 || phase == 179 || phase == 181 || phase == 185
                        || phase == 191 || phase == 193 || phase == 196 || phase == 199) {
                        int script = phase == 171 ? 2465 : phase == 173 ? 2467 : phase == 179 ? 1410 : phase == 181 ? 1411
                            : phase == 185 ? 2385 : phase == 191 ? 2484 : phase == 193 ? 2488 : phase == 196 ? 2491 : 2485;
                        if (phase == 173 && map_current_map() == 41
                            && location_get_x(obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION)) < 1970) script = 2425;
                        int64_t party_loc = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
                        bool burial_room = map_current_map() == 59 && location_get_y(party_loc) < 64;
                        bool temple_hall = map_current_map() == 59 && location_get_x(party_loc) >= 69
                            && location_get_y(party_loc) >= 64 && location_get_y(party_loc) < 128;
                        /* Hadrian's original route: temple exit, front grate,
                         * sewer crack, password passage and tomb hatch. The
                         * 2490 stair is in K'an Hua's disconnected chamber. */
                        if (phase == 193) script = map_current_map() == 1 ? 2499 : map_current_map() == 9 ? 2496
                            : map_current_map() == 60 ? 2494 : 2485;
                        if (phase == 196) script = map_current_map() == 1 ? 2484 : map_current_map() == 9 ? 2500
                            : map_current_map() == 60 ? 2493 : 2497;
                        int expected_map = phase == 171 ? 41 : phase == 173 || phase == 185 || phase == 199 ? 1
                            : phase == 179 ? 20 : phase == 181 ? 19 : 59;
                        int64_t leader = coop_campaign_object(0), entry = attached(script, SAP_USE);
                        ready = coop_runtime_epoch() > baseline && map_current_map() == expected_map;
                        if (phase == 193) ready &= burial_room;
                        if (phase == 196) ready &= temple_hall;
                        if ((phase == 193 || phase == 196) && map_current_map() == 60) {
                            int64_t door = attached(1149, SAP_USE);
                            if (door && !(obj_field_int32_get(door, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
                                && (object_locked_get(door) || !portal_is_open(door))) entry = door;
                        }
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            if (!entry) {
                                int64_t goal = phase == 171 ? location_make(96387, 18912) : phase == 191 ? location_make(102647, 116816)
                                    : phase == 179 ? location_make(1974, 2063) : phase == 181 ? location_make(91, 100)
                                    : phase == 185 ? location_make(2006, 2032) : location_make(124, 96);
                                walk(pc, goal, 0, 3);
                            } else if (object_dist(pc, entry) > 5) walk(pc, obj_field_int64_get(entry, OBJ_F_LOCATION), 0, 3);
                            else coop_runtime_world_use(entry);
                        }
                        next = now + 700;
                    } else if (phase == 170 || phase == 205 || phase == 276) {
                        int64_t leader = coop_campaign_object(0), guard = nearest_speaker(phase == 170 ? 1096 : phase == 205 ? 1110 : 1111, leader);
                        int64_t cove_entry = phase == 205 && !guard ? attached(2455, SAP_USE) : 0;
                        bool amulet = phase == 170 && item_find_by_name(leader, 2807) != OBJ_HANDLE_NULL;
                        ready = script_global_flag_get(phase == 170 ? 2541 : phase == 205 ? 2550 : 2718);
                        if (view->active && view->chooser == coop_session_local_slot(held) && (ready || !amulet))
                            coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                        if (member) {
                            if (!ready && !view->active && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !view->active) {
                            if (cove_entry) {
                                if (object_dist(pc, cove_entry) > 5) walk(pc, obj_field_int64_get(cove_entry, OBJ_F_LOCATION), 0, 3);
                                else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(cove_entry);
                            } else if (!guard || object_dist(pc, guard) > 25) walk(pc, guard ? obj_field_int64_get(guard, OBJ_F_LOCATION)
                                : phase == 276 ? location_make(1077,875) : area_get_location(phase == 170 ? 16 : 24), 0, 4);
                            else if (!amulet && !coop_battle_busy(coop_gameplay_battle(game))) {
                                /* Native racial/reaction greetings can refuse
                                 * conversation before the guard's amulet page.
                                 * This party chooses the original combat route,
                                 * using the same forced attack as a player. */
                                if (!combat_critter_is_combat_mode_active(pc)) coop_runtime_combat(COOP_COMBAT_STANCE, 1, 0);
                                else coop_runtime_combat(COOP_COMBAT_ATTACK, COOP_ATTACK_FORCE | (COOP_ATTACK_REPEAT << COOP_ATTACK_MODE_SHIFT), guard);
                            } else if (amulet) coop_runtime_talk(guard);
                        } else if (!ready && !member && amulet && view->chooser == coop_session_local_slot(held) && view->mode == 0) {
                            int choice = coop_campaign_probe_option(view, 1096, 25);
                            if (choice < 0) good = false; else key((unsigned)choice);
                        }
                        next = now + 700;
                    } else if (phase == 194 || phase == 195) {
                        int64_t leader = coop_campaign_object(0), skull = named_item(5615), parent = 0;
                        if (skull) item_parent(skull, &parent);
                        int64_t target = phase == 195 ? attached(1203, SAP_EXAMINE) : parent && parent != leader ? parent : skull;
                        CoopItems* items = coop_gameplay_items(game);
                        ready = phase == 195 ? script_global_flag_get(2096) : skull && parent == leader && script_global_flag_get(2016);
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!coop_items_busy(items)) {
                            if (ready && coop_items_loot_target(items)) { coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1); ready = false; }
                            else if (!ready && target) {
                                if (object_dist(pc, target) > 3) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), 0, 2);
                                else if (phase == 195) coop_runtime_world_examine(target);
                                else if (!parent) coop_runtime_item(COOP_ITEM_PICKUP, skull, 0, -1);
                                else if (coop_items_loot_target(items) != parent) coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1);
                                else coop_runtime_item(COOP_LOOT_TAKE, skull, 0, -1);
                            }
                        }
                        next = now + 700;
                    } else if (phase == 120 || phase == 127 || phase == 129 || phase == 133 || phase == 140 || phase == 142
                        || (phase == 141 && (map_current_map() == 1 || !game_time_is_day()))) {
                        int expected_map = phase == 141 ? (map_current_map() == 1 ? 23 : 1) : phase == 120 || phase == 133 || phase == 142 ? 1 : phase == 140 ? 23 : phase == 127 ? 20 : 19;
                        int passage = phase == 141 ? (map_current_map() == 1 ? 1755 : 1756) : phase == 120 || phase == 133 ? 2385 : phase == 140 ? 1755 : phase == 142 ? 1756 : phase == 127 ? 1410 : 1411;
                        int64_t leader = coop_campaign_object(0), entry = attached(passage, SAP_USE);
                        ready = map_current_map() == expected_map;
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            int64_t goal = entry ? obj_field_int64_get(entry, OBJ_F_LOCATION)
                                : passage == 1755 ? location_make(61990, 65633) : passage == 1756 ? location_make(93, 94) : phase == 120 || phase == 133 ? location_make(2006, 2032) : phase == 127 ? location_make(1974, 2063) : location_make(91, 100);
                            if (!entry || object_dist(pc, entry) > 5) walk(pc, goal, 0, 3);
                            else coop_runtime_world_use(entry);
                        }
                        next = now + 700;
                    } else if (phase == 114 || phase == 135 || phase == 148 || phase == 168 || phase == 187 || phase == 189 || phase == 177 || phase == 116 || phase == 125) {
                        int64_t leader = coop_campaign_object(0);
                        ready = phase == 114 || phase == 135 || phase == 148 || phase == 168 || phase == 187 || phase == 189 ? location_dist(obj_field_int64_get(leader, OBJ_F_LOCATION), (phase == 148 || phase == 189 ? location_make(96160, 92256) : phase == 168 ? location_make(95648, 92256) : location_make(phase == 135 || phase == 187 ? 82400 : 82784, 45600))) <= 4
                            : map_current_map() == 19;
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && (phase == 114 || phase == 135 || phase == 148 || phase == 168 || phase == 187 || phase == 189)) {
                            int64_t x = location_get_x(obj_field_int64_get(pc, OBJ_F_LOCATION));
                            int64_t goal = phase == 148 || phase == 189 ? (x < 95748 ? location_make(95750, 92288)
                                : x < 95848 ? location_make(95850, 92288)
                                : x < 95968 ? location_make(95970, 92288) : location_make(96160, 92256))
                                : phase == 168 ? (x > 95972 ? location_make(95970, 92288)
                                    : x > 95852 ? location_make(95850, 92288)
                                    : x > 95752 ? location_make(95750, 92288) : location_make(95648, 92256))
                                : location_make(phase == 135 || phase == 187 ? 82400 : 82784, 45600);
                            walk(pc, goal, 0, phase == 148 || phase == 168 || phase == 189 ? 2 : 3);
                        }
                        else if (!ready && !script_global_flag_get(2365)) {
                            /* The original guard grants entrance permission in
                             * farewell callback -7. The tree itself correctly
                             * refuses use until that conversation is complete. */
                            int64_t guard = npc(1082);
                            if (!view->active) {
                                if (!guard || object_dist(pc, guard) > 5) walk(pc, guard ? obj_field_int64_get(guard, OBJ_F_LOCATION) : location_make(98909, 42010), 0, 4);
                                else coop_runtime_talk(guard);
                            } else if (view->chooser == coop_session_local_slot(held) && view->mode == 0) {
                                int choice = coop_campaign_probe_option(view, 1082, -7);
                                if (choice < 0) good = false;
                                else key((unsigned)choice);
                            }
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            int64_t entry = attached(1643, SAP_USE);
                            if (!entry || object_dist(pc, entry) > 5) walk(pc, entry ? obj_field_int64_get(entry, OBJ_F_LOCATION) : location_make(98907, 42011), 0, 3);
                            else coop_runtime_world_use(entry);
                        }
                        next = now + 700;
                    } else if (phase == 139 || phase == 163) {
                        int64_t document = phase == 139 ? investigation_paper() : attached(1102, SAP_USE);
                        unsigned type; int number; bool variant;
                        if (!member && !sent && document && !coop_items_busy(coop_gameplay_items(game)))
                            sent = coop_runtime_use_item(document, NULL, false);
                        if (!member && written_ui_coop_state(&type, &number, &variant)) {
                            bought_once = true;
                            written_ui_coop_stop();
                        }
                        ready = coop_engine_documents()->count >= (phase == 139 ? 1u : 2u) && (member || (sent && bought_once));
                        if (phase == 163) ready &= script_story_state_get() >= 14 && area_is_known(pc, 16)
                            && quest_state_get(pc, 1020) == QUEST_STATE_COMPLETED && quest_state_get(pc, 1022) == QUEST_STATE_COMPLETED;
                        next = now + 700;
                    } else if (phase == 154 || phase == 156 || phase == 157 || phase == 162) {
                        int64_t leader = coop_campaign_object(0), book = 0, parent = 0, target = 0;
                        CoopItems* items = coop_gameplay_items(game);
                        book = attached(phase == 162 ? 1102 : 1103, phase == 162 ? SAP_USE : SAP_INSERT_ITEM);
                        if (book) item_parent(book, &parent);
                        target = phase == 154 ? attached(1236, SAP_EXAMINE) : parent && parent != leader ? parent : book;
                        Script use = { 0 }; if (target) obj_arrayfield_script_get(target, OBJ_F_SCRIPTS_IDX, SAP_USE, &use);
                        ready = phase == 154 ? script_global_flag_get(2005) && quest_state_get(pc, 1048) == QUEST_STATE_COMPLETED
                            : phase == 156 ? use.num == 2943 : book && parent == leader;
                        if (phase == 157) ready &= rumor_known_get(pc, 2002);
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!coop_items_busy(items)) {
                            if (ready && coop_items_loot_target(items)) { coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1); ready = false; }
                            else if (!ready) {
                                int64_t goal = target ? obj_field_int64_get(target, OBJ_F_LOCATION)
                                    : phase == 162 ? location_make(111937, 96847)
                                    : location_make(102685, 116827);
                                if (!target || object_dist(pc, target) > 3) walk(pc, goal, 0, 2);
                                else if (phase == 154 || (phase == 162 && !script_global_flag_get(2646))) coop_runtime_world_examine(target);
                                else if (phase == 156 || (phase == 162 && use.num != 2943)) {
                                    TargetDescriptor descriptor = { .obj = target };
                                    int64_t shovel = item_find_by_name(pc, 5614);
                                    if (shovel) coop_runtime_use_item(shovel, &descriptor, false);
                                    else good = false;
                                } else if (!parent) coop_runtime_item(COOP_ITEM_PICKUP, book, 0, -1);
                                else if (coop_items_loot_target(items) != parent) coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1);
                                else coop_runtime_item(COOP_LOOT_TAKE, book, 0, -1);
                            }
                        }
                        next = now + 700;
                    } else if (phase == 106) {
                        int64_t leader = coop_campaign_object(0), pelt = named_item(5612), parent = 0;
                        if (pelt) item_parent(pelt, &parent);
                        ready = parent == leader;
                        CoopItems* items = coop_gameplay_items(game);
                        if (member) {
                            if (!ready && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!coop_items_busy(items)) {
                            if (ready && coop_items_loot_target(items)) { coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1); ready = false; }
                            else if (!ready && pelt) {
                                int64_t target = parent ? parent : pelt;
                                if (object_dist(pc, target) > 3) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), 0, 2);
                                else if (!parent) coop_runtime_item(COOP_ITEM_PICKUP, pelt, 0, -1);
                                else if (coop_items_loot_target(items) != parent) coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1);
                                else coop_runtime_item(COOP_LOOT_TAKE, pelt, 0, -1);
                            } else if (!ready) walk(pc, location_make(62020, 65649), 0, 3);
                        }
                        next = now + 700;
                    } else if (phase == 77) {
                        ready = map_current_map() == 1;
                        int64_t leader = coop_campaign_object(0);
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && !coop_items_busy(coop_gameplay_items(game))) {
                            int64_t here = obj_field_int64_get(pc, OBJ_F_LOCATION);
                            int passage = location_get_x(here) < 300 ? 2332 : location_get_x(here) < 600 ? 2328
                                : location_get_y(here) < 85 ? 2329 : 2326;
                            int64_t target = attached(passage, SAP_USE);
                            int64_t goal = target ? obj_field_int64_get(target, OBJ_F_LOCATION)
                                : passage == 2332 ? location_make(157, 158) : passage == 2328 ? location_make(440, 67)
                                : passage == 2329 ? location_make(677, 70) : location_make(678, 125);
                            if (!target || object_dist(pc, target) > 5) walk(pc, goal, 0, 4);
                            else coop_runtime_world_use(target);
                        }
                        next = now + 800;
                    } else if (phase == 79) {
                        int map = map_current_map();
                        int64_t target = map == 13 ? npc(1047) : attached(map == 1 ? 1267 : 1266, SAP_USE);
                        int64_t goal = target ? obj_field_int64_get(target, OBJ_F_LOCATION) : location_make(61974, 65664);
                        int64_t leader = coop_campaign_object(0);
                        ready = map == 13 && target && object_dist(pc, target) <= 6;
                        if (!ready && member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready) {
                            if (!target || object_dist(pc, target) > 5 || map == 13) walk(pc, goal, 0, 4);
                            else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(target);
                        }
                        next = now + 700;
                    } else if ((phase == 65 && map_current_map() != 13) || ((phase == 69 || phase == 83) && map_current_map() != 1)) {
                        int64_t stairs = attached(phase == 65 ? map_current_map() == 1 ? 1267 : 1266 : 1267, SAP_USE);
                        int64_t leader = coop_campaign_object(0);
                        if (member) {
                            if (object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if ((phase == 69 || phase == 83) && map_current_map() == 12) {
                            /* Original first-floor map.jmp returns to Tarant. */
                            coop_runtime_move(location_make(109, 92), true);
                        } else if (!stairs) { fprintf(stderr, "ARCMP ordinary missing estate stairs phase=%u map=%d\n", phase, map_current_map()); good = false;
                        } else if (object_dist(pc, stairs) > 6) walk(pc, obj_field_int64_get(stairs, OBJ_F_LOCATION), 0, 4);
                        else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(stairs);
                        next = now + 650;
                    } else if (phase == 83 || (strengthened && (phase == 49 || phase == 69))) {
                        ready = true;
                    } else if (phase == 49 || phase == 69) {
                        CoopProgression* progression = coop_gameplay_progression(game);
                        int points = stat_level_get(pc, STAT_UNSPENT_POINTS);
                        if (points > 0 || coop_progression_lease(progression)) coop_runtime_progression_editor(points > 0);
                        if (points > 0 && coop_progression_editing(progression) && !coop_progression_busy(progression)) {
                            CoopProgressionKind kind = !spell_is_known(pc, SPELL_HARM) ? COOP_PROGRESS_COLLEGE : COOP_PROGRESS_STAT;
                            unsigned index = kind == COOP_PROGRESS_COLLEGE ? COLLEGE_NECROMANTIC_BLACK : STAT_STRENGTH;
                            fprintf(stderr, "ARCMP ordinary earned advancement member=%d xp=%d level=%d points=%d kind=%u index=%u\n", member, stat_level_get(pc, STAT_EXPERIENCE_POINTS), stat_level_get(pc, STAT_LEVEL), points, kind, index);
                            coop_runtime_progression_change(pc, kind, index, true);
                        }
                        ready = !points && !coop_progression_editing(progression) && !coop_progression_busy(progression);
                        next = now + 650;
                    } else if (phase == 59 || phase == 64) {
                        int hour = datetime_current_hour();
                        ready = phase == 59 ? (hour < 3 || script_global_flag_get(1023)) : (hour >= 8 && hour < 19);
                        if (!ready && !coop_controls_pending(coop_runtime_controls())) {
                            int remaining = phase == 59 ? 24 - hour : hour < 8 ? 8 - hour : 32 - hour;
                            unsigned duration = remaining >= 4 ? 2 : remaining >= 2 ? 1 : 0;
                            if (member == 0 && shop_wait_epoch != coop_runtime_epoch() && coop_runtime_control(COOP_CONTROL_REST, duration, 0, 0)) shop_wait_epoch = coop_runtime_epoch();
                        }
                        next = now + 600;
                    } else if (phase == 60) {
                        ready = script_global_flag_get(1023) && script_global_var_get(1011) >= 3;
                        /* Original heartbeat1169 reveals the saboteurs only
                         * after midnight and within three tiles of a PC. */
                        if (!ready && member == 0 && !script_global_flag_get(2459))
                            coop_runtime_move(location_make(61944, 65706), true);
                        next = now + 500;
                    } else if (city_dialogue(phase)) {
                        unsigned script = city_dialogue(phase);
                        if (phase==274 && map_current_map()==8 && !view->active) {
                            int64_t leader=coop_campaign_object(0),entry=attached(2642,SAP_USE);
                            if (!member && entry && !coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(entry);
                            else if (member && object_dist(pc,leader)>3) walk(pc,obj_field_int64_get(leader,OBJ_F_LOCATION),(unsigned)member,3);
                            next=now+700; continue;
                        }
                        if (phase==274 && view->active) {
                            int64_t speaker=obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
                            Script speaking={0};
                            if (speaker) obj_arrayfield_script_get(speaker,OBJ_F_SCRIPTS_IDX,SAP_DIALOG,&speaking);
                            if (speaking.num==1112 || speaking.num==2630) {
                                /* Finish the original guard's greeting before
                                 * continuing to the requested Arronax talk. */
                                if (view->chooser==coop_session_local_slot(held) && view->option_count) {
                                    unsigned choice=0;
                                    for (unsigned i=0;i<view->option_count;i++)
                                        if (contains(view->options[i],"Thank you and good day")) choice=i;
                                    key(choice);
                                }
                                next=now+700; continue;
                            }
                        }
                        /* Alexander's ordinary directions43 grant permission
                         * for the original office doors1162. Finish that page
                         * before approaching Hadrian behind those doors. */
                        if (phase == 192 && view->active && npc(1089)
                            && objid_is_equal(coop_object_id_to_engine(view->npc), obj_get_id(npc(1089)))) {
                            if (script_global_flag_get(2087)) {
                                if (view->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                                next = now + 650; continue;
                            }
                            script = 1089;
                        }
                        if (phase == 90 && !item_find_by_name(coop_campaign_object(0), 4410)) script = 1363;
                        int64_t target = phase == 138 ? nearest_speaker((int)script, coop_campaign_object(0)) : npc((int)script);
                        if (phase >= 202) target = nearest_speaker((int)script, coop_campaign_object(0));
                        bool scripted_chooser = (phase == 241 || phase == 270 || phase == 274 || phase == 283)
                            && view->active && view->chooser == coop_session_local_slot(held);
                        /* Jorian's first request clears an earlier mural
                         * reading. Return to the original mural after asking,
                         * then use the original hall jump point back to him. */
                        if (phase == 226 && !script_global_flag_get(2376)) {
                            int64_t leader = coop_campaign_object(0), here = obj_field_int64_get(leader,OBJ_F_LOCATION);
                            bool main_hall = location_get_x(here)>400;
                            if (!script_global_flag_get(2375) || main_hall) {
                                unsigned type; int number; bool variant;
                                if (!member && written_ui_coop_state(&type,&number,&variant)) written_ui_coop_stop();
                                else if (member && object_dist(pc,leader)>3) walk(pc,here,(unsigned)member,3);
                                else if (!member && !view->active) {
                                    if (!main_hall) walk(pc,location_make(307,219),0,0);
                                    else if (script_global_flag_get(2375)) walk(pc,location_make(436,215),0,0);
                                    else {
                                        int64_t mural=attached(2686,SAP_USE);
                                        if (mural && !coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(mural);
                                    }
                                }
                                next=now+700; continue;
                            }
                        }
                        if ((phase == 155 || phase == 192 || phase == 228) && !member && !view->active) {
                            CoopProgression* progression = coop_gameplay_progression(game);
                            bool trained = phase == 228 ? stat_level_get(pc, STAT_CHARISMA) >= 20
                                    && basic_skill_level(pc, BASIC_SKILL_PERSUATION) >= 20 && spell_college_level_get(pc,COLLEGE_CONVEYANCE) >= 2
                                : phase == 155 ? stat_level_get(pc, STAT_CHARISMA) >= 15
                                : script_global_flag_get(2046) || basic_skill_level(pc, BASIC_SKILL_PERSUATION) >= 16;
                            if (!trained || coop_progression_lease(progression)) {
                                coop_runtime_progression_editor(!trained);
                                if (!trained && coop_progression_editing(progression) && !coop_progression_busy(progression)) {
                                    if (stat_level_get(pc, STAT_UNSPENT_POINTS) <= 0) good = false;
                                    else if (phase == 228) {
                                        CoopProgressionKind kind = stat_level_get(pc,STAT_CHARISMA)<20 ? COOP_PROGRESS_STAT
                                            : basic_skill_level(pc,BASIC_SKILL_PERSUATION)<20 ? COOP_PROGRESS_SKILL : COOP_PROGRESS_COLLEGE;
                                        coop_runtime_progression_change(pc,kind,kind==COOP_PROGRESS_STAT ? STAT_CHARISMA
                                            : kind==COOP_PROGRESS_SKILL ? BASIC_SKILL_PERSUATION : COLLEGE_CONVEYANCE,true);
                                    } else coop_runtime_progression_change(pc, phase == 155 ? COOP_PROGRESS_STAT : COOP_PROGRESS_SKILL,
                                            phase == 155 ? STAT_CHARISMA : BASIC_SKILL_PERSUATION, true);
                                }
                                next = now + 650; continue;
                            }
                        }
                        int passage = 0;
                        if (phase == 172 && map_current_map() == 41
                            && location_get_x(obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION)) > 1970) passage = 2424;
                        if (phase == 98 && map_current_map() == 7) {
                            int64_t here = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
                            passage = location_get_y(here) < 160 ? 2307 : location_get_x(here) > 500 ? 2323 : 0;
                        }
                        ready = phase == 172 ? script_story_state_get() >= 15 && script_global_flag_get(2000) && quest_state_get(pc, 1016) == QUEST_STATE_COMPLETED
                            : phase == 178 ? script_global_flag_get(1098)
                            : phase == 180 ? script_story_state_get() >= 16 && script_global_flag_get(1088) && quest_state_get(pc, 1024) == QUEST_STATE_ACCEPTED
                            : phase == 182 ? rumor_known_get(pc, 2056)
                            : phase == 192 ? quest_state_get(pc, 1049) == QUEST_STATE_ACCEPTED && script_global_flag_get(2046)
                            : phase == 197 ? script_global_flag_get(2033) && quest_state_get(pc, 1049) == QUEST_STATE_COMPLETED
                            : phase == 198 ? script_story_state_get() >= 17 && rumor_known_get(pc, 2073)
                            : phase == 202 ? map_current_map() == 1 && area_of_object(pc) == 21 && coop_runtime_epoch() > baseline
                            : phase == 206 ? map_current_map() == 1
                                && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), area_get_location(50)) <= 32 && coop_runtime_epoch() > baseline
                            : phase == 209 ? area_is_known(pc, 20) && script_story_state_get() >= 18
                            : phase == 210 ? quest_state_get(pc, 1051) == QUEST_STATE_ACCEPTED && script_global_flag_get(2089)
                            : phase == 213 ? map_current_map() == 1 && area_of_object(pc) == 8 && coop_runtime_epoch() > baseline
                            : phase == 218 ? area_is_known(pc,11)
                            : phase == 221 ? script_global_flag_get(2527)
                            : phase == 226 ? script_global_flag_get(2376)
                            : phase == 228 ? quest_state_get(pc,1139) == QUEST_STATE_ACCEPTED
                            : phase == 241 ? area_is_known(pc,22) && quest_state_get(pc,1139) == QUEST_STATE_COMPLETED
                            : phase == 267 ? map_current_map() == 1 && area_of_object(pc) == 4 && coop_runtime_epoch() > baseline
                            : phase == 270 ? map_current_map() == 8 && script_story_state_get() >= 20 && quest_state_get(pc,1051) == QUEST_STATE_COMPLETED
                            : phase == 274 ? script_story_state_get() >= 22 && script_global_flag_get(2042) && quest_state_get(pc,1057) == QUEST_STATE_ACCEPTED
                            : phase == 283 ? false
                            : phase == 153 ? script_global_flag_get(2006) && rumor_known_get(pc, 2092)
                            : phase == 155 ? item_find_by_name(coop_campaign_object(0), 5614) != 0
                            : phase == 158 ? script_story_state_get() >= 13 && area_is_known(pc, 13)
                            : phase == 138 ? investigation_paper() != 0
                            : phase == 141 ? script_story_state_get() >= 11 && quest_state_get(pc, 1021) == QUEST_STATE_COMPLETED && quest_state_get(pc, 1022) == QUEST_STATE_ACCEPTED
                            : phase == 122 ? script_global_flag_get(1070) && quest_state_get(pc, 1019) == QUEST_STATE_COMPLETED
                            : phase == 126 ? rumor_known_get(pc, 2053)
                            : phase == 128 ? script_global_flag_get(1076) && script_global_flag_get(1081) && rumor_known_get(pc, 2057)
                            : phase == 130 ? script_story_state_get() >= 10 && quest_state_get(pc, 1020) == QUEST_STATE_ACCEPTED && quest_state_get(pc, 1021) == QUEST_STATE_ACCEPTED
                            : phase == 117 ? quest_state_get(pc, 1019) == QUEST_STATE_ACCEPTED && area_is_known(pc, 12)
                            : phase == 105 ? script_global_flag_get(1092) && item_find_by_name(coop_campaign_object(0), 4406)
                            : phase == 109 ? script_story_state_get() >= 9 && area_is_known(pc, 6) && area_is_known(pc, 17)
                                && quest_state_get(pc, 1018) == QUEST_STATE_COMPLETED
                            : phase == 98 ? script_global_flag_get(1056)
                            : phase == 100 ? script_story_state_get() >= 8 && area_is_known(pc, 10) && item_find_by_name(coop_campaign_object(0), 5203)
                            : phase == 90 ? script_global_flag_get(1036) && item_find_by_name(coop_campaign_object(0), 4419)
                            : phase == 91 ? quest_state_get(pc, 1015) == QUEST_STATE_COMPLETED && area_is_known(pc, 9)
                                && script_global_flag_get(1035) && item_find_by_name(coop_campaign_object(0), 2806)
                            : phase == 92 ? script_global_flag_get(1031) && !script_global_flag_get(1029)
                                && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), location_make(32205, 62499)) < 100
                            : phase == 80 ? quest_state_get(pc, 1013) == QUEST_STATE_COMPLETED
                                && quest_state_get(pc, 1015) == QUEST_STATE_ACCEPTED && script_global_flag_get(1008)
                            : phase == 86 ? map_current_map() == 1 && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), location_make(20691, 56027)) < 30
                            : phase == 52 ? quest_state_get(pc, 1040) == QUEST_STATE_ACCEPTED && item_find_by_name(coop_campaign_object(0), 4417)
                            : phase == 55 ? target && script_local_flag_get(target, SAP_DIALOG, 2)
                            : phase == 57 ? script_global_flag_get(2460)
                            : phase == 63 ? quest_state_get(pc, 1040) == QUEST_STATE_COMPLETED && script_global_flag_get(1024)
                            : area_is_known(pc, 7) && quest_state_get(pc, 1013) == QUEST_STATE_ACCEPTED && script_story_state_get() >= 4
                                && (!ring_initial || !item_find_by_name(coop_campaign_object(0), 2804));
                        if (ready && view->active) {
                            if (view->chooser == coop_session_local_slot(held)) {
                                /* Guard page127 grants the quest reward before
                                 * its sole farewell runs original callback62,
                                 * which unlocks the estate gates. */
                                if ((phase == 63 || phase == 98) && view->mode == 0 && view->option_count == 1) key(0);
                                else coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                            }
                            ready = false;
                        } else if (!ready && phase >= 90 && member && !scripted_chooser) {
                            int64_t leader = coop_campaign_object(0);
                            if (!view->active && object_dist(pc, leader) > 3) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                        } else if (!ready && phase == 105 && member == 0 && !script_global_flag_get(1048)) {
                            /* Look at the original giant exhibit before asking
                             * Parnell about it. This is the normal hover/examine
                             * command, not a privately supplied quest flag. */
                            int64_t exhibit = attached(1144, SAP_EXAMINE);
                            if (view->active) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                            else if (!exhibit || object_dist(pc, exhibit) > 8) walk(pc, exhibit ? obj_field_int64_get(exhibit, OBJ_F_LOCATION) : location_make(62019, 65648), 0, 5);
                            else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_examine(exhibit);
                        } else if (!ready && passage && member == 0 && !view->active) {
                            int64_t entry = attached(passage, SAP_USE);
                            int64_t goal = entry ? obj_field_int64_get(entry, OBJ_F_LOCATION)
                                : passage == 2424 ? location_make(2006, 2008) : passage == 2307 ? location_make(638, 95) : location_make(524, 224);
                            if (!entry || (passage != 2424 && object_dist(pc, entry) > 5)) walk(pc, goal, 0, 3);
                            else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(entry);
                        } else if (!ready && (phase == 86 || phase >= 90) && !target) {
                            int64_t leader = coop_campaign_object(0);
                            walk(pc, member ? obj_field_int64_get(leader, OBJ_F_LOCATION) : phase == 202 ? location_make(102715, 116958)
                                : phase == 206 ? area_get_location(24) : phase == 209 || phase == 210 ? location_make(46112, 120624)
                                : phase == 213 ? area_get_location(50) : phase == 218 ? area_get_location(20)
                                : phase == 221 ? area_get_location(11) : phase == 226 ? location_make(284,217)
                                : phase == 228 ? location_make(161,218) : phase == 241 ? location_make(28388,31169)
                                : phase == 267 ? location_make(32220,62497) : phase == 270 ? location_make(111981,96786)
                                : phase == 274 ? location_make(361,349) : phase == 283 ? location_make(428,112)
                                : phase == 90 ? location_make(20702, 55939)
                                : phase == 172 ? location_make(2040, 2038) : phase == 178 || phase == 182 ? location_make(1976, 2060) : phase == 180 ? location_make(96, 95) : phase == 192 || phase == 197 || phase == 198 ? location_make(124, 96) : phase == 155 ? location_make(102682, 116840) : phase == 153 || phase == 158 ? area_get_location(4) : phase == 138 ? location_make(62243, 65664) : phase == 141 ? location_make(101, 83) : phase == 122 ? location_make(104196, 35989) : phase == 128 ? location_make(96, 95) : phase == 117 || phase == 126 || phase == 130 ? location_make(1976, 2060) : phase == 105 ? location_make(62020, 65649) : phase == 109 ? location_make(76072, 48124) : phase == 98 ? location_make(480, 223) : phase == 100 ? location_make(82, 380) : phase == 91 ? location_make(20702, 55924) : phase == 92 ? location_make(20680, 56024) : location_make(32205, 62499), (unsigned)member, 3);
                        } else if (!ready && (member == 0 || scripted_chooser) && target) {
                            if (!view->active) {
                                if (phase == 192 && script == 1089 && object_dist(pc, target) > 5)
                                    coop_runtime_move(location_make(103, 97), true);
                                else if (phase >= 90 && object_dist(pc, target) > 5) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), 0, 4);
                                else if (!sent || now - plan_at > 4000) { sent = coop_runtime_talk(target); plan_at = now; }
                            }
                            else if (view->chooser == coop_session_local_slot(held) && view->mode == 0) {
                                int goal = phase == 274 ? 270 : phase == 283 ? 636
                                    : phase == 218 ? 80 : phase == 221 ? 33 : phase == 226 ? 130 : phase == 228 ? 40 : phase == 241 ? 25 : phase == 267 ? -26 : phase == 270 ? 40
                                    : phase == 202 ? -20 : phase == 206 ? -5 : phase == 209 ? 354 : phase == 210 ? 312 : phase == 213 ? -7
                                    : phase == 172 ? 126 : phase == 178 ? 653 : phase == 180 ? 121 : phase == 182 ? 753 : phase == 192 ? (script == 1089 ? 43 : script_global_flag_get(2046) ? 180 : 139) : phase == 197 ? 203 : phase == 198 ? 193 : phase == 153 ? 137 : phase == 155 ? 163 : phase == 158 ? 164 : phase == 141 ? 96 : phase == 122 ? 117 : phase == 126 ? 292 : phase == 128 ? (!script_global_flag_get(1076) ? 55 : !script_global_flag_get(1081) ? 75 : 85) : phase == 130 ? 476 : phase == 117 ? 222 : phase == 105 ? 482 : phase == 109 ? (quest_state_get(pc, 1043) < QUEST_STATE_ACCEPTED ? 198 : 223) : phase == 98 ? 280 : phase == 100 ? 301 : phase == 90 ? (script == 1363 ? 72 : 100) : phase == 91 ? 235 : phase == 92 ? -18 : phase == 80 ? 530 : phase == 86 ? 1900 : phase == 52 ? 57 : phase == 55 ? 24 : phase == 57 ? 20 : phase == 63 ? 127
                                    : quest_state_get(pc, 1013) == QUEST_STATE_ACCEPTED && area_is_known(pc, 7) ? 180 : 470;
                                int choice = coop_campaign_probe_option(view, (int)script, goal);
                                if (phase == 138) choice = 0;
                                if (phase == 283) {
                                    static const char* argument[] = { "Perhaps I could persuade you otherwise", "inclined to say no", "That's YOUR opinion",
                                        "I'd have to say you're correct", "Let's speak of the nature", "let's speak of the nature",
                                        "As you've said, I have only my experience", "Hold, Kerghan. That assumption",
                                        "Yes, but perhaps that is the nature", "No. Some souls are in pain",
                                        "No, but the PAIN is of their own making", "The pain is from LOSS of life",
                                        "I've spoken with souls who weren't in pain", "The distortion is caused by the eyes",
                                        "I have a solution.  I can free you", "The Vendigroth Device...it will sever",
                                        "Yes.  I can free you from the pain", "Let me end your journey, Kerghan",
                                        "Yes, I think you might be right", "Good bye, Kerghan" };
                                    bool found = false;
                                    for (unsigned n=0;n<SDL_arraysize(argument)&&!found;n++)
                                        for (unsigned i=0;i<view->option_count&&!found;i++)
                                            if (contains(view->options[i],argument[n])) { choice=(int)i; found=true; }
                                }
                                if (phase == 202 || phase == 267 || phase == 210 || phase == 226) for (unsigned i = 0; i < view->option_count; i++)
                                    if (((phase == 202 || phase == 267) && contains(view->options[i], "Isn't there a better fare"))
                                        || (phase == 226 && (contains(view->options[i], "already beheld the mural") || contains(view->options[i], "already looked at the mural")))
                                        || (phase == 210 && contains(view->options[i], "I'll do it, but can you clarify"))) choice = (int)i;
                                if (phase == 192 && script == 1090 && !script_global_flag_get(2046)) {
                                    /* The original persuasion16 route is now
                                     * earned through the native editor. Avoid
                                     * graph shortcuts through unavailable local
                                     * flags on this party's first conversation. */
                                    static const char* archaeology[] = { "Let's talk about your work", "What do you know of the remains",
                                        "Have you seen them", "Why, Hadrian", "But why won't they let", "I see your point.  I, myself",
                                        "Uh...yes.  Yes I am", "Uh...I'm thinking of converting", "What do you know, Hadrian",
                                        "But you've never seen inside", "Interesting.  I'd REALLY", "VERY much", "I'm listening",
                                        "What does it show", "That's fascinating", "And then" };
                                    for (unsigned n = 0; n < SDL_arraysize(archaeology); n++)
                                        for (unsigned i = 0; i < view->option_count; i++)
                                            if (contains(view->options[i], archaeology[n])) choice = (int)i;
                                }
                                if (phase == 192 && script_global_flag_get(2046)) for (unsigned i = 0; i < view->option_count; i++)
                                    if (contains(view->options[i], "If I find anything") || contains(view->options[i], "return when I")) choice = (int)i;
                                if (phase == 105) {
                                    /* Follow the CHA12/persuasion path actually
                                     * available to this party, through Payne's
                                     * story and the insurance bargain. */
                                    static const char* bargain[] = { "How did you come to acquire", "Epic struggle", "And then what happened",
                                        "Are you sure Payne didn't rip", "It is a bit less", "I can't believe people are still flocking",
                                        "There must be some other way", "I'll sneak it out" };
                                    for (unsigned n = 0; n < SDL_arraysize(bargain); n++)
                                        for (unsigned i = 0; i < view->option_count; i++)
                                            if (contains(view->options[i], bargain[n])) choice = (int)i;
                                }
                                if (phase == 109 && quest_state_get(pc, 1043) >= QUEST_STATE_ACCEPTED && contains(view->reply, "doing in my house")) {
                                    coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                                    next = now + 650;
                                    continue;
                                }
                                if (phase == 91) {
                                    /* The generic graph cannot predict hidden
                                     * conditions on later replies. Follow the
                                     * original earned-IQ/persuasion negotiation,
                                     * rather than the shorter haggling branch. */
                                    static const char* negotiation[] = {
                                        "That is my problem. But I still need", "Then give me the spectacles",
                                        "No, hear me out", "If I fail to take back the ship",
                                        "I'll tell your clan of your situation", "I swear. [Truth]"
                                    };
                                    for (unsigned n = 0; n < SDL_arraysize(negotiation); n++)
                                        for (unsigned i = 0; i < view->option_count; i++)
                                            if (contains(view->options[i], negotiation[n])) choice = (int)i;
                                }
                                if (phase == 117 && area_is_known(pc, 12))
                                    for (unsigned i = 0; i < view->option_count; i++)
                                        if (contains(view->options[i], "Thanks.  I'll be back when the job is done")) choice = (int)i;
                                if (phase == 122 && script_global_flag_get(1070) && view->option_count == 1) choice = 0;
                                if (choice < 0 && phase == 55) choice = coop_campaign_probe_option(view, (int)script, 50);
                                if (choice < 0) choice = dialogue_choice(view, phase == 66 || phase == 80 ? 6 : 5);
                                if (choice < 0 || choices > 100) { fprintf(stderr, "ARCMP ordinary unhandled city dialogue phase=%u reply=%s\n", phase, view->reply); good = false; }
                                else { fprintf(stderr, "ARCMP ordinary original city choice phase=%u option=%d text=%s\n", phase, choice, view->options[choice]); key((unsigned)choice); choices++; }
                            }
                        }
                        next = now + 650;
                    } else if (gather_phase(phase) || exit_phase(phase)) {
                        if (phase == 65) {
                            /* Original heartbeat1195 changes early dialogue1937
                             * to1047 once story2 is reached. Let native time run;
                             * arriving upstairs does not imply it has fired. */
                            int64_t bates = npc(1047);
                            if (!bates) bates = npc(1937);
                            if (bates) destination = obj_field_int64_get(bates, OBJ_F_LOCATION);
                        }
                        ready = location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), destination) <= (exit_phase(phase) ? 4 : 5);
                        if (phase == 65) ready &= npc(1047) != 0;
                        if (exit_phase(phase)) {
                            int64_t leader = coop_campaign_object(0), sector = sector_id_from_loc(obj_field_int64_get(leader, OBJ_F_LOCATION));
                            ready = !sector_is_blocked(sector) && townmap_get(sector) == TOWNMAP_NONE;
                            for (unsigned i = 1; i < 4; i++) ready &= object_dist(leader, coop_campaign_object(i)) <= 8;
                        }
                        if (!ready && exit_phase(phase)) {
                            int64_t leader = coop_campaign_object(0), anchor = obj_field_int64_get(leader, OBJ_F_LOCATION);
                            if (member) walk(pc, location_dist(anchor, destination) <= 4 ? destination : anchor, (unsigned)member, location_dist(anchor, destination) <= 4 ? 4 : 3);
                            else {
                                int64_t sector = sector_id_from_loc(anchor);
                                bool outside = !sector_is_blocked(sector) && townmap_get(sector) == TOWNMAP_NONE;
                                bool together = true;
                                /* On a one-file bridge, stopping the front human
                                 * at eight tiles can trap the others behind idle
                                 * companions. Keep walking to a usable exterior,
                                 * then gather at the native eight-tile boundary. */
                                for (unsigned i = 1; i < 4; i++) together &= object_dist(pc, coop_campaign_object(i)) <= 24;
                                if (!outside && together) walk(pc, destination, 0, 4);
                                else coop_runtime_halt(destination);
                            }
                        } else if (!ready) walk(pc, destination, (unsigned)member, 5);
                        next = now + 1500;
                    } else if (phase == 2 || phase == 26 || phase == 31 || phase == 42 || (phase >= 8 && phase <= 23 && (phase-8)%4 == 0)) {
                        unsigned owner = phase >= 8 && phase <= 23 ? (phase-8)/4 : 0;
                        unsigned goal = phase == 2 ? 0 : phase == 26 ? 2 : phase == 31 ? 3 : phase == 42 ? 4 : 1;
                        int64_t target = phase == 2 ? virgil : phase == 31 ? npc(1041) : merchant;
                        ready = phase == 2 ? virgil && critter_leader_get(virgil) == coop_campaign_object(0) && area_is_known(pc, 1)
                            : phase == 26 ? area_is_known(pc, 21) || quest_state_get(player_get_local_pc_obj(), 1037) >= QUEST_STATE_ACCEPTED
                            : phase == 31 ? bridge_key_owned() && item_gold_get(coop_campaign_object(0)) == 0
                            : phase == 42 ? area_is_known(pc, 21) && quest_state_get(pc, 1037) == QUEST_STATE_COMPLETED && !item_find_by_name(coop_campaign_object(0), 5607)
                            : view->active && view->mode == 3 && view->chooser == coop_session_local_slot(held) && (unsigned)member == owner;
                        if (phase >= 8 && phase <= 23 && (unsigned)member != owner) ready = view->active && view->mode == 3;
                        if (ready && (phase == 2 || phase == 26 || phase == 31 || phase == 42)) {
                            if (view->active && view->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                            ready &= !view->active;
                        } else if ((unsigned)member == owner && target) {
                            if (!view->active) { if (object_dist(pc, target) > 8) walk(pc, obj_field_int64_get(target, OBJ_F_LOCATION), (unsigned)member, 5); else coop_runtime_talk(target); }
                            else if (view->chooser == coop_session_local_slot(held) && view->mode == 0) {
                                int choice = dialogue_choice(view, goal);
                                if (choice < 0 || choices > 100) { fprintf(stderr, "ARCMP ordinary unhandled dialogue goal=%u reply=%s\n", goal, view->reply); good = false; }
                                else { fprintf(stderr, "ARCMP ordinary choice phase=%u option=%d text=%s\n", phase, choice, view->options[choice]); key((unsigned)choice); choices++; }
                            }
                        }
                        next = now + 650;
                    } else if (phase == 29 || phase == 30 || phase == 38) {
                        unsigned owner = phase == 29 ? 1 : phase == 30 && !item_gold_get(coop_campaign_object(2)) ? 3 : 2;
                        int64_t actor = coop_campaign_object(owner), recipient = coop_campaign_object(0);
                        if (phase == 38) ready = item_find_by_name(recipient, 5607) != 0;
                        else ready = item_gold_get(recipient) >= 1000 || (phase == 29 && item_gold_get(actor) == 0);
                        if (phase == 30 && !ready && owner == 3 && !item_gold_get(actor)) {
                            fprintf(stderr, "ARCMP ordinary original toll needs1000 actual-party-gold=%d\n", item_gold_get(recipient)); good = false;
                        }
                        if (!ready && (unsigned)member == owner && !coop_items_busy(coop_gameplay_items(game))) {
                            if (object_dist(actor, recipient) > 6) walk(actor, obj_field_int64_get(recipient, OBJ_F_LOCATION), owner, 3);
                            else {
                                int64_t item = phase == 38 ? item_find_by_name(actor, 5607) : item_find_first_of_type(actor, OBJ_TYPE_GOLD);
                                int amount = 1000 - item_gold_get(recipient);
                                if (amount > item_gold_get(actor)) amount = item_gold_get(actor);
                                if (item && (phase == 38 || amount > 0)) coop_runtime_item(phase == 38 ? COOP_ITEM_GIVE : COOP_ITEM_GIVE_AMOUNT, item, 0, phase == 38 ? -1 : amount);
                            }
                        }
                        next = now + 700;
                    } else if (phase == 33) {
                        int64_t gate = bridge_gate();
                        ready = gate && portal_is_open(gate);
                        if (!ready && gate && !coop_items_busy(coop_gameplay_items(game))) {
                            if (object_dist(pc, gate) > 4) walk(pc, obj_field_int64_get(gate, OBJ_F_LOCATION), (unsigned)member, 3);
                            else if (member == 1 && object_dist(pc, coop_campaign_object(0)) <= 8) coop_runtime_world_use(gate);
                        }
                        next = now + 700;
                    } else if (phase == 37) {
                        int64_t camera = named_item(5607), parent = 0;
                        if (camera) item_parent(camera, &parent);
                        ready = parent == coop_campaign_object(2);
                        if (member == 2 && !coop_items_busy(coop_gameplay_items(game))) {
                            CoopItems* items = coop_gameplay_items(game);
                            if (ready && coop_items_loot_target(items)) { coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1); ready = false; }
                            else if (!ready && camera) {
                                if (!parent) { if (object_dist(pc, camera) > 3) walk(pc, obj_field_int64_get(camera, OBJ_F_LOCATION), 2, 2); else coop_runtime_item(COOP_ITEM_PICKUP, camera, 0, -1); }
                                else if (coop_items_loot_target(items) != parent) coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1);
                                else coop_runtime_item(COOP_LOOT_TAKE, camera, 0, -1);
                            }
                        }
                        next = now + 700;
                    } else if (phase >= 8 && phase <= 23) {
                        unsigned owner = (phase-8)/4, step = (phase-8)%4;
                        if (step == 1) {
                            int64_t item = selected.bytes[0] ? obj_pool_perm_lookup(coop_object_id_to_engine(selected)) : 0;
                            if ((unsigned)member == owner && merchant) {
                                CoopServices* service = coop_gameplay_services(game); unsigned status;
                                uint32_t result = coop_services_last_result(service, &status);
                                if (!item && !sent && !coop_services_busy(service)) {
                                    item = affordable_weapon(pc, merchant, excluded, attempts);
                                    if (!item) { fprintf(stderr, "ARCMP ordinary no original stock weapon owner=%u merchant=%d gold=%d\n", owner, phase >= 12 ? 1140 : 1040, item_gold_get(pc)); good = false; }
                                    else { coop_object_id_from_engine(obj_get_id(item), &selected); quote_sequence = result + 1; coop_services_hover(service, item); sent = true; purse = item_gold_get(pc); }
                                } else if (sent && !bought_once && result == quote_sequence && !coop_services_busy(service)) {
                                    uint32_t quote = coop_services_quote(service, &fee);
                                    if (status || !quote || fee <= 0 || fee > item_gold_get(pc) - (owner == 0 ? 100 : 0)) {
                                        if (attempts == 32) good = false;
                                        else { excluded[attempts++] = selected; memset(&selected, 0, sizeof(selected)); sent = false; }
                                    } else if (coop_services_buy(service, item)) bought_once = true;
                                } else if (bought_once && result == quote_sequence + 1 && !coop_services_busy(service)) {
                                    int64_t parent = 0; if (item) item_parent(item, &parent);
                                    good &= !status && parent == pc && item_gold_get(pc) == purse - fee;
                                    if (good && !paid[owner]) {
                                        bought[owner] = selected; paid[owner] = fee; purchases++;
                                        char id[35], text[100]; for (unsigned i = 0; i < 17; i++) snprintf(id + i*2, 3, "%02x", selected.bytes[i]);
                                        snprintf(text, sizeof(text), "ordinary-purchase:%u:%s:%d", owner, id, fee); good &= coop_session_chat(held, text);
                                    }
                                }
                            }
                            if (bought[owner].bytes[0]) { int64_t taken = obj_pool_perm_lookup(coop_object_id_to_engine(bought[owner])), parent = 0; if (taken) item_parent(taken, &parent); ready = parent == coop_campaign_object(owner) && paid[owner] > 0; }
                        } else if (step == 2) {
                            if ((unsigned)member == owner && view->active) inven_ui_destroy(); ready = !view->active;
                        } else {
                            if ((unsigned)member == owner && !equipped(owner) && !coop_items_busy(coop_gameplay_items(game))) {
                                int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(bought[owner])); if (item) coop_runtime_item(COOP_ITEM_MOVE, item, 0, ITEM_INV_LOC_WEAPON);
                            }
                            ready = equipped(owner);
                        }
                        next = now + 300;
                    }
                }
                bool acquired = phase >= 11 && phase <= 23 && (phase - 11) % 4 == 0;
                bool walked_exit = last >= 48 && exit_phase(phase);
                bool walk_ready = ready;
                if (phase == 59 || phase == 64) {
                    if (control->proposal != vote_proposal) { vote_proposal = control->proposal; voted = false; }
                    if (member != 0 && control->phase == COOP_CONTROL_VOTING && !voted)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    if (host && control->phase == COOP_CONTROL_IDLE && control->paused)
                        coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                }
                bool save = walked_exit || acquired || route_save(phase) || phase == 3 || phase == 7 || phase == 24 || phase == 27, load = route_load(phase) || phase == 25;
                unsigned travel_area = journey_area(phase);
                if (save || load || travel_area) {
                    unsigned owner = travel_area ? 0 : 3;
                    bool arrived = travel_area && ((phase == 96 || phase >= 103) ? map_current_map() == 1
                        && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), (phase == 134 || phase == 186 ? area_get_location(17) : journey_destination(phase))) <= (phase == 96 ? 64 : 32)
                        || ((phase == 147 || phase == 167) && map_current_map() == 1 && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), area_get_location(18)) <= 32)
                        || (phase == 212 && map_current_map() == 1 && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), area_get_location(50)) <= 32)
                        || (phase == 269 && map_current_map() == 1 && area_of_object(pc)==13)
                        : area_of_object(pc) == (int)travel_area);
                    if (travel_area && sent && coop_runtime_epoch() > baseline && !arrived
                        && control->phase == COOP_CONTROL_IDLE && !wmap_ui_is_encounter()) {
                        /* An encounter resumes the plotted route. A native
                         * blocked-route result needs a different itinerary,
                         * not dozens of identical world drains. */
                        if (control->notice == COOP_CONTROL_LOCATION) good = false;
                        if (++journeys > 64) good = false;
                        baseline = coop_runtime_epoch(); sent = voted = false;
                    }
                    bool gathered = true;
                    if (travel_area) {
                        int64_t leader = coop_campaign_object(0);
                        /* The world-map planner cannot retrace the local cliff
                         * path to Nasrudin. Walk the accepted approach back to
                         * a usable exterior before requesting the journey. */
                        static unsigned coast_leg;
                        static const int coast[][2] = {{46158,120838},{46106,120812},{46127,120766},
                            {46136,120755},{46136,120719},{46129,120697},{46112,120624}};
                        int64_t here = obj_field_int64_get(leader,OBJ_F_LOCATION);
                        if (phase == 212 && location_get_y(here)<=120700 && !coast_leg) coast_leg=SDL_arraysize(coast);
                        while (phase == 212 && coast_leg<SDL_arraysize(coast)
                            && location_dist(here,location_make(coast[coast_leg][0],coast[coast_leg][1]))<=4) coast_leg++;
                        bool coast_exit = phase == 212 && (coast_leg<SDL_arraysize(coast)
                            || sector_is_blocked(sector_id_from_loc(here))
                            || townmap_get(sector_id_from_loc(here)) != TOWNMAP_NONE || !return_route_open(here));
                        if (coast_exit && !member && now-plan_at>4000) {
                            if (coast_leg<SDL_arraysize(coast)) coop_runtime_move(location_make(coast[coast_leg][0],coast[coast_leg][1]),true);
                            else {
                                int outside=escape_destination(212);
                                if (outside<0) good=false;
                                else if (outside) coop_runtime_move(destination,true);
                            }
                            plan_at=now;
                        }
                        gathered &= !coast_exit;
                        for (unsigned i = 1; i < 4; i++) gathered &= object_dist(leader, coop_campaign_object(i)) <= 6;
                        if (!gathered && control->phase == COOP_CONTROL_IDLE && !view->active) {
                            if (member) walk(pc, obj_field_int64_get(leader, OBJ_F_LOCATION), (unsigned)member, 3);
                            else if (!coast_exit) coop_runtime_halt(destination);
                        }
                    }
                    unsigned status; uint32_t result = coop_controls_last_result(coop_runtime_controls(), &status);
                    if (sent && result == control_sequence && status && control->phase == COOP_CONTROL_IDLE) {
                        fprintf(stderr, "ARCMP ordinary control refused phase=%u status=%u retry-after-gather\n", phase, status);
                        sent = false;
                        /* The authoritative encounter cooldown lasts four
                         * seconds and is not a replica-local UI flag. A guest
                         * can propose the next leg before the host releases it. */
                        if (status != COOP_CONTROL_DISTANCE && status != COOP_CONTROL_BUSY && status != COOP_CONTROL_ENEMIES
                            && !(travel_area && status == COOP_CONTROL_LOCATION && area_of_object(pc) == 0)) good = false;
                    }
                    if ((unsigned)member == owner && !sent && !arrived && gathered && (!walked_exit || walk_ready) && (!acquired || equipped((phase - 11) / 4)) && (phase != 47 || now - at >= 30000) && control->phase == COOP_CONTROL_IDLE && !foe && !view->active) {
                        baseline = coop_runtime_epoch();
                        control_sequence = result + 1;
                        if (phase == 71 && map_current_map() == 1
                            && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), area_get_location(7)) <= 16) {
                            /* The world marker is outside the area's map.
                             * Original scenery2325 supplies the dungeon entry. */
                            int64_t entry = attached(2325, SAP_USE);
                            if (!entry) good = false;
                            else if (object_dist(pc, entry) > 6) walk(pc, obj_field_int64_get(entry, OBJ_F_LOCATION), 0, 4);
                            else if (!coop_items_busy(coop_gameplay_items(game))) coop_runtime_world_use(entry);
                        } else if (phase == 169 || phase == 188 || phase == 190 || phase == 204 || phase == 212
                            || phase == 215 || phase == 266 || phase == 269) {
                            int64_t route[6]; unsigned count = 0;
                            if (phase == 169) {
                                route[count++] = location_make(94944, 91616); route[count++] = location_make(89888, 84128);
                                route[count++] = location_make(66976, 61216); route[count++] = area_get_location(16);
                            } else if (phase == 188) {
                                route[count++] = location_make(66976, 61216); route[count++] = location_make(89888, 84192);
                                route[count++] = location_make(92704, 89312); route[count++] = location_make(93664, 90336);
                                route[count++] = location_make(95648, 92256);
                            } else if (phase == 215 || phase == 266) {
                                /* Native routes on either side of Ashbury's
                                 * coastal barrier; each leg is plotted normally. */
                                route[count++] = location_make(35008,58016);
                                if (phase == 266) route[count++] = location_make(32992,62496);
                                route[count++] = journey_destination(phase);
                            } else if (phase == 269) {
                                route[count++] = location_make(107936,100960);
                                route[count++] = journey_destination(phase);
                            } else if (phase == 212) {
                                /* Reverse the actual outbound coast route;
                                 * the native greedy direct return is refused. */
                                route[count++] = location_make(43552,115488);
                                route[count++] = location_make(40224,102688); route[count++] = journey_destination(phase);
                            } else if (phase == 204) {
                                route[count++] = area_get_location(8); route[count++] = area_get_location(24);
                            } else {
                                route[count++] = location_make(99104, 96544); route[count++] = location_make(99296, 97248);
                                route[count++] = area_get_location(4);
                            }
                            unsigned passed = 0; int64_t distance = location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), route[count - 1]);
                            while (passed + 1 < count && distance <= location_dist(route[passed], route[count - 1]) + 8) passed++;
                            sent = coop_runtime_journey_route(route + passed, count - passed);
                        } else if (phase == 152) {
                            /* The retained post-encounter sector cannot start
                             * the native greedy southbound path. Plot its
                             * adjacent open sector before the original city;
                             * no placement or terrain restrictions are changed. */
                            int64_t route[2] = { location_make(99296, 98272), area_get_location(4) };
                            bool passed = location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), route[1]) < location_dist(route[0], route[1]);
                            sent = coop_runtime_journey_route(route + passed, passed ? 1 : 2);
                        } else if (phase == 96 || phase == 104) {
                            /* Preserve the player's plotted detour around the
                             * mountain/river rather than sending only its end. */
                            int64_t route[2] = { phase == 96 ? location_make(60256, 45280) : location_make(54496, 57952), area_get_location(travel_area) };
                            bool passed = location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), route[1]) < location_dist(route[0], route[1]);
                            sent = coop_runtime_journey_route(route + passed, passed ? 1 : 2);
                        } else sent = travel_area ? coop_runtime_journey(journey_destination(phase)) : coop_runtime_control(load ? COOP_CONTROL_LOAD : COOP_CONTROL_SAVE, 0, 0, 0);
                    }
                    if ((unsigned)member == owner && sent) control_started = true;
                    if (control->phase == COOP_CONTROL_VOTING
                        && control->action == (travel_area ? COOP_CONTROL_TRAVEL : load ? COOP_CONTROL_LOAD : COOP_CONTROL_SAVE)) {
                        control_started = true; baseline = coop_runtime_epoch();
                    }
                    if (control->proposal != vote_proposal) { vote_proposal = control->proposal; voted = false; }
                    if ((unsigned)member != owner && control->phase == COOP_CONTROL_VOTING && !voted)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                    /* Saves intentionally skip voting. All owners can
                     * observe the save owner's completed sequence. */
                    bool saved = save && control->action == COOP_CONTROL_SAVE
                        && control->sequences[save_connection]>phase_save_sequence
                        && control->statuses[save_connection]==COOP_CONTROL_OK;
                    ready = (control_started || saved) && coop_runtime_epoch() > baseline && control->phase == COOP_CONTROL_IDLE && control->notice == COOP_CONTROL_OK;
                    if (travel_area) ready &= arrived;
                    /* Later native encounters can break or drop a purchased
                     * weapon. Its historical purchase receipt is not a promise
                     * of permanent equipment; compare the actual final roster. */
                    if (phase >= 24 && phase <= 27) for (unsigned i = 0; i < 4; i++) ready &= equipped(i);
                    if (ready && host && control->paused) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    ready &= !control->paused; next = now + 500;
                }
                if (ready && (gather_phase(phase) || exit_phase(phase))) coop_runtime_halt(destination);
                if (ready) { good &= receipts(true) && image(phase, framebuffer); char text[48]; snprintf(text, sizeof(text), "ordinary-done:%u", phase); good &= coop_session_chat(held, text); done = true; completed = phase; }
            }
            if (host && phase && done && !finishing) {
                bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == phase;
                if (all && !pending_phase && !advance_sent && coop_services_settled(coop_gameplay_services(game)) && coop_dialogue_settled(coop_gameplay_dialogue(game))) {
                    advance_sent = true;
                    if (exit_phase(phase + 1)) pending_phase = phase + 1;
                    else if (phase < last) good &= announce(phase+1); else { good &= coop_gameplay_finish(game); finishing = true; }
                }
            }
            if (coop_gameplay_finished(game)) { if (!ended) ended = now; if (now-ended > 600) break; }
            if (now > progress + 15000) { progress = now; unsigned equipment = 0; for (unsigned i = 0; i < 4; i++) equipment |= (unsigned)equipped(i) << i; fprintf(stderr, "ARCMP ordinary status phase=%u member=%d epoch=%u baseline=%u area=%d mode=%u control=%u/%u gear=%x sent=%d done=%d x=%lld y=%lld gold=%d hp=%d\n", phase, member, coop_runtime_epoch(), baseline, area_of_object(pc), view->mode, control->phase, control->notice, equipment, sent, done, (long long)location_get_x(obj_field_int64_get(pc, OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(pc, OBJ_F_LOCATION)), item_gold_get(pc), object_hp_current(pc)); }
        }
        if (phase && !done && now-at > (exit_phase(phase) || gather_phase(phase) || phase == 52 || phase == 74 || phase == 77 || phase == 79 || phase >= 103 ? 900000 : 180000)) { fprintf(stderr, "ARCMP ordinary timeout phase=%u epoch=%u state=%u\n", phase, coop_runtime_epoch(), coop_runtime_phase()); good = false; }
        gamelib_ping(); iso_redraw(); tig_window_display(); TigMessage m;
        while (tig_message_dequeue(&m) == TIG_OK) { if (m.type == TIG_MESSAGE_QUIT) good = false; else if (m.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&m); }
        SDL_Delay(2);
    }
    if (last == 283) {
        good &= ending.good && ending.terminal && !ending.terminal_id && ending.slides>0
            && ending.original_device && ending.farewell && ending_reported && ending.story == 22
            && phase == 283 && (completed == 282 || first == 283) && coop_runtime_phase() == COOP_RUNTIME_OFF
            && mainmenu_ui_is_active() && !coop_campaign_active();
        FILE* report=fopen("arcmp-connected-ending-result.json","wb");
        if (!report) good=false;
        else {
            fprintf(report,"{\"verified\":%s,\"first_phase\":%u,\"phases\":283,\"epoch\":%u,\"story\":%u,\"original_device\":%s,\"original_farewell\":%s,\"shared_slides\":%u,\"all_ready_done\":%s,\"terminal_id\":%u,\"native_menu\":%s,\"party_reset\":%s}\n",
                good?"true":"false",first,ending.epoch,ending.story,ending.original_device?"true":"false",ending.farewell?"true":"false",
                ending.slides,ending.terminal?"true":"false",ending.terminal_id,mainmenu_ui_is_active()?"true":"false",coop_campaign_active()?"false":"true");
            if (fclose(report)) good=false;
        }
        coop_runtime_present_observer(NULL,NULL);
        fprintf(stderr,"ARCMP ordinary probe %s phases=283\n",good?"PASS":"FAIL");
        return good;
    }
    CoopGameplay* game = coop_runtime_gameplay();
    good &= game && coop_gameplay_finished(game) && completed == last && area_is_known(player_get_local_pc_obj(), 1);
    if (last >= 48) good &= bridge_key_owned() && area_is_known(player_get_local_pc_obj(), 21)
        && quest_state_get(player_get_local_pc_obj(), 1037) == QUEST_STATE_COMPLETED;
    if (last == 48) good &= area_of_object(player_get_local_pc_obj()) == 21;
    if (last >= 73 && last <= 76) good &= area_of_object(player_get_local_pc_obj()) == 7 && area_is_known(player_get_local_pc_obj(), 7)
        && quest_state_get(player_get_local_pc_obj(), 1040) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1013) == QUEST_STATE_ACCEPTED
        && script_global_var_get(1011) >= 3 && script_global_flag_get(1023) && script_story_state_get() >= 4;
    if (last >= 76) good &= script_global_flag_get(1007) && script_story_state_get() >= 5;
    if (last >= 82) good &= quest_state_get(player_get_local_pc_obj(), 1013) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1015) == (last >= 94 ? QUEST_STATE_COMPLETED : QUEST_STATE_ACCEPTED) && script_global_flag_get(1008);
    if (last == 88) good &= map_current_map() == 1
        && location_dist(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION), location_make(20691, 56027)) < 30;
    if (last == 94) good &= script_story_state_get() >= 7 && area_is_known(player_get_local_pc_obj(), 9)
        && script_global_flag_get(1031) && !script_global_flag_get(1029) && item_find_by_name(coop_campaign_object(0), 2806)
        && location_dist(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION), location_make(32205, 62499)) < 100;
    if (last == 102) good &= map_current_map() == 7 && script_story_state_get() >= 8
        && area_is_known(player_get_local_pc_obj(), 10) && script_global_flag_get(1056)
        && script_global_flag_get(2095) && item_find_by_name(coop_campaign_object(0), 5203)
        && quest_state_get(player_get_local_pc_obj(), 1017) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1018) == QUEST_STATE_ACCEPTED;
    if (last >= 111) good &= (map_current_map() == (last >= 281 ? 73 : last >= 272 ? 8 : (last >= 119 && last <= 132) || last == 184 ? 19 : 1)) && script_story_state_get() >= 9
        && area_is_known(player_get_local_pc_obj(), 6) && area_is_known(player_get_local_pc_obj(), 17)
        && script_global_flag_get(1092) && script_global_flag_get(2681)
        && item_find_by_name(coop_campaign_object(0), 4406)
        && quest_state_get(player_get_local_pc_obj(), 1018) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1043) == QUEST_STATE_COMPLETED;
    if (last >= 119) good &= area_is_known(player_get_local_pc_obj(), 12)
        && quest_state_get(player_get_local_pc_obj(), 1019) == (last >= 132 ? QUEST_STATE_COMPLETED : QUEST_STATE_ACCEPTED);
    if (last == 132) good &= script_story_state_get() >= 10 && script_global_flag_get(1076) && script_global_flag_get(1081)
        && rumor_known_get(player_get_local_pc_obj(), 2057)
        && quest_state_get(player_get_local_pc_obj(), 1020) == QUEST_STATE_ACCEPTED
        && quest_state_get(player_get_local_pc_obj(), 1021) == QUEST_STATE_ACCEPTED;
    if (last == 142) good &= script_story_state_get() >= 11
        && quest_state_get(player_get_local_pc_obj(), 1021) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1022) == QUEST_STATE_ACCEPTED
        && investigation_paper() && coop_engine_documents()->count >= 1;
    if (last >= 165) good &= script_story_state_get() >= 14 && area_is_known(player_get_local_pc_obj(), 13)
        && area_is_known(player_get_local_pc_obj(), 16) && item_find_by_name(coop_campaign_object(0), 5614)
        && quest_state_get(player_get_local_pc_obj(), 1020) == QUEST_STATE_COMPLETED
        && quest_state_get(player_get_local_pc_obj(), 1022) == QUEST_STATE_COMPLETED && coop_engine_documents()->count >= 2;
    if (last >= 175) good &= script_story_state_get() >= 15 && script_global_flag_get(2000)
        && quest_state_get(player_get_local_pc_obj(), 1016) == QUEST_STATE_COMPLETED;
    if (last >= 184) good &= script_story_state_get() >= 16 && script_global_flag_get(1088)
        && quest_state_get(player_get_local_pc_obj(), 1024) == (last >= 217 ? QUEST_STATE_COMPLETED : QUEST_STATE_ACCEPTED)
        && rumor_known_get(player_get_local_pc_obj(), 2056);
    if (last >= 201) good &= script_story_state_get() >= 17 && script_global_flag_get(2033) && script_global_flag_get(2096)
        && quest_state_get(player_get_local_pc_obj(), 1049) == QUEST_STATE_COMPLETED && rumor_known_get(player_get_local_pc_obj(), 2073);
    if (last >= 217) good &= script_story_state_get() >= 18
        && quest_state_get(player_get_local_pc_obj(), 1051) == (last >= 272 ? QUEST_STATE_COMPLETED : QUEST_STATE_ACCEPTED)
        && script_global_flag_get(2550) && script_global_flag_get(2089) && area_is_known(player_get_local_pc_obj(), 20);
    if (last >= 243) good &= area_is_known(player_get_local_pc_obj(),22) && quest_state_get(player_get_local_pc_obj(),1139) == QUEST_STATE_COMPLETED
        && script_global_flag_get(2552) && coop_engine_documents()->count >= 4;
    if (last >= 262) good &= script_story_state_get() >= 19 && script_global_flag_get(2027) && item_find_by_name(coop_campaign_object(0),5616);
    if (last >= 281) good &= script_story_state_get() == 22 && script_global_flag_get(2042) && script_global_flag_get(2718)
        && quest_state_get(player_get_local_pc_obj(),1057) == QUEST_STATE_ACCEPTED;
    FILE* report = fopen("arcmp-ordinary-result.json", "wb"); if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"first_phase\":%u,\"phases\":%u,\"epoch\":%u,\"member\":%d,\"original_merchants\":[1040,1140,1140,1140],\"purchases_by_local_owner\":%u,\"shrouded_hills\":%s,\"tarant_known\":%s,\"ristezze_quest\":%d,\"hp_before\":[", good ? "true" : "false", first, completed, coop_runtime_epoch(), member, purchases, area_is_known(player_get_local_pc_obj(), 1) ? "true" : "false", area_is_known(player_get_local_pc_obj(), 21) ? "true" : "false", quest_state_get(player_get_local_pc_obj(), 1037));
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", starting_hp[i]); fputs("],\"hp_after\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", object_hp_current(coop_campaign_object(i))); fputs("],\"gold_before\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", initial_gold[i]); fputs("],\"prices\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", paid[i]); fputs("],\"gold_after\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", item_gold_get(coop_campaign_object(i))); fputs("],\"weapons\":[", report);
        unsigned owned_mask = 0, equipped_mask = 0;
        for (unsigned i = 0; i < 4; i++) {
            fprintf(report, "%s\"", i ? "," : ""); for (unsigned j = 0; j < 17; j++) fprintf(report, "%02x", bought[i].bytes[j]); fputc('"', report);
            int64_t item = obj_pool_perm_lookup(coop_object_id_to_engine(bought[i])), parent = 0; if (item) item_parent(item, &parent);
            owned_mask |= (unsigned)(parent == coop_campaign_object(i)) << i;
            equipped_mask |= (unsigned)equipped(i) << i;
        }
        fprintf(report, "],\"purchased_owned_mask\":%u,\"purchased_equipped_mask\":%u,\"bridge_key_owned\":%s,\"in_tarant\":%s,\"factory_quest\":%d,\"saboteur_deaths\":%d,\"bmc_quest\":%d,\"bmc_known\":%s,\"in_bmc\":%s,\"story\":%d,\"xp_before\":[", owned_mask, equipped_mask, bridge_key_owned() ? "true" : "false", area_of_object(player_get_local_pc_obj()) == 21 ? "true" : "false", quest_state_get(player_get_local_pc_obj(), 1040), script_global_var_get(1011), quest_state_get(player_get_local_pc_obj(), 1013), area_is_known(player_get_local_pc_obj(), 7) ? "true" : "false", area_of_object(player_get_local_pc_obj()) == 7 ? "true" : "false", script_story_state_get());
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", starting_xp[i]); fputs("],\"xp_after\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", stat_level_get(coop_campaign_object(i), STAT_EXPERIENCE_POINTS)); fputs("],\"points_before\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", starting_points[i]); fputs("],\"points_after\":[", report);
        for (unsigned i = 0; i < 4; i++) fprintf(report, "%s%d", i ? "," : "", stat_level_get(coop_campaign_object(i), STAT_UNSPENT_POINTS)); fprintf(report, "],\"controlled_combat_stats\":%s,\"ordinary_party\":%s}\n", strengthened ? "true" : "false", strengthened ? "false" : "true");
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP ordinary probe %s phases=%u\n", good ? "PASS" : "FAIL", completed); return good;
}
