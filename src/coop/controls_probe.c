#include "coop/controls_probe.h"
#include "coop/gold_ui.h"
#include "coop/modern.h"
#include "coop/display.h"
#include "coop/runtime.h"
#include "coop/saves_ui.h"
#include "coop/lobby_ui.h"
#include "coop/campaign.h"
#include "coop/engine_object.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_journal.h"
#include "coop/engine_options.h"
#include "coop/engine_notes.h"
#include "coop/engine_magic.h"
#include "coop/engine_teleport.h"
#include "coop/engine_traps.h"
#include "coop/quickbar.h"
#include "game/anim.h"
#include "game/area.h"
#include "game/broadcast.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/fate.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/script.h"
#include "game/stat.h"
#include "game/spell.h"
#include "game/timeevent.h"
#include "game/tech.h"
#include "ui/schematic_ui.h"
#include "game/tf.h"
#include "game/tile.h"
#include "game/teleport.h"
#include "game/trap.h"
#include "ui/dialog_ui.h"
#include "ui/intgame.h"
#include "ui/hotkey_ui.h"
#include "ui/inven_ui.h"
#include "tig/mouse.h"
#include "ui/iso.h"
#include "ui/sleep_ui.h"
#include "ui/wmap_ui.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { CONTROL_PHASES = 18, ITEM_TAG = 187700, FOLLOWER_TAG = 187701 };
static CoopSession* held;
static bool tools_mode, qol_mode, feedback_mode;
static int bound_members[4] = { -1, -1, -1, -1 };
void coop_controls_probe_cleanup(void)
{
    if (!held) return;
    coop_runtime_detach(); coop_session_destroy(held); held = NULL;
}
static bool content(uint8_t id[32])
{
    FILE* f = fopen("arcmp-content.id", "rb"); char hex[66] = { 0 };
    if (!f) return false;
    bool good = fread(hex, 1, 65, f) >= 64; fclose(f);
    for (unsigned i = 0; good && i < 32; i++) { char pair[] = { hex[2*i], hex[2*i+1], 0 }, *end; id[i] = (uint8_t)strtoul(pair, &end, 16); good = !*end; }
    return good;
}
static int64_t pc_slot(CoopSession* session, unsigned slot)
{
    int member = coop_session_is_host(session) ? coop_campaign_slot_for_owner(coop_session_players(session)[slot].identity) : bound_members[slot];
    return member < 0 ? 0 : coop_campaign_object((unsigned)member);
}
static int64_t tagged(bool follower)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (follower ? obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == FOLLOWER_TAG
            : obj_type_is_item(obj_field_int32_get(obj, OBJ_F_TYPE)) && obj_field_int32_get(obj, OBJ_F_ITEM_WORTH) == ITEM_TAG) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static int64_t bed_object(void)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_SCENERY && obj_field_int32_get(obj, OBJ_F_HP_ADJ) == ITEM_TAG
            && !(obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_DESTROYED | OF_OFF | OF_EXTINCT))) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static bool prepare(CoopSession* session)
{
    int64_t obj, follower = 0; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        Script script = { 0 }; obj_arrayfield_script_get(obj, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) follower = obj;
        object_flags_set(obj, OF_OFF);
    } while (obj_inst_next(&obj, &it));
    int64_t actor = pc_slot(session, 1), item = 0, loc = obj_field_int64_get(actor, OBJ_F_LOCATION);
    if (!object_create(sub_4685A0(BP_SWORD), location_make(location_get_x(loc) + 6, location_get_y(loc)), &item)) return false;
    obj_field_int32_set(item, OBJ_F_ITEM_WORTH, ITEM_TAG);
    if (follower) {
        Script none = { 0 }; for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(follower, OBJ_F_SCRIPTS_IDX, sap, &none);
        obj_field_int32_set(follower, OBJ_F_NPC_PAD_I_1, FOLLOWER_TAG);
        object_flags_unset(follower, OF_OFF); if (!critter_follow(follower, pc_slot(session, 0), true)) return false;
    }
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        stat_base_set(pc, STAT_STRENGTH, 11 + (int)i);
        int gold = item_gold_get(pc);
        if ((gold && !item_gold_transfer(pc, 0, gold, 0)) || !item_gold_transfer(0, pc, 1000 + (int)i, 0)) return false;
    }
    script_global_var_set(0, 52001);
    return sub_4B6C90(true) && sub_4B6C90(false) && follower;
}
static bool gather(CoopSession* session)
{
    int64_t anchor = obj_field_int64_get(pc_slot(session, 0), OBJ_F_LOCATION);
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        sub_424070(pc, PRIORITY_3, false, false);
        sub_43E770(pc, location_make(location_get_x(anchor) + 2 * (int)(i % 2), location_get_y(anchor) + 2 * (int)(i / 2)), 0, 0);
        object_hp_damage_set(pc, 9 + (int)i); critter_fatigue_damage_set(pc, 10);
    }
    int64_t follower = tagged(true);
    if (follower) {
        if (!critter_follow(follower, pc_slot(session, 0), true)) return false;
        /* Keep automatic healing from racing the save/load health assertions. */
        for (int spell = 0; spell < SPELL_COUNT; spell++) if (spell_is_known(follower, spell)) spell_remove(follower, spell);
        sub_43E770(follower, location_make(location_get_x(anchor) - 2, location_get_y(anchor)), 0, 0); object_hp_damage_set(follower, 9);
    }
    int64_t bed = bed_object(), loc = obj_field_int64_get(pc_slot(session, 2), OBJ_F_LOCATION);
    if (bed) object_destroy(bed);
    if (!object_create(sub_4685A0(BP_BED), location_make(location_get_x(loc) - 1, location_get_y(loc) - 1), &bed)) return false;
    obj_field_int32_set(bed, OBJ_F_HP_ADJ, ITEM_TAG);
    /* Retail Bates bed script: flag 2676 is its granted-use branch. Keeping the
     * native permission script exercises the same gate as an actual paid bed. */
    Script permission = { .num = 3023 };
    obj_arrayfield_script_set(bed, OBJ_F_SCRIPTS_IDX, SAP_USE, &permission);
    script_global_flag_set(2676, true);
    return follower && bed;
}
static int64_t preference_item(unsigned slot, unsigned which)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_type_is_item(obj_field_int32_get(obj, OBJ_F_TYPE))
            && obj_field_int32_get(obj, OBJ_F_ITEM_WORTH) == ITEM_TAG + 100 + (int)(slot * 2 + which)) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static bool preference_prepare(CoopSession* session)
{
    if (!prepare(session)) return false;
    for (unsigned slot = 0; slot < 4; slot++) {
        int64_t actor = pc_slot(session, slot);
        stat_base_set(actor, STAT_FATE_POINTS, 2);
        for (unsigned which = 0; which < 2; which++) {
            int64_t item;
            if (!object_create(sub_4685A0(BP_SWORD), obj_field_int64_get(actor, OBJ_F_LOCATION), &item)) return false;
            obj_field_int32_set(item, OBJ_F_ITEM_WORTH, ITEM_TAG + 100 + (int)(slot * 2 + which));
            if (!item_transfer(item, actor)) return false;
        }
    }
    return true;
}
static bool preference_bar(CoopSession* session, unsigned slot)
{
    int64_t actor = pc_slot(session, slot), first = preference_item(slot, 0), recent = preference_item(slot, 1);
    if (!actor || !first || !recent) return false;
    hotkey_ui_coop_store();
    CoopQuickbar bar = { 0 };
    memcpy(bar.owner, coop_session_players(session)[slot].identity, 16);
    memcpy(bar.character, obj_get_id(actor).d.g.data, 16);
    CoopObjectId first_id, recent_id;
    return coop_quickbar_load(&bar) && coop_object_id_from_engine(obj_get_id(first), &first_id)
        && coop_object_id_from_engine(obj_get_id(recent), &recent_id)
        && bar.slots[0].kind == HOTKEY_ITEM && !memcmp(bar.slots[0].item.bytes, first_id.bytes, sizeof(first_id.bytes))
        && bar.slots[10].kind == HOTKEY_ITEM && !memcmp(bar.slots[10].item.bytes, recent_id.bytes, sizeof(recent_id.bytes))
        && bar.slots[11].kind == HOTKEY_ITEM && !memcmp(bar.slots[11].item.bytes, first_id.bytes, sizeof(first_id.bytes))
        && sub_57F240(0)->item_obj.obj == first && sub_57F240(0)->art_id == sub_554BE0(first)
        && intgame_recent_action_button_get(0)->art_num == tig_art_num_get(sub_554BE0(recent))
        && intgame_recent_action_button_get(1)->art_num == tig_art_num_get(sub_554BE0(first));
}
static int64_t tool_follower(unsigned slot)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && obj_field_int32_get(obj, OBJ_F_NPC_PAD_I_1) == FOLLOWER_TAG + 100 + (int)slot) return obj;
    } while (obj_inst_next(&obj, &it));
    return 0;
}
static int64_t tool_item(int prototype, unsigned slot)
{
    int64_t actor = pc_slot(held, slot);
    int count = obj_field_int32_get(actor, OBJ_F_CRITTER_INVENTORY_NUM);
    for (int i = 0; i < count; i++) {
        int64_t item = obj_arrayfield_handle_get(actor, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        if (item && sub_49B290(item) == prototype) return item;
    }
    return 0;
}
static bool tools_prepare(CoopSession* session)
{
    if (!prepare(session)) return false;
    for (unsigned slot = 0; slot < 4; slot++) {
        int64_t actor = pc_slot(session, slot), follower;
        int64_t loc = obj_field_int64_get(actor, OBJ_F_LOCATION);
        stat_base_set(actor, STAT_CHARISMA, 20);
        if (!object_create(sub_4685A0(17067), location_make(location_get_x(loc) + 3, location_get_y(loc) + 3), &follower)) return false;
        Script none = { 0 }; for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(follower, OBJ_F_SCRIPTS_IDX, sap, &none);
        obj_field_int32_set(follower, OBJ_F_NPC_PAD_I_1, FOLLOWER_TAG + 100 + (int)slot);
        if (!critter_follow(follower, actor, true)) return false;
    }
    int64_t caster = pc_slot(session, 2);
    stat_base_set(caster, STAT_LEVEL, 50); stat_base_set(caster, STAT_INTELLIGENCE, 20); stat_base_set(caster, STAT_WILLPOWER, 20);
    obj_field_int32_set(caster, OBJ_F_CRITTER_FATIGUE_PTS, 2000); critter_fatigue_damage_set(caster, 0);
    for (int spell = SPELL_DISARM; spell <= SPELL_TELEPORTATION; spell++)
        if (!spell_is_known(caster, spell) && !spell_add(caster, spell, false)) return false;
    if (!area_set_known(caster, 1)) return false;
    for (unsigned i = 0; i < 2; i++) {
        int64_t owner = pc_slot(session, i ? 0 : 3), item;
        if (!object_create(sub_4685A0(i ? BP_DYNAMITE : BP_SPIKE_TRAP), obj_field_int64_get(owner, OBJ_F_LOCATION), &item)
            || !item_transfer(item, owner)) return false;
    }
    return true;
}
static uint64_t tool_choice(void)
{
    const CoopMagicView* view = coop_engine_magic_view();
    if (view) for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++)
        if (view->choices[i].token && !memcmp(view->choices[i].actor, obj_get_id(pc_slot(held, 2)).d.g.data, 16)) return view->choices[i].token;
    return 0;
}
static bool tool_bomb_event(TimeEvent* event)
{
    return event->params[0].integer_value == 176
        && event->params[3].integer_value == coop_campaign_slot_for_object(pc_slot(held, 0)) + 1;
}
static bool tool_trap_saved(void)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj, &it)) do {
        if (obj_field_int32_get(obj, OBJ_F_TYPE) == OBJ_TYPE_TRAP && (obj_field_int32_get(obj, OBJ_F_FLAGS) & OF_TRAP_PC)
            && coop_engine_trap_owner(obj) == pc_slot(held, 3)) return timeevent_any(TIMEEVENT_TYPE_COOP_TRAPS, tool_bomb_event);
    } while (obj_inst_next(&obj, &it));
    return false;
}
static bool tool_notes_saved(void)
{
    const CoopMapNotes* notes = coop_engine_notes();
    for (unsigned slot = 0; slot < 4; slot++) {
        char text[64]; snprintf(text, sizeof(text), "Traveller %u shared annotation", slot);
        bool found = false;
        for (unsigned i = 0; i < notes->count; i++) found |= !strcmp(notes->records[i].text, text);
        if (!found) return false;
    }
    return true;
}
static void key(SDL_Scancode code, SDL_Keycode value)
{
    TigMessage m = { .type = TIG_MESSAGE_KEYBOARD }; m.data.keyboard.scancode = code; m.data.keyboard.key = value;
    m.data.keyboard.pressed = true; tig_message_enqueue(&m); m.data.keyboard.pressed = false; tig_message_enqueue(&m);
}
static void click(int x, int y)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE }; m.data.mouse.x = x; m.data.mouse.y = y;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
static bool image(unsigned phase, bool framebuffer)
{
    if (!framebuffer) return true;
    gamelib_redraw(); iso_redraw(); tig_window_display();
    char name[80]; snprintf(name, sizeof(name), "arcmp-controls-%u.bmp", phase);
    return tig_video_screenshot_save(name) == TIG_OK;
}
static bool named_browser(bool save, unsigned* step, uint64_t* next, bool framebuffer)
{
    uint64_t now = SDL_GetTicks();
    if (now < *next) return true;
    if (!*step) {
        if (!coop_saves_ui_open(save ? COOP_SAVES_SAVE : COOP_SAVES_LOAD)) return true;
    } else if (*step == 1) key(SDL_SCANCODE_END, SDLK_END);
    else if (*step == 2) key(SDL_SCANCODE_LEFT, SDLK_LEFT);
    else if (*step == 3) {
        const CoopSavePage* page = coop_save_catalog_current(coop_runtime_save_catalog());
        if (coop_save_catalog_pending(coop_runtime_save_catalog()) || !page || page->page != 15) return true;
        key(SDL_SCANCODE_4, SDLK_4);
    } else if (*step == 4 && save) {
        /* Type while an asynchronous refresh is pending. Its reply must
         * preserve the edited name and selected last manual slot. */
        click(610, 425); click(50, 390);
        for (unsigned i = 0; i < COOP_CHECKPOINT_DESCRIPTION; i++) key(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
        TigMessage text = { .type = TIG_MESSAGE_TEXT_INPUT, .data.text.text = "Four travellers - named slot63" };
        tig_message_enqueue(&text);
    } else if (*step == 5) {
        if (!image(save ? 100 : 120, framebuffer)) return false;
        key(SDL_SCANCODE_RETURN, SDLK_RETURN);
    } else if (*step == 6) {
        /* An occupied named slot needs the explicit second confirmation. */
        if (save && coop_saves_ui_active()) key(SDL_SCANCODE_RETURN, SDLK_RETURN);
    }
    (*step)++; *next = now + 350;
    return true;
}
static bool qol_prepare(CoopSession* session)
{
    if (!prepare(session) || !gather(session)) return false;
    int64_t actor = pc_slot(session, 1);
    stat_base_set(actor, STAT_INTELLIGENCE, 20);
    obj_arrayfield_uint32_set(actor, OBJ_F_CRITTER_SPELL_TECH_IDX, 17 + TECH_GUN, 1);
    SchematicInfo recipe;
    schematic_ui_info_get(tech_schematic_get(TECH_GUN, 1), &recipe);
    int components[] = { recipe.item1[0], recipe.item2[0] };
    for (unsigned i = 0; i < 2; i++) {
        int64_t item;
        if (!object_create(sub_4685A0(components[i]), obj_field_int64_get(actor, OBJ_F_LOCATION), &item)
            || !item_transfer(item, actor)) return false;
    }
    return coop_play_pref_set(PLAY_PREF_AUTOSAVE_MINUTES, 2) && coop_play_pref_set(PLAY_PREF_AUTOSAVE_TRAVEL, 1);
}
static bool qol_pistol(CoopSession* session)
{
    static int prototype;
    if (!prototype) {
        SchematicInfo recipe;
        schematic_ui_info_get(tech_schematic_get(TECH_GUN, 1), &recipe);
        prototype = recipe.prod[0];
    }
    return sub_462540(pc_slot(session, 1), sub_4685A0(prototype), 7) != 0;
}
static bool announce(CoopSession* session, unsigned phase)
{
    if (phase == 1 || phase == 8) for (unsigned slot = 0; slot < 4; slot++) {
        int member = coop_campaign_slot_for_owner(coop_session_players(session)[slot].identity);
        char text[64]; snprintf(text, sizeof(text), "control-bind:%u:%d", slot, member);
        if (member < 0 || !coop_session_chat(session, text)) return false;
    }
    if (phase == 8 && !tools_mode && !qol_mode && !gather(session)) return false;
    if (phase == 11) {
        script_global_var_set(0, 52002);
        for (unsigned i = 0; i < 4; i++) { item_gold_transfer(0, coop_campaign_object(i), 17, 0); object_hp_damage_set(coop_campaign_object(i), 12); }
    }
    char text[64]; snprintf(text, sizeof(text), "control-phase:%u", phase); return coop_session_chat(session, text);
}
bool coop_controls_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false, recovery = false, tail = false, preview = false, native_ui = false, preferences = false; unsigned port = 35742, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-recover")) recovery = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-recover-ui")) recovery = preview = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-controls-tail")) tail = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-controls-ui")) native_ui = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-preferences")) preferences = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-native-tools")) tools_mode = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-party-qol")) qol_mode = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-play-feedback")) feedback_mode = qol_mode = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (!port || port > 65535 || player >= 4 || host != (player == 0)) return false;
    unsigned last_phase = feedback_mode ? 15u : qol_mode ? 9u : tools_mode ? 10u : preferences ? 4u : recovery ? 1u : CONTROL_PHASES;
    CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Control player %u", player);
    if (!content(config.content_id) || !coop_identity_load_or_create("arcmp-player.id", config.identity)) return false;
    held = coop_session_create(&config); if (!held) return false;
    uint64_t start = SDL_GetTicks(), playing = 0, at = 0, chats = 0, ended = 0, freeze = 0;
    bool good = (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port); fflush(stderr);
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, baseline_epoch = 0, expected = 0, rest_hours = 0, saved_hp[4] = { 0 };
    bool rest_story = getenv("ARCMP_PROBE_REST_STORY") != NULL, story_queued = false, story_seen = false;
    unsigned story_observers = 0;
    bool selected = false, initialized = false, sent = false, done = false, voted = false, picker_clicked = false, paused_native = false, cancelled = false, replayed = false, finishing = false, guards = false;
    CoopControlRequest replay = { 0 };
    unsigned browser_step = 0;
    uint64_t browser_next = 0;
    while (good && SDL_GetTicks() - start < 180000) {
        uint64_t now = SDL_GetTicks(); tig_ping(); coop_session_poll(held, now); coop_lobby_tick();
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) { fprintf(stderr, "ARCMP controls runtime: %s\n", coop_runtime_status()); good = false; break; }
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name, sizeof(name), "Control hero %u", player); selected = coop_session_character(held, 16067 + player, name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held, true);
            unsigned count = 0; for (unsigned i = 0; i < 4; i++) count += coop_session_players(held)[i].connected;
            if (host && count == 4 && coop_session_all_ready(held)) good = tail ? coop_runtime_recover_checkpoint() : recovery ? coop_runtime_start_checkpoint() : coop_runtime_start();
        }
        CoopControls* controls = coop_runtime_controls(); const CoopControlView* v = coop_controls_current(controls);
        CoopGameplay* game = coop_runtime_gameplay();
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing) playing = now;
            const CoopDialogueView* dialogue = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!initialized && host && !dialogue->active && now - playing > 1200) { initialized = true; good = (qol_mode ? qol_prepare(held) : tools_mode ? tools_prepare(held) : preferences ? preference_prepare(held) : recovery || (tail && !native_ui) || prepare(held)) && announce(held, tail ? 8 : 1); }
            if (!phase && dialogue->active && dialogue->chooser == coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
        }
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats); unsigned value;
            unsigned connection, member;
            if (!chat->slot && sscanf(chat->text, "control-bind:%u:%u", &connection, &member) == 2) {
                if (connection >= 4 || member >= 4) { good = false; break; }
                bound_members[connection] = (int)member;
            }
            if (!chat->slot && sscanf(chat->text, "control-phase:%u", &value) == 1) {
                if (value != (!phase && tail ? 8 : phase + 1) || value > CONTROL_PHASES) { good = false; break; }
                phase = value; at = now; sent = done = voted = picker_clicked = cancelled = replayed = false; freeze = 0;
                browser_step = 0; browser_next = 0;
                baseline_epoch = coop_runtime_epoch(); unsigned status; expected = coop_controls_last_result(controls, &status) + 1;
                fprintf(stderr, "ARCMP controls phase=%u slot=%u epoch=%u\n", phase, coop_session_local_slot(held), baseline_epoch);
            }
            if (chat->slot < 4 && sscanf(chat->text, "control-done:%u", &value) == 1) ack[chat->slot] = value;
            if (chat->slot < 4 && !strcmp(chat->text, "control-story-seen")) story_observers |= 1u << chat->slot;
            if (qol_mode && chat->slot == 1 && !strcmp(chat->text, "qol-schematic-open")) story_queued = true;
            if (qol_mode && !chat->slot && !strcmp(chat->text, "qol-autosave-done")) story_seen = true;
        }
        uint8_t slot = coop_session_local_slot(held);
        bool live = game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && v && v->phase == COOP_CONTROL_IDLE;
        if (phase && !done && now - at > 400) {
            bool ready = live; unsigned status = 255; uint32_t result = coop_controls_last_result(controls, &status);
            int64_t item = tagged(false), parent = 0; if (item) item_parent(item, &parent);
            bool preferences_live = game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && v
                && (v->phase == COOP_CONTROL_IDLE || (phase == 4 && v->phase == COOP_CONTROL_VOTING));
            if (qol_mode) {
                int64_t actor = player_get_local_pc_obj();
                ready = game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && v
                    && (v->phase == COOP_CONTROL_IDLE || v->phase == COOP_CONTROL_VOTING);
                if (phase == 1) {
                    ready &= coop_play_autosave_minutes() == 2 && coop_play_autosave_travel();
                    if (slot && !sent) {
                        good &= !coop_play_pref_set(PLAY_PREF_AUTOSAVE_MINUTES, 30)
                            && !coop_play_pref_set(PLAY_PREF_AUTOSAVE_TRAVEL, 0);
                        sent = true;
                    }
                } else if (phase == 2) {
                    if (slot == 1 && !sent && live) {
                        good &= coop_gold_ui_open();
                        unsigned row = 0, member = (unsigned)coop_campaign_slot_for_object(pc_slot(held, 0));
                        for (unsigned i = 0; i < member; i++) if (coop_campaign_object(i) != actor) row++;
                        click(230, 112 + 104 + (int)row * 28);
                        TigMessage input = { .type = TIG_MESSAGE_TEXT_INPUT, .data.text.text = "137" };
                        tig_message_enqueue(&input); click(260, 450); sent = true;
                    }
                    ready &= item_gold_get(pc_slot(held, 0)) == 1137 && item_gold_get(pc_slot(held, 1)) == 864
                        && coop_items_settled(coop_gameplay_items(game));
                    if (ready && slot == 1) { good &= image(402, framebuffer); coop_gold_ui_close(); }
                } else if (phase == 3) {
                    if (slot == 1 && !sent && live) {
                        coop_view_zoom_set(150, 400, 240);
                        schematic_ui_toggle(actor, actor);
                        good &= intgame_mode_get() == INTGAME_MODE_SCHEMATIC
                            && schematic_ui_process(tech_schematic_get(TECH_GUN, 1), actor, actor);
                        sent = true;
                    }
                    ready &= qol_pistol(held) && coop_services_settled(coop_gameplay_services(game));
                    if (ready && slot == 1) { schematic_ui_close(); good &= intgame_mode_get() == INTGAME_MODE_MAIN && image(403, framebuffer); }
                } else if (phase == 4) {
                    if (slot == 1 && !sent) {
                        schematic_ui_toggle(actor, actor);
                        good &= intgame_mode_get() == INTGAME_MODE_SCHEMATIC && coop_session_chat(held, "qol-schematic-open");
                        sent = true;
                    }
                    if (host && story_queued && !sent && live && coop_runtime_probe_autosave(true)) {
                        CoopSaveSet party; CoopSaveEntry entry;
                        good &= coop_runtime_save_set(&party);
                        coop_save_inspect(&party, COOP_SAVE_AUTOSAVE_FIRST, &entry);
                        good &= entry.status == COOP_CHECKPOINT_OK && !coop_runtime_checkpoint_exists();
                        good &= coop_session_chat(held, "qol-autosave-done"); sent = true;
                    }
                    ready &= story_seen && coop_runtime_epoch() == baseline_epoch;
                    if (ready && slot == 1) {
                        good &= intgame_mode_get() == INTGAME_MODE_SCHEMATIC;
                        schematic_ui_close(); good &= image(404, framebuffer);
                    }
                } else if (phase == 5) {
                    if (slot == 3 && !sent && live) { good &= named_browser(true, &browser_step, &browser_next, framebuffer); sent = browser_step >= 7; }
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && v->notice == COOP_CONTROL_OK && v->action == COOP_CONTROL_SAVE;
                } else if (phase == 6) {
                    if (host && !sent) { script_global_var_set(0, 52002); sent = coop_runtime_item(COOP_ITEM_GOLD, 0, (uint8_t)coop_campaign_slot_for_object(pc_slot(held, 2)), 100); }
                    ready &= script_global_var_get(0) == 52002 && item_gold_get(pc_slot(held, 0)) == 1037
                        && item_gold_get(pc_slot(held, 2)) == 1102 && coop_items_settled(coop_gameplay_items(game));
                } else if (phase<=9) {
                    if (phase < 9 && slot == 3 && live && browser_step < 7) {
                        if (!browser_step) { coop_lobby_open(); browser_step = 1; browser_next = now + 350; }
                        else if (browser_step == 1 && now >= browser_next) {
                            click(340, 565); browser_step = 2; browser_next = now + 350;
                        } else if (browser_step >= 2 && now >= browser_next) {
                            good &= coop_saves_ui_active();
                            unsigned step = browser_step - 1;
                            good &= named_browser(false, &step, &browser_next, framebuffer); browser_step = step + 1;
                        }
                        sent = browser_step >= 7;
                    }
                    if (phase == 9 && host && !sent && live) {
                        CoopSaveSet party; CoopSaveEntry entry;
                        good &= coop_runtime_save_set(&party); coop_save_inspect(&party, COOP_SAVE_AUTOSAVE_FIRST, &entry);
                        good &= coop_runtime_checkpoint_request(COOP_CONTROL_LOAD, &entry, NULL); sent = true;
                    }
                    if (v->phase == COOP_CONTROL_VOTING && !voted && now - at > 1500) {
                        if (phase == 7 && slot == 2) voted = coop_runtime_control(COOP_CONTROL_VOTE, 0, v->proposal, 0);
                        else if (phase != 7 && slot != (phase == 9 ? 0u : 3u)) voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, v->proposal, 0);
                    }
                    ready &= v->action == COOP_CONTROL_LOAD && v->notice == (phase == 7 ? COOP_CONTROL_DECLINED : COOP_CONTROL_OK);
                    ready &= coop_runtime_epoch() == baseline_epoch + (phase != 7);
                    ready &= script_global_var_get(0) == (phase == 7 ? 52002 : 52001);
                    ready &= item_gold_get(pc_slot(held, 0)) == (phase == 7 ? 1037 : 1137)
                        && item_gold_get(pc_slot(held, 1)) == 864 && qol_pistol(held);
                    if (ready) good &= image(400 + phase, framebuffer);
                } else if (phase==10) {
                    if (host && !sent && live) {
                        script_global_var_set(0,52003);
                        TeleportData trip={.obj=actor,.map=79,.loc=location_make(2087,2018),.flags=TELEPORT_COOP_PARTY};
                        sent=teleport_do(&trip); good &= sent;
                    }
                    ready &= coop_runtime_epoch()==baseline_epoch+1 && map_current_map()==79;
                    if (ready && host) {
                        CoopSaveSet party; CoopSaveEntry entry;
                        good &= coop_runtime_save_set(&party); coop_save_inspect(&party,COOP_SAVE_AUTOSAVE_FIRST+1,&entry);
                        good &= entry.status==COOP_CHECKPOINT_OK && entry.info.map==1
                            && !strncmp(entry.info.description,"Before travel autosave",sizeof("Before travel autosave")-1);
                    }
                } else if (phase==11) {
                    if (host && !sent && live) {
                        CoopSaveSet party; CoopSaveEntry entry;
                        good &= coop_runtime_save_set(&party); coop_save_inspect(&party,COOP_SAVE_AUTOSAVE_FIRST+1,&entry);
                        sent=coop_runtime_checkpoint_request(COOP_CONTROL_LOAD,&entry,NULL); good &= sent;
                    }
                    if (slot && !voted && v->phase==COOP_CONTROL_VOTING) voted=coop_runtime_control(COOP_CONTROL_VOTE,1,v->proposal,0);
                    ready &= coop_runtime_epoch()==baseline_epoch+1 && map_current_map()==1 && script_global_var_get(0)==52003;
                } else if (phase==12) {
                    if (host && !sent && live) {
                        good &= coop_play_pref_set(PLAY_PREF_TURN_RADIUS,5) && coop_play_pref_set(PLAY_PREF_AUTOPAUSE,PLAY_PAUSE_ENEMY);
                        int64_t follower=tagged(true),origin=obj_field_int64_get(actor,OBJ_F_LOCATION);
                        sub_43E770(follower,location_make(location_get_x(origin)+30,location_get_y(origin)),0,0);
                        good &= !coop_play_turn_trigger(follower,"Enemy spotted") && !combat_turn_based_is_active() && !coop_runtime_paused();
                        sub_43E770(follower,location_make(location_get_x(origin)+2,location_get_y(origin)),0,0);
                        good &= coop_play_turn_trigger(follower,"Enemy spotted"); sent=true;
                    }
                    ready &= combat_turn_based_is_active() && !v->paused && !coop_lobby_is_open() && coop_play_turn_radius()==5;
                    if (ready) good &= image(412,framebuffer);
                } else if (phase==13) {
                    if (host && !sent && live) {
                        good &= sub_4B6C90(false) && coop_play_pref_set(PLAY_PREF_AUTOPAUSE,0);
                        for (unsigned i=0;i<4;i++) {
                            int64_t target=pc_slot(held,i);
                            obj_field_int32_set(target,OBJ_F_CRITTER_FLAGS,obj_field_int32_get(target,OBJ_F_CRITTER_FLAGS)|OCF_STUNNED);
                            stat_base_set(target,STAT_POISON_LEVEL,15);
                        }
                        int64_t npc=tagged(true);
                        obj_field_int32_set(npc,OBJ_F_SPELL_FLAGS,obj_field_int32_get(npc,OBJ_F_SPELL_FLAGS)|OSF_INVISIBLE);
                        sent=true;
                    }
                    ready &= !combat_is_turn_based() && !v->paused && !coop_lobby_is_open();
                    for (unsigned i=0;i<4;i++) ready &= (obj_field_int32_get(pc_slot(held,i),OBJ_F_CRITTER_FLAGS)&OCF_STUNNED) && tf_has_entries(pc_slot(held,i));
                    ready &= tf_has_entries(tagged(true));
                    if (ready) { good &= coop_play_pref_set(PLAY_PREF_CRIT_SHAKE,slot==1); good &= image(413,framebuffer); }
                } else if (phase==14) {
                    if (host && !sent && live) {
                        for (unsigned i=0;i<4;i++) {
                            int64_t target=pc_slot(held,i);
                            obj_field_int32_set(target,OBJ_F_CRITTER_FLAGS,obj_field_int32_get(target,OBJ_F_CRITTER_FLAGS)&~OCF_STUNNED);
                            stat_base_set(target,STAT_POISON_LEVEL,0);
                            CombatContext hit={.flags=CF_HIT|CF_CRITICAL,.target_obj=target,.dam_flags=CDF_HAVE_DAMAGE|CDF_IGNORE_RESISTANCE};
                            hit.dam[DAMAGE_TYPE_NORMAL]=1; combat_dmg(&hit);
                        }
                        sent=true;
                    }
                    if (coop_view_shaking()) picker_clicked=true;
                    ready &= !(obj_field_int32_get(actor,OBJ_F_CRITTER_FLAGS)&OCF_STUNNED) && coop_items_settled(coop_gameplay_items(game));
                    ready &= slot==1 ? picker_clicked : !coop_view_shaking();
                    if (ready) good &= image(414,framebuffer);
                } else if (phase==15) {
                    if (!sent) { coop_lobby_open(); sent=true; }
                    if (!picker_clicked && now-at>800) { click(340,370); picker_clicked=true; }
                    if (!cancelled && now-at>1200) { good &= coop_saves_ui_active() && image(415,framebuffer); key(SDL_SCANCODE_ESCAPE,SDLK_ESCAPE); cancelled=true; }
                    ready &= cancelled && !coop_saves_ui_active() && !coop_view_shaking() && map_current_map()==1;
                }
            } else if (tools_mode) {
                ready = game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && v
                    && (v->phase == COOP_CONTROL_IDLE || (phase == 9 && v->phase == COOP_CONTROL_VOTING));
                if (ready && phase <= 3) {
                    int64_t follower = tool_follower(1);
                    if (host && phase == 1 && !voted) voted = coop_runtime_combat(COOP_COMBAT_POLICY, 2, 0);
                    if (slot == 1 && !sent && follower && combat_is_turn_based() && combat_fast_turn_based_get()) {
                        if (phase == 2) { key(SDL_SCANCODE_F4, SDLK_F4); sent = true; }
                        else sent = coop_runtime_broadcast(player_get_local_pc_obj(), phase == 1 ? "#Wait" : "#Follow", 0,
                            obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                    }
                    ready &= follower && combat_is_turn_based() && combat_fast_turn_based_get();
                    if (follower) ready &= phase == 1 ? (obj_field_int32_get(follower, OBJ_F_NPC_FLAGS) & ONF_AI_WAIT_HERE) != 0
                        : phase == 2 ? (obj_field_int32_get(follower, OBJ_F_NPC_FLAGS) & ONF_AI_SPREAD_OUT) != 0
                                     : (obj_field_int32_get(follower, OBJ_F_NPC_FLAGS) & ONF_AI_WAIT_HERE) == 0;
                    ready &= coop_followers_settled(coop_gameplay_followers(game));
                } else if (ready && phase == 4) {
                    if (!sent) {
                        CoopNoteRequest note = { .operation = COOP_NOTE_ADD, .note = { .x = 700 + slot * 20, .y = 600, .type = slot % 3 } };
                        snprintf(note.note.text, sizeof(note.note.text), "Traveller %u shared annotation", slot);
                        sent = coop_runtime_note(&note);
                    }
                    ready &= tool_notes_saved() && coop_notes_settled(coop_gameplay_notes(game));
                } else if (ready && (phase == 5 || phase == 7)) {
                    int64_t caster = pc_slot(held, 2);
                    if (slot == 2 && !sent) {
                        TargetDescriptor target = { .obj = caster };
                        sent = coop_runtime_cast(caster, SPELL_TELEPORTATION, &target, false);
                    }
                    if (phase == 7 && (slot == 0 || slot == 3) && !picker_clicked) {
                        int64_t device = tool_item(slot == 0 ? BP_DYNAMITE : BP_SPIKE_TRAP, slot);
                        if (device) {
                            int64_t origin = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                            TargetDescriptor target = { .is_loc = true };
                            for (int dx = 6; dx < 12; dx++) {
                                target.loc = location_make(location_get_x(origin) + dx, location_get_y(origin) + (slot == 0 ? -6 : 6));
                                int8_t steps[128];
                                PathCreateInfo path = { .obj = player_get_local_pc_obj(), .from = origin, .to = target.loc,
                                    .max_rotations = 128, .rotations = steps };
                                if (!tile_is_blocking(target.loc, false) && path_make(&path) > 0) break;
                                target.loc = 0;
                            }
                            if (target.loc) picker_clicked = coop_runtime_use_item(device, &target, false);
                        }
                    }
                    if (phase == 7 && host && !cancelled && now - at > 15000) {
                        cancelled = true;
                        fprintf(stderr, "ARCMP tools devices bomb=%lld trap=%lld timer=%d choice=%llu\n",
                            (long long)tool_item(BP_DYNAMITE, 0), (long long)tool_item(BP_SPIKE_TRAP, 3), tool_trap_saved(),
                            (unsigned long long)tool_choice());
                    }
                    ready &= tool_choice() && (slot == 2 ? wmap_ui_is_created() : !wmap_ui_is_created())
                        && critter_fatigue_damage_get(caster) == 0;
                    if (phase == 7) {
                        ready &= !tool_item(BP_DYNAMITE, 0) && !tool_item(BP_SPIKE_TRAP, 3);
                        if (host && ready) {
                            ready &= tool_trap_saved();
                            if (ready && !voted) voted = coop_runtime_control(COOP_CONTROL_PAUSE, 1, 0, 0);
                        }
                        ready &= v->paused == 1;
                    }
                } else if (ready && phase == 6) {
                    if (slot == 2 && !sent && tool_choice() && wmap_ui_is_created() && !coop_magic_busy(coop_gameplay_magic(game))) { key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE); sent = true; }
                    ready &= !tool_choice() && !wmap_ui_is_created() && critter_fatigue_damage_get(pc_slot(held, 2)) == 0;
                } else if (ready && (phase == 8 || phase == 9)) {
                    if (slot == 3 && !sent && live) sent = coop_runtime_control(phase == 8 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0);
                    if (phase == 9 && slot != 3 && !voted && v->phase == COOP_CONTROL_VOTING)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, v->proposal, 0);
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && v->phase == COOP_CONTROL_IDLE && v->notice == COOP_CONTROL_OK
                        && v->action == (phase == 8 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD) && v->paused == 1
                        && tool_notes_saved() && tool_choice() && combat_is_turn_based() && combat_fast_turn_based_get()
                        && critter_fatigue_damage_get(pc_slot(held, 2)) == 0 && (!host || tool_trap_saved());
                } else if (ready && phase == 10) {
                    if (host && !voted) voted = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
                    if (slot == 2 && !v->paused && tool_choice() && wmap_ui_is_created()
                        && !coop_magic_busy(coop_gameplay_magic(game)) && now >= browser_next) {
                        if (!sent) good &= image(311, framebuffer);
                        /* Original Shrouded Hills marker, southeast of Crash
                         * Site in this centered world-map view. Drive the same
                         * mouse-up selection/confirmation as a human. */
                        click(443, 259); sent = true; browser_next = now + 500;
                    }
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && !v->paused && !tool_choice() && !wmap_ui_is_created()
                        && tool_notes_saved() && critter_fatigue_damage_get(pc_slot(held, 2)) > 0;
                    for (unsigned owner = 0; owner < 4; owner++)
                        ready &= location_dist(obj_field_int64_get(pc_slot(held, owner), OBJ_F_LOCATION), area_get_location(1)) <= 10;
                }
                if (ready) good &= image(300 + phase, framebuffer);
            } else if (preferences && !preferences_live) ready = false;
            else if (preferences) {
                int64_t actor = player_get_local_pc_obj();
                unsigned mask = slot * 3;
                CoopProgression* progression = coop_gameplay_progression(game);
                if (phase == 1) {
                    if (!sent && live && coop_progression_settled(progression) && preference_item(slot, 1)) {
                        /* Queue four native changes behind an in-flight full
                         * preference request, rather than pacing each click. */
                        good &= coop_runtime_player_options(15);
                        for (unsigned option = 0; option < COOP_OPTION_COUNT; option++)
                            good &= coop_runtime_player_option(option, ((mask >> option) & 1) != 0);
                        int64_t first = preference_item(slot, 0), recent = preference_item(slot, 1);
                        sub_57EFA0(HOTKEY_ITEM, 0, first); sub_57EFA0(HOTKEY_ITEM, 0, recent);
                        sub_573630(first); tig_mouse_set_position(205, 450);
                        good &= sub_57EDA0(TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP);
                        sent = true;
                    }
                    ready &= sent && coop_progression_settled(progression)
                        && coop_engine_options_record(actor) == (0xc0u | mask) && preference_bar(held, slot);
                } else if (phase == 2) {
                    if (host && !sent && live) good &= coop_runtime_difficulty(2);
                    if (!sent) { key(SDL_SCANCODE_F, SDLK_F); sent = true; browser_next = now + 400; }
                    if (!picker_clicked && now >= browser_next) { click(232, 231); picker_clicked = true; }
                    ready &= picker_clicked && fate_is_activated(actor, FATE_CRIT_SUCCESS_PICK_LOCKS)
                        && stat_base_get(actor, STAT_FATE_POINTS) == 1 && gamelib_game_difficulty_get() == 2;
                    if (ready && !cancelled) { good &= image(202, framebuffer); key(SDL_SCANCODE_F, SDLK_F); cancelled = true; ready = false; }
                } else {
                    if (slot == 3 && !sent && live) { sent = coop_runtime_control(phase == 3 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0); }
                    if (phase == 4 && slot != 3 && !voted && v->phase == COOP_CONTROL_VOTING)
                        voted = coop_runtime_control(COOP_CONTROL_VOTE, 1, v->proposal, 0);
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 && v->notice == COOP_CONTROL_OK
                        && v->action == (phase == 3 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD)
                        && coop_engine_options_record(actor) == (0xc0u | mask) && gamelib_game_difficulty_get() == 2
                        && fate_is_activated(actor, FATE_CRIT_SUCCESS_PICK_LOCKS) && stat_base_get(actor, STAT_FATE_POINTS) == 1
                        && preference_bar(held, slot);
                }
                if (ready && phase != 2) good &= image(200 + phase, framebuffer);
            } else if (recovery) {
                if (preview) {
                    if (slot == 1 && !sent && live) { good = coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0); sent = true; }
                    if (v->phase == COOP_CONTROL_VOTING && now - at > 1500 && !voted) { good = good && image(91, framebuffer); voted = true; }
                    if (v->phase == COOP_CONTROL_VOTING && slot == 2 && now - at > 2500 && !cancelled) { click(675, 565); cancelled = true; }
                    ready &= v->notice == COOP_CONTROL_DECLINED && v->action == COOP_CONTROL_LOAD;
                }
                ready &= script_global_var_get(0) == 52001;
                for (unsigned i = 0; i < 4; i++) ready &= item_gold_get(coop_campaign_object(i)) == 1000 + (int)i && stat_base_get(coop_campaign_object(i), STAT_STRENGTH) == 11 + (int)i;
            } else if (phase == 1) ready &= item && script_global_var_get(0) == 52001;
            else if (phase == 2) {
                if (slot == 1 && !sent && live) { good = coop_runtime_item(COOP_ITEM_PICKUP, item, 0, -1); sent = true; }
                if (host && !paused_native && anim_is_current_goal_type(pc_slot(held, 1), AG_PICKUP_ITEM, NULL) && !parent) { good = coop_runtime_control(COOP_CONTROL_PAUSE, 1, 0, 0); paused_native = true; }
                ready &= v->paused == 1 && !parent && (!host || anim_is_current_goal_type(pc_slot(held, 1), AG_PICKUP_ITEM, NULL));
            } else if (phase == 3 || phase == 4 || phase == 5 || phase == 6 || phase == 15 || phase == 17) {
                unsigned owner = phase == 3 || phase == 6 ? 1 : phase == 5 ? 3 : phase == 15 ? 2 : 0;
                unsigned mask = phase == 3 ? 3 : phase == 4 || phase == 5 ? 2 : phase == 15 ? 4 : 0;
                if (phase == 6 && now - at < (native_ui ? 2000u : 35000u)) {
                    DateTime clock = sub_45A7C0(); if (!freeze) freeze = clock.value;
                    if (clock.value != freeze || parent || (host && !anim_is_current_goal_type(pc_slot(held, 1), AG_PICKUP_ITEM, NULL))) { fprintf(stderr, "ARCMP pause changed clock/goal\n"); good = false; break; }
                    ready = false;
                } else {
                    if (slot == owner && !sent && live) { if (phase == 5 || phase == 17) good = coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0); else key(SDL_SCANCODE_PAUSE, SDLK_PAUSE); sent = true; }
                    ready &= v->paused == mask;
                    if (slot == owner) ready &= result == expected && status == (phase == 5 ? COOP_CONTROL_OWNER : COOP_CONTROL_OK);
                }
            } else if (phase == 7) ready &= parent == pc_slot(held, 1) && coop_items_settled(coop_gameplay_items(game));
            else if (phase == 8 || phase == 9 || phase == 12 || phase == 13) {
                unsigned owner = phase == 8 || phase == 12 ? 1 : phase == 9 ? 2 : 0;
                if (slot == owner && !sent && live) {
                    if (phase == 9) { key(SDL_SCANCODE_F12, SDLK_F12); }
                    else if (phase == 12 && !native_ui) key(SDL_SCANCODE_F10, SDLK_F10);
                    else if (phase == 12) { /* named_browser drives the selected saved campaign below */ }
                    else good = coop_runtime_control(COOP_CONTROL_REST, phase == 13 ? 4 : 0, 0, 0);
                    sent = true;
                }
                if (phase == 12 && native_ui && slot == owner && live && browser_step < 7)
                    good &= named_browser(false, &browser_step, &browser_next, framebuffer);
                if (phase == 9 && slot == owner && sent && !v->proposal && !voted && now - at > 650 && bed_object()) {
                    int64_t bed = bed_object(), pc = player_get_local_pc_obj();
                    good = coop_runtime_bed(bed); voted = true;
                    fprintf(stderr, "ARCMP bed requested=%d active=%d type=%d bed-type=%d expected=%d distance=%" PRId64 "\n", good, critter_is_active(pc), obj_field_int32_get(bed, OBJ_F_TYPE), tig_art_scenery_id_type_get(obj_field_int32_get(bed, OBJ_F_CURRENT_AID)), TIG_ART_SCENERY_TYPE_BEDS, object_dist(pc, bed));
                }
                if (phase == 9 && slot == owner && voted && !picker_clicked && sleep_ui_is_active()) { good = good && image(90, framebuffer); click(590, 70); picker_clicked = true; }
                if (v->phase == COOP_CONTROL_VOTING && slot != owner && !voted && now - at > 650) {
                    if (phase == 9 && (slot == 1 || slot == 3)) good = good && image(91, framebuffer);
                    /* Party toolbar coordinates include its window offset. */
                    if (phase == 8 && slot != 2) { ready = false; }
                    else { click(phase == 8 ? 675 : 535, 565); voted = true; }
                }
                if (phase == 13 && rest_story && host && v->phase == COOP_CONTROL_RESTING && v->completed >= 2 && !story_queued) {
                    /* Exercise the native script entry points while their
                     * shared action controller is deliberately unbound. The
                     * repeated opening must be coalesced and travel must wait
                     * until the remote chooser has answered. */
                    good &= !coop_engine_dialogue_bound();
                    int64_t actor = pc_slot(held, 2), npc = tagged(true);
                    Script script = { .num = 1324 };
                    ScriptInvocation hint = { .script = &script, .triggerer_obj = actor,
                        .attachee_obj = npc, .attachment_point = SAP_DIALOG };
                    good &= script_campaign_line_probe(&hint, 1);
                    dialog_ui_start_dialog(actor, npc, 1324, 0, 1);
                    dialog_ui_start_dialog(actor, npc, 1324, 0, 1);
                    TeleportData travel = { .obj = actor, .map = map_current_map(),
                        .loc = obj_field_int64_get(actor, OBJ_F_LOCATION), .flags = TELEPORT_COOP_PARTY };
                    good &= coop_engine_dialogue_pending() && teleport_do(&travel);
                    story_queued = true;
                }
                if (phase == 13 && rest_story && game) {
                    const CoopDialogueView* page = coop_dialogue_current(coop_gameplay_dialogue(game));
                    if (page->active && !story_seen) {
                        unsigned hints = 0;
                        for (unsigned i = 0; i < coop_journal_count(coop_engine_journal()); i++) {
                            CoopDialogueView recorded; char speaker[COOP_JOURNAL_SPEAKER];
                            if (coop_journal_page(coop_engine_journal(), i, &recorded, speaker)
                                && recorded.mode == COOP_DIALOGUE_FLOAT && recorded.chooser == 2
                                && !memcmp(recorded.pc_guid, page->pc_guid, sizeof(page->pc_guid))
                                && !strcmp(recorded.reply, page->reply)) hints++;
                        }
                        good &= hints == 2;
                        fprintf(stderr, "ARCMP retained story hints=%u slot=%u\n", hints, slot);
                        good &= page->chooser == 2 && page->reply[0] && coop_runtime_epoch() == baseline_epoch + 1
                            && v->notice == COOP_CONTROL_INTERRUPTED && image(130, framebuffer)
                            && coop_session_chat(held, "control-story-seen");
                        story_seen = true;
                    }
                    if (page->active && slot == 2 && story_observers == 15 && !cancelled)
                        cancelled = coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                }
                if (phase == 13 && !rest_story && slot == 3 && v->phase == COOP_CONTROL_RESTING && v->completed >= 2 && !cancelled)
                    cancelled = coop_runtime_control(COOP_CONTROL_CANCEL, 0, v->proposal, 0);
                if (phase == 8) ready &= v->notice == COOP_CONTROL_DECLINED && v->action == COOP_CONTROL_REST && bed_object()
                    && pc_slot(held, 2) && object_dist(pc_slot(held, 2), bed_object()) <= 3;
                else {
                    ready &= coop_runtime_epoch() == baseline_epoch + 1 + (phase == 13 && rest_story) && v->epoch == coop_runtime_epoch() && v->action == (phase == 12 ? COOP_CONTROL_LOAD : COOP_CONTROL_REST);
                    if (phase == 13 && rest_story) ready &= story_seen && story_observers == 15;
                    if (phase == 13) { ready &= v->notice == COOP_CONTROL_INTERRUPTED && v->completed >= 2 && v->completed < 24; if (ready) rest_hours = v->completed; }
                    else ready &= v->notice == COOP_CONTROL_OK;
                    if (phase == 9 && ready) {
                        ready &= v->completed == 2;
                        for (unsigned i = 0; i < 4; i++) {
                            int64_t pc = coop_campaign_object(i); int hp = 9 + (int)i - 4 * stat_level_get(pc, STAT_HEAL_RATE); if (hp < 0) hp = 0;
                            if (object_hp_damage_get(pc) != hp && !sent) fprintf(stderr, "ARCMP rest HP member=%u got=%d expected=%d rate=%d\n", i, object_hp_damage_get(pc), hp, stat_level_get(pc, STAT_HEAL_RATE));
                            ready &= object_hp_damage_get(pc) == hp; saved_hp[i] = (unsigned)hp;
                        }
                        int64_t follower = tagged(true); ready &= follower && object_hp_damage_get(follower) < 9;
                        if (!sent) fprintf(stderr, "ARCMP rest follower=%" PRId64 " hp=%d\n", follower, follower ? object_hp_damage_get(follower) : -1);
                        sent = true;
                    }
                    if (phase == 12 && ready) {
                        ready &= script_global_var_get(0) == 52001;
                        for (unsigned i = 0; i < 4; i++) ready &= item_gold_get(coop_campaign_object(i)) == 1000 + (int)i && object_hp_damage_get(coop_campaign_object(i)) == (int)saved_hp[i];
                    }
                }
            } else if (phase == 10 || phase == 16) {
                if (slot == 3 && !sent && live) {
                    replay = (CoopControlRequest) { .epoch = coop_runtime_epoch(), .sequence = expected, .map = (uint32_t)map_current_map(), .action = COOP_CONTROL_SAVE };
                    memcpy(replay.actor, obj_get_id(player_get_local_pc_obj()).d.g.data, 16);
                    if (phase == 10 && native_ui) {
                        replay.value = 63;
                        good &= named_browser(true, &browser_step, &browser_next, framebuffer);
                        sent = browser_step >= 7;
                    } else { key(SDL_SCANCODE_F9, SDLK_F9); sent = true; }
                }
                ready &= coop_runtime_epoch() == baseline_epoch + 1 && v->epoch == coop_runtime_epoch() && v->action == COOP_CONTROL_SAVE && v->notice == COOP_CONTROL_OK;
                if (host) {
                    if (phase == 10 && native_ui) {
                        CoopSaveSet party; CoopSaveEntry entry;
                        bool available = coop_runtime_save_set(&party);
                        if (available) coop_save_inspect(&party, 63, &entry);
                        ready &= available && entry.status == COOP_CHECKPOINT_OK && !strcmp(entry.info.description, "Four travellers - named slot63");
                    } else ready &= coop_runtime_checkpoint_exists();
                    ready &= coop_runtime_recovery_exists();
                }
                if (phase == 16) ready &= v->paused == 4;
                if (slot == 3 && ready && !replayed) {
                    if (!native_ui || phase != 10) { uint8_t bytes[COOP_CONTROL_REQUEST_BYTES]; good = coop_control_request_encode(&replay, bytes) && coop_session_send(held, 0, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)); }
                    replayed = true; ready = false;
                }
                if (slot == 3) ready &= replayed;
            } else if (phase == 11) {
                ready &= script_global_var_get(0) == 52002;
                for (unsigned i = 0; i < 4; i++) ready &= item_gold_get(coop_campaign_object(i)) == 1017 + (int)i && object_hp_damage_get(coop_campaign_object(i)) == 12;
            } else if (phase == 14) {
                if (!guards) { guards = !gamelib_save("ForbiddenCoopLocal", "must not write") && !gamelib_load("arcmp-bootstrap"); good = guards; }
                ready &= guards;
            } else if (phase == 18) ready &= !v->paused && coop_runtime_epoch() == 6 + rest_story;
            if (ready && good) {
                if ((phase == 3 || phase == 13 || phase == 16) && (slot == 1 || slot == 3)) good = image(phase, framebuffer);
                char text[64]; snprintf(text, sizeof(text), "control-done:%u", phase); good = good && coop_session_chat(held, text); done = true; completed = phase;
            }
        }
        if (phase && !done && now - at > (phase == 6 ? 44000u : 25000u)) { image(phase, framebuffer); fprintf(stderr, "ARCMP controls timeout phase=%u sent=%d voted=%d epoch=%u view=%u proposal=%u hours=%u paused=%u notice=%u\n", phase, sent, voted, coop_runtime_epoch(), v->phase, v->proposal, v->completed, v->paused, v->notice); good = false; break; }
        if (host && done && !finishing && game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == phase;
            if (all) { if (phase < last_phase) good = good && announce(held, phase + 1); else { good = good && coop_gameplay_finish(game); finishing = true; } }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended = now; if (now - ended > 600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message; while (tig_message_dequeue(&message) == TIG_OK) { if (message.type == TIG_MESSAGE_QUIT) good = false; else if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message); }
        SDL_Delay(2);
    }
    CoopGameplay* game = coop_runtime_gameplay();
    good = good && game && coop_gameplay_finished(game) && completed == last_phase
        && coop_controls_executed(coop_runtime_controls()) == (host && !recovery ? feedback_mode ? 4u : qol_mode ? 3u : preferences || tools_mode ? 2u : 5u : 0u);
    FILE* report = fopen("arcmp-controls-result.json", "wb"); if (!report) good = false;
    if (report) {
        unsigned first = tail ? 8 : 1;
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"first_phase\":%u,\"phases_completed\":%u,\"epoch\":%u,\"executed\":%u,\"long_pause_ms\":%u,\"cancelled_hours\":%u,\"native_guards\":%s,\"recovery\":%s,\"rest_story\":%s,\"story_observers\":%u}\n",
            good ? "true" : "false", completed, first, completed >= first ? completed - first + 1 : 0,
            coop_runtime_epoch(), coop_controls_executed(coop_runtime_controls()),
            recovery || tail || preferences || tools_mode || qol_mode ? 0 : native_ui ? 2000 : 35000,
            rest_hours, guards ? "true" : "false", recovery ? "true" : "false",
            rest_story && story_seen ? "true" : "false", story_observers);
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP controls probe %s phases=%u\n", good ? "PASS" : "FAIL", completed); return good;
}
