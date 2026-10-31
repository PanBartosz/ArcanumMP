#include "coop/localization.h"
#include "coop/language_engine.h"
#include "ui/dialog_ui.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/dialogue_ui.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_object.h"
#include "coop/runtime.h"
#include "game/description.h"
#include "game/tc.h"

#include <stdio.h>
#include <string.h>

#include "game/ai.h"
#include "game/anim.h"
#include "game/broadcast.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/dialog.h"
#include "game/gsound.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj_pool.h"
#include "game/obj_private.h"
#include "game/player.h"
#include "game/reaction.h"
#include "game/script.h"
#include "game/script_name.h"
#include "game/stat.h"
#include "game/tb.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/schematic_ui.h"
#include "ui/tb_ui.h"
#include "ui/wmap_ui.h"

#define MAX_ENTRIES 8

typedef struct DialogUiEntry {
    /* 0000 */ int slot;
    /* 0004 */ int dlg;
    /* 0008 */ DialogState state;
    /* 1850 */ int field_1850;
    /* 1854 */ int script_num;
    /* 1858 */ int script_line;
    /* 185C */ char field_185C;
    /* 185D */ char field_185D;
    /* 185E */ char field_185E;
    /* 185F */ char field_185F;
} DialogUiEntry;

static DialogUiEntry* sub_567420(int64_t obj);
static void sub_5679C0(DialogUiEntry* entry);
static bool dialog_ui_process_option(DialogUiEntry* entry, int option);
static bool dialog_ui_message_filter(TigMessage* msg);
static void dialog_ui_execute_script(DialogUiEntry* entry, int line);
static void dialog_ui_display(DialogUiEntry* entry);
static void dialog_ui_npc_say(int64_t npc_obj, int64_t pc_obj, int type, int expires_in, const char* str, int speech_id);
static void dialog_ui_pc_say(int64_t pc_obj, int64_t npc_obj, int type, int expires_in, const char* str);
static void dialog_ui_speech_start(int64_t npc_obj, int64_t pc_obj, int speech_id);
static void dialog_ui_speech_stop(void);

// 0x66DAB8
static DialogUiEntry stru_66DAB8[8];

// 0x67B960
static tig_sound_handle_t dialog_ui_speech_handle;

// 0x67B964
static bool dialog_ui_local_pc_dialog_active;
static bool coop_presenting;
static bool coop_visible;
static bool coop_sheet_presented, coop_map_presented, coop_chooser;
static CoopDialogueView coop_view;
static char coop_restored_actions[COOP_DIALOGUE_OPTIONS][COOP_DIALOGUE_ACTION_TEXT];

// 0x567330
bool dialog_ui_init(GameInitInfo* init_info)
{
    int index;

    (void)init_info;

    for (index = 0; index < MAX_ENTRIES; index++) {
        stru_66DAB8[index].field_1850 = false;
        stru_66DAB8[index].slot = index;
    }

    script_set_callbacks(dialog_ui_start_dialog, dialog_ui_float_line);
    ai_set_callbacks(dialog_ui_end_dialog, dialog_ui_float_line);
    broadcast_set_float_line_func(dialog_ui_float_line);
    dialog_ui_speech_handle = TIG_SOUND_HANDLE_INVALID;

    return true;
}

// 0x5673A0
void dialog_ui_exit(void)
{
    /* Object destruction can still notify the UI after dialog files exit.
     * Release active entries while both UI and dialog storage are alive. */
    dialog_ui_reset();
    dialog_ui_speech_stop();
}

// 0x5673B0
void dialog_ui_reset(void)
{
    int index;

    if (dialog_ui_local_pc_dialog_active) {
        intgame_dialog_end();
        dialog_ui_local_pc_dialog_active = false;
    }

    for (index = 0; index < MAX_ENTRIES; index++) {
        if (stru_66DAB8[index].field_1850) {
            sub_5679C0(&(stru_66DAB8[index]));
        }
    }
}

// 0x567400
bool dialog_ui_is_in_dialog(int64_t obj)
{
    if (coop_engine_dialogue_bound()) {
        DialogUiEntry* entry = &stru_66DAB8[0];
        return entry->field_1850 && (obj == entry->state.pc_obj || obj == entry->state.npc_obj);
    }
    return sub_567420(obj)->field_1850;
}

// 0x567420
DialogUiEntry* sub_567420(int64_t obj)
{
    int index = 0;

    return &(stru_66DAB8[index]);
}

static void dialog_ui_start_dialog_internal(int64_t pc_obj, int64_t npc_obj, int script_num, int script_line, int num);

// 0x567460
void dialog_ui_start_dialog(int64_t pc_obj, int64_t npc_obj, int script_num, int script_line, int num)
{
    if (coop_engine_dialogue_request(pc_obj, npc_obj, script_num, script_line, num)) return;
    dialog_ui_start_dialog_internal(pc_obj, npc_obj, script_num, script_line, num);
    coop_engine_dialogue_started();
}

static void dialog_ui_start_dialog_internal(int64_t pc_obj, int64_t npc_obj, int script_num, int script_line, int num)
{
    DialogUiEntry* entry;
    char path[TIG_MAX_PATH];
    char str[2000];

    if (critter_is_dead(pc_obj)) {
        return;
    }

    if (critter_is_unconscious(pc_obj)) {
        return;
    }

    int speak = ai_can_speak(npc_obj, pc_obj, false);
    if (speak != AI_SPEAK_OK) {
        if (coop_engine_dialogue_bound()) fprintf(stderr, "ARCMP native dialogue admission script=%d speak=%d npc-name=%d actor=%d\n",
            script_num, speak, obj_field_int32_get(npc_obj, OBJ_F_NAME), coop_campaign_slot_for_object(pc_obj));
        return;
    }

    if (!coop_engine_dialogue_bound() && player_is_local_pc_obj(pc_obj) && intgame_mode_get() == INTGAME_MODE_DIALOG) {
        return;
    }

    if (combat_critter_is_combat_mode_active(pc_obj)) {
        if (!combat_can_exit_combat_mode(pc_obj)) {
            return;
        }
        combat_critter_deactivate_combat_mode(pc_obj);
    }

    entry = sub_567420(pc_obj);

    if (script_num != 0 && script_name_build_dlg_name(script_num, path, sizeof(path))) {
        if (!dialog_load(path, &(entry->dlg))) {
            return;
        }

        entry->state.dlg = entry->dlg;
        entry->state.pc_obj = pc_obj;
        entry->state.npc_obj = npc_obj;
        entry->state.num = num;
        entry->state.script_num = script_num;
        if (!sub_412FD0(&(entry->state))) {
            dialog_unload(entry->dlg);
            return;
        }

        if (entry->state.field_17E8 == 4) {
            if (coop_engine_dialogue_bound()) coop_engine_dialogue_publish(&entry->state, false);
            else dialog_ui_npc_say(entry->state.npc_obj,
                entry->state.pc_obj,
                TB_TYPE_WHITE,
                TB_EXPIRE_DEFAULT,
                entry->state.reply,
                entry->state.speech_id);
            sub_413280(&(entry->state));
            dialog_unload(entry->dlg);
            return;
        }

        if (!coop_engine_dialogue_bound() && player_is_local_pc_obj(pc_obj)) {
            if (!intgame_dialog_begin(dialog_ui_message_filter)) {
                sub_413280(&(entry->state));
                dialog_unload(entry->dlg);
                return;
            }

            dialog_ui_local_pc_dialog_active = true;

            if (!intgame_is_compact_interface()) {
                sub_57CD60(pc_obj, npc_obj, str);
                intgame_examine_object(pc_obj, npc_obj, str);
                object_hover_obj_set(npc_obj);
            }
        }

        sub_424070(pc_obj, 3, 0, true);
        anim_goal_rotate(pc_obj, object_rot(pc_obj, npc_obj));

        if (critter_is_concealed(pc_obj)) {
            critter_set_concealed(pc_obj, false);
        }

        if (critter_is_concealed(npc_obj)) {
            critter_set_concealed(npc_obj, false);
        }

        entry->script_num = script_num;
        entry->script_line = script_line;
        entry->field_1850 = 1;

        dialog_ui_display(entry);
    } else {
        sub_5681C0(pc_obj, npc_obj);
    }
}

// 0x5678D0
void dialog_ui_end_dialog(int64_t obj, int a2)
{
    DialogUiEntry* entry;

    (void)a2;

    if (coop_presenting) return;

    entry = sub_567420(obj);
    if (!entry->field_1850) {
        return;
    }

    entry->field_1850 = false;

    if (coop_engine_dialogue_bound()) coop_engine_dialogue_publish(&entry->state, false);
    else if (player_is_local_pc_obj(obj)) {
        intgame_dialog_end();
        dialog_ui_local_pc_dialog_active = 0;
    }

    if (entry->dlg >= 0) dialog_unload(entry->dlg);
    entry->dlg = -1;

    tb_expire_in(entry->state.npc_obj, TB_EXPIRE_DEFAULT);

    sub_413280(&(entry->state));
}

// 0x5679C0
void sub_5679C0(DialogUiEntry* entry)
{
    if (!entry->field_1850) {
        return;
    }

    entry->field_1850 = false;

    if (entry->dlg >= 0) dialog_unload(entry->dlg);
    entry->dlg = -1;

    sub_413280(&(entry->state));
}

// 0x567A10
bool dialog_ui_is_local_pc_in_dialog(void)
{
    return dialog_ui_local_pc_dialog_active;
}

// 0x567A20
void dialog_ui_notify_dialog_ended(int64_t obj)
{
    if (coop_engine_dialogue_bound()) return;
    sub_567420(obj)->field_1850 = false;
    if (player_is_local_pc_obj(obj)) {
        intgame_dialog_end();
        dialog_ui_local_pc_dialog_active = false;
    }
}

// 0x567A60
void dialog_ui_notify_dialog_started(int64_t obj)
{
    if (coop_engine_dialogue_bound()) return;
    sub_567420(obj)->field_1850 = true;
    if (player_is_local_pc_obj(obj)) {
        if (intgame_dialog_begin(dialog_ui_message_filter)) {
            dialog_ui_local_pc_dialog_active = true;
        }
    }
}

// 0x567E30
bool dialog_ui_process_option(DialogUiEntry* entry, int option)
{
    bool is_pc;

    is_pc = !coop_engine_dialogue_bound() && player_is_local_pc_obj(entry->state.pc_obj);
    coop_engine_dialogue_selected(entry->state.options[option],&entry->state.text_refs[option+1]);
    dialog_ui_pc_say(entry->state.pc_obj,
        entry->state.npc_obj,
        TB_TYPE_GREEN,
        TB_EXPIRE_DEFAULT,
        entry->state.options[option]);
    tb_remove(entry->state.npc_obj);
    dialog_ui_speech_stop();
    sub_413130(&(entry->state), option);

    switch (entry->state.field_17E8) {
    case 0:
        dialog_ui_display(entry);
        break;
    case 1:
        dialog_ui_end_dialog(entry->state.pc_obj, 0);
        dialog_ui_execute_script(entry, 0);
        break;
    case 2:
        dialog_ui_end_dialog(entry->state.pc_obj, 0);
        dialog_ui_execute_script(entry, entry->state.field_17EC);
        break;
    case 3:
        if (coop_engine_dialogue_bound() && coop_authority_can_simulate())
            sub_572640(entry->state.pc_obj, entry->state.npc_obj, INVEN_UI_MODE_BARTER);
        if (is_pc) {
            intgame_dialog_clear();
            inven_ui_open(entry->state.pc_obj, entry->state.npc_obj, INVEN_UI_MODE_BARTER);
        }
        break;
    case 4:
        if (!coop_engine_dialogue_bound()) dialog_ui_npc_say(entry->state.npc_obj,
            entry->state.pc_obj,
            TB_TYPE_WHITE,
            TB_EXPIRE_DEFAULT,
            entry->state.reply,
            entry->state.speech_id);
        dialog_ui_end_dialog(entry->state.pc_obj, 0);
        break;
    case 5:
        if (is_pc) {
            intgame_dialog_clear();
            charedit_open(entry->state.npc_obj, CHAREDIT_MODE_PASSIVE);
        }
        break;
    case 6:
        if (is_pc) {
            intgame_dialog_clear();
            wmap_ui_open();
        }
        break;
    case 7:
        if (is_pc) {
            intgame_dialog_clear();
            schematic_ui_toggle(entry->state.npc_obj, entry->state.pc_obj);
        }
        break;
    case 8:
        if (is_pc) {
            intgame_dialog_clear();
            ui_show_inven_npc_identify(entry->state.pc_obj, entry->state.npc_obj);
        }
        break;
    case 9:
        if (is_pc) {
            intgame_dialog_clear();
            inven_ui_open(entry->state.pc_obj, entry->state.npc_obj, INVEN_UI_MODE_NPC_REPAIR);
        }
        break;
    }

    if (coop_engine_dialogue_bound() && entry->field_1850 && entry->state.field_17E8 >= 3)
        coop_engine_dialogue_publish(&entry->state, true);

    return true;
}

// 0x5680A0
bool dialog_ui_message_filter(TigMessage* msg)
{
    DialogUiEntry* entry;
    int option;
    int player;

    entry = sub_567420(player_get_local_pc_obj());

    option = intgame_dialog_get_option(msg);
    if (coop_engine_dialogue_bound()) {
        if (option >= 0) coop_engine_dialogue_submit((unsigned)option);
        else sub_5517A0(msg);
        return true;
    }
    if (option == -1) {
        sub_5517A0(msg);
        return true;
    }

    if (!dialog_ui_process_option(entry, option)) {
        sub_5517A0(msg);
    }

    return true;
}

// 0x5681C0
void sub_5681C0(int64_t pc_obj, int64_t npc_obj)
{
    char text[1000];

    sub_4132A0(npc_obj, pc_obj, text);
    dialog_ui_float_line(npc_obj, pc_obj, text, -1);
}

// 0x568430
void dialog_ui_float_line(int64_t npc_obj, int64_t pc_obj, const char* str, int speech_id)
{
    int type;

    if (coop_engine_dialogue_float(npc_obj, pc_obj, str, speech_id)) return;

    type = obj_field_int32_get(npc_obj, OBJ_F_TYPE) != OBJ_TYPE_PC
        ? TB_TYPE_WHITE
        : TB_TYPE_GREEN;
    dialog_ui_npc_say(npc_obj, pc_obj, type, TB_EXPIRE_DEFAULT, str, speech_id);
}

// 0x568480
void dialog_ui_execute_script(DialogUiEntry* entry, int line)
{
    if (line == 0) {
        line = entry->script_line + 1;
    }

    if (coop_engine_dialogue_bound())
        object_script_execute_dialogue(entry->state.pc_obj, entry->state.npc_obj, entry->script_num, line);
    else
        object_script_execute(entry->state.pc_obj, entry->state.npc_obj, OBJ_HANDLE_NULL, SAP_DIALOG, line);
}

// 0x5684C0
void dialog_ui_display(DialogUiEntry* entry)
{
    int index;

    if (coop_engine_dialogue_bound()) {
        coop_engine_dialogue_publish(&entry->state, true);
        return;
    }

    dialog_ui_npc_say(entry->state.npc_obj,
        entry->state.pc_obj,
        TB_TYPE_WHITE,
        TB_EXPIRE_NEVER,
        entry->state.reply,
        entry->state.speech_id);

    if (player_is_local_pc_obj(entry->state.pc_obj)) {
        intgame_dialog_clear();

        for (index = 0; index < entry->state.num_options; index++) {
            char localized[COOP_DIALOGUE_TEXT];
            intgame_dialog_set_option(index,coop_language_dialogue(entry->state.options[index],entry->state.pc_obj,entry->state.npc_obj,localized,sizeof(localized)));
        }
    }
}

// 0x568540
void dialog_ui_npc_say(int64_t npc_obj, int64_t pc_obj, int type, int expires_in, const char* str, int speech_id)
{
    char localized[COOP_DIALOGUE_TEXT];
    str=coop_language_dialogue(str,pc_obj,npc_obj,localized,sizeof(localized));
    if (pc_obj != OBJ_HANDLE_NULL
        && !critter_is_dead(npc_obj)
        && !sub_423300(npc_obj, NULL)) {
        sub_424070(npc_obj, 3, 0, 1);
        anim_goal_rotate(npc_obj, object_rot(npc_obj, pc_obj));
    }

    tb_remove(npc_obj);
    tb_add(npc_obj, type, str);
    tb_expire_in(npc_obj, expires_in);

    dialog_ui_speech_start(npc_obj, pc_obj, speech_id);
}

// 0x5686C0
void dialog_ui_pc_say(int64_t pc_obj, int64_t npc_obj, int type, int expires_in, const char* str)
{
    char localized[COOP_DIALOGUE_TEXT];
    str=coop_language_dialogue(str,pc_obj,npc_obj,localized,sizeof(localized));
    if (npc_obj != OBJ_HANDLE_NULL
        && !critter_is_dead(pc_obj)
        && !sub_423300(pc_obj, NULL)) {
        sub_424070(pc_obj, 3, 0, 1);
        anim_goal_rotate(pc_obj, object_rot(pc_obj, npc_obj));
    }

    if (!player_is_local_pc_obj(pc_obj)) {
        tb_remove(pc_obj);
        tb_add(pc_obj, type, str);
        tb_expire_in(pc_obj, expires_in);
    }
}

// 0x568830
void dialog_ui_notify_object_destroyed(int64_t obj)
{
    int index;

    for (index = 0; index < 8; index++) {
        if (stru_66DAB8[index].state.pc_obj == obj
            || stru_66DAB8[index].state.npc_obj == obj) {
            dialog_ui_end_dialog(obj, 0);
        }
    }
}

// 0x5688D0
void dialog_ui_speech_start(int64_t npc_obj, int64_t pc_obj, int speech_id)
{
    int v1;
    int v2;
    char gender;
    char path[TIG_MAX_PATH];

    (void)npc_obj;

    if (speech_id == -1) {
        return;
    }

    dialog_ui_speech_stop();
    sub_418A00(speech_id, &v1, &v2);

    gender = stat_level_get(pc_obj, STAT_GENDER) != 0 ? 'm' : 'f';
    snprintf(path, sizeof(path),
        "sound\\speech\\%.5d\\v%d_%c.mp3",
        v1,
        v2,
        gender);
    if (gender == 'f' && !tig_file_exists(path, NULL)) {
        snprintf(path, sizeof(path),
            "sound\\speech\\%.5d\\v%d_%c.mp3",
            v1,
            v2,
            'm');
    }
    if (tig_file_exists(path, NULL)) {
        dialog_ui_speech_handle = gsound_play_voice(path, 0);
    }
}

// 0x5689B0
void dialog_ui_speech_stop(void)
{
    if (dialog_ui_speech_handle != TIG_SOUND_HANDLE_INVALID) {
        tig_sound_destroy(dialog_ui_speech_handle);
        dialog_ui_speech_handle = TIG_SOUND_HANDLE_INVALID;
    }
}

bool dialog_ui_coop_choose(unsigned option)
{
    DialogUiEntry* entry = &stru_66DAB8[0];
    if (option == COOP_DIALOGUE_CANCEL && entry->field_1850) {
        dialog_ui_end_dialog(entry->state.pc_obj, 0);
        return true;
    }
    if (!entry->field_1850 || entry->state.field_17E8 != 0
        || option >= (unsigned)entry->state.num_options) return false;
    return dialog_ui_process_option(entry, (int)option);
}
bool dialog_ui_coop_companion_inventory(int64_t pc, int64_t npc)
{
    if (!coop_engine_dialogue_bound() || !coop_authority_can_simulate() || !coop_engine_dialogue_can_start(pc)
        || !pc || !npc || critter_pc_leader_get(npc) != pc || !critter_is_active(pc) || !critter_is_active(npc)
        || object_dist(pc, npc) > 10) return false;
    bool combat = combat_critter_is_combat_mode_active(pc);
    bool native = inven_ui_can_open_inven(pc, npc);
    if (combat || !native || !sub_572370(pc, npc, INVEN_UI_MODE_BARTER)) {
        fprintf(stderr, "ARCMP companion inventory refused combat=%d native=%d speak=%d active=%d/%d distance=%lld\n",
            combat, native, ai_can_speak(npc, pc, false), critter_is_active(pc), critter_is_active(npc), (long long)object_dist(pc, npc));
        return false;
    }
    DialogUiEntry* entry = sub_567420(pc);
    if (entry->field_1850) return false;
    sub_572640(pc, npc, INVEN_UI_MODE_BARTER);
    memset(entry, 0, sizeof(*entry));
    entry->dlg = entry->state.dlg = -1;
    entry->state.pc_obj = pc;
    entry->state.npc_obj = npc;
    entry->state.field_17E8 = 3;
    entry->state.speech_id = -1;
    snprintf(entry->state.reply, sizeof(entry->state.reply), coop_tr("Managing companion inventory."));
    sub_4C1020(npc, pc);
    entry->field_1850 = true;
    dialog_ui_display(entry);
    return !coop_engine_dialogue_failed();
}
bool dialog_ui_coop_service_valid(int64_t pc, int64_t npc, unsigned mode)
{
    DialogUiEntry* entry = &stru_66DAB8[0];
    return coop_engine_dialogue_bound() && coop_authority_can_simulate() && entry->field_1850
        && entry->state.pc_obj == pc && entry->state.npc_obj == npc && (mode == 3 || mode == 7 || mode == 8 || mode == 9) && (unsigned)entry->state.field_17E8 == mode;
}

bool dialog_ui_coop_present(const CoopDialogueView* view, bool chooser)
{
    int64_t pc = OBJ_HANDLE_NULL;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (member && !memcmp(member->object_id.d.g.data, view->pc_guid, 16)) pc = coop_campaign_object(i);
    }
    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
    if (view->active && (!pc || !npc)) return false;
    coop_presenting = true;
    if (coop_sheet_presented && (!view->active || view->mode != 5 || !chooser)) {
        coop_sheet_presented = false;
        charedit_close();
    }
    if (coop_map_presented && (!view->active || view->mode != 6 || !chooser)) {
        coop_map_presented = false;
        wmap_ui_close();
    }
    coop_chooser = chooser;
    bool service = view->active && chooser && (view->mode == 3 || view->mode == 8 || view->mode == 9);
    bool craft = view->active && chooser && view->mode == 7;
    if (!service) inven_ui_coop_present_service(OBJ_HANDLE_NULL, 0);
    if (!craft) schematic_ui_coop_present(OBJ_HANDLE_NULL);
    if (view->active && !coop_dialogue_ui_show(view, chooser, description_get(obj_field_int32_get(npc, OBJ_F_DESCRIPTION)))) {
        coop_presenting = false;
        return false;
    }
    coop_presenting = true;
    coop_view = *view;
    if (view->active) {
        if (!coop_visible) intgame_dialog_begin(dialog_ui_message_filter);
        tc_hide();
        coop_visible = true;
        dialog_ui_local_pc_dialog_active = true;
        location_origin_set(obj_field_int64_get(npc, OBJ_F_LOCATION));
        tb_remove(pc);
        tb_remove(npc);
        if (chooser && view->mode == 5) {
            if (!coop_sheet_presented) {
                if (charedit_is_created()) charedit_close();
                charedit_open(npc, CHAREDIT_MODE_PASSIVE);
                coop_sheet_presented = charedit_is_created();
            }
            if (coop_sheet_presented) coop_dialogue_ui_hide();
        }
        if (chooser && view->mode == 6) {
            if (!coop_map_presented) {
                if (wmap_ui_is_created()) wmap_ui_close();
                wmap_ui_open();
                coop_map_presented = wmap_ui_is_created();
            }
            if (coop_map_presented) coop_dialogue_ui_hide();
        }
        if (service) {
            coop_dialogue_ui_hide();
            if (!inven_ui_coop_present_service(npc, view->mode)) { coop_presenting = false; return false; }
        }
        if (craft) {
            coop_dialogue_ui_hide();
            if (!schematic_ui_coop_present(npc)) { coop_presenting = false; return false; }
        }
    } else {
        if (coop_visible) {
            intgame_dialog_clear();
            intgame_dialog_end();
        }
        coop_visible = false;
        dialog_ui_local_pc_dialog_active = false;
        coop_dialogue_ui_hide();
        if (npc && view->reply[0]) {
            tb_remove(npc);
            tb_add(npc, TB_TYPE_WHITE, view->reply);
            tb_expire_in(npc, TB_EXPIRE_DEFAULT);
        }
    }
    if (npc && view->reply[0]) dialog_ui_speech_start(npc, pc, view->speech);
    coop_presenting = false;
    return true;
}

bool dialog_ui_coop_capture(CoopDialogueRecovery* recovery)
{
    DialogUiEntry* entry = &stru_66DAB8[0];
    recovery->active = false;
    if (!entry->field_1850) return true;
    const DialogState* state = &entry->state;
    unsigned mode = (unsigned)state->field_17E8;
    if (mode != 0 && mode != 3 && (mode < 5 || mode > 9)) return false;
    if (entry->dlg < 0 && (mode != 3 || entry->script_num || state->script_num)) return false;
    if (state->num_options < 0 || state->num_options > COOP_DIALOGUE_OPTIONS
        || !coop_object_id_from_engine(obj_get_id(state->pc_obj), &recovery->current.pc)
        || !coop_object_id_from_engine(obj_get_id(state->npc_obj), &recovery->current.npc)) return false;
    recovery->current.map = map_current_map();
    recovery->current.script = entry->script_num;
    recovery->current.line = entry->script_line;
    recovery->current.page = state->num;
    recovery->mode = mode;
    recovery->state_script = state->script_num;
    recovery->last_reply = state->field_17EC;
    recovery->speech = state->speech_id;
    recovery->response = state->field_1840;
    recovery->seed = state->seed;
    recovery->options = (unsigned)state->num_options;
    memcpy(recovery->reply, state->reply, sizeof(recovery->reply));
    recovery->text_refs[0] = state->text_refs[0];
    for (unsigned i = 0; i < recovery->options; i++) {
        memcpy(recovery->text[i], state->options[i], sizeof(recovery->text[i]));
        recovery->text_refs[i + 2] = state->text_refs[i + 1];
        recovery->target[i] = state->field_17F0[i];
        recovery->argument[i] = state->field_1804[i];
        recovery->auxiliary[i] = state->field_1818[i];
        const char* action = state->actions[i] ? state->actions[i] : "";
        size_t size = strlen(action);
        if (size >= sizeof(recovery->actions[i])) return false;
        memcpy(recovery->actions[i], action, size + 1);
    }
    recovery->active = true;
    return true;
}
bool dialog_ui_coop_restore(const CoopDialogueRecovery* recovery)
{
    if (!recovery->active || !coop_authority_can_simulate() || !coop_engine_dialogue_bound()
        || recovery->current.map != map_current_map() || recovery->options > COOP_DIALOGUE_OPTIONS) return false;
    int64_t pc = obj_pool_perm_lookup(coop_object_id_to_engine(recovery->current.pc));
    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(recovery->current.npc));
    if (!pc || !npc || coop_campaign_slot_for_object(pc) < 0) return false;
    DialogUiEntry* entry = &stru_66DAB8[0];
    if (entry->field_1850) return false;
    char path[TIG_MAX_PATH];
    int dlg = -1;
    if (recovery->current.script && (!script_name_build_dlg_name(recovery->current.script, path, sizeof(path)) || !dialog_load(path, &dlg))) return false;
    memset(entry, 0, sizeof(*entry));
    entry->dlg = entry->state.dlg = dlg;
    entry->script_num = recovery->current.script;
    entry->script_line = recovery->current.line;
    DialogState* state = &entry->state;
    state->pc_obj = pc; state->npc_obj = npc;
    sub_443EB0(pc, &state->field_10); sub_443EB0(npc, &state->field_40);
    state->num = recovery->current.page;
    state->script_num = recovery->state_script;
    state->field_17E8 = (int)recovery->mode;
    state->field_17EC = recovery->last_reply;
    state->speech_id = recovery->speech;
    state->field_1840 = recovery->response;
    state->seed = recovery->seed;
    state->num_options = (int)recovery->options;
    memcpy(state->reply, recovery->reply, sizeof(state->reply));
    coop_language_remember(state->reply,&recovery->text_refs[0]);
    state->text_refs[0]=recovery->text_refs[0];
    for (unsigned i = 0; i < recovery->options; i++) {
        memcpy(state->options[i], recovery->text[i], sizeof(state->options[i]));
        coop_language_remember(state->options[i],&recovery->text_refs[i+2]);
        state->text_refs[i+1]=recovery->text_refs[i+2];
        state->field_17F0[i] = recovery->target[i];
        state->field_1804[i] = recovery->argument[i];
        state->field_1818[i] = recovery->auxiliary[i];
        memcpy(coop_restored_actions[i], recovery->actions[i], sizeof(coop_restored_actions[i]));
        state->actions[i] = coop_restored_actions[i][0] ? coop_restored_actions[i] : NULL;
    }
    /* Entry effects, service preparation and random option selection already
     * happened. Reattach the reaction context and publish the retained view;
     * its chooser rebuilds the native panel without repeating those effects. */
    sub_4C1020(npc, pc);
    entry->field_1850 = true;
    dialog_ui_display(entry);
    return !coop_engine_dialogue_failed();
}
void dialog_ui_coop_stop(void)
{
    if (stru_66DAB8[0].field_1850) {
        /* Release native dialog/reaction resources without publishing teardown. */
        sub_5679C0(&stru_66DAB8[0]);
    }
    CoopDialogueView closed = coop_view;
    closed.active = false;
    closed.option_count = 0;
    closed.reply[0] = closed.selected[0] = 0;
    dialog_ui_coop_present(&closed, false);
    coop_dialogue_ui_reset();
    dialog_ui_speech_stop();
}

void dialog_ui_coop_float(int64_t npc, int64_t pc, const char* text, int speech)
{
    if (!npc) return;
    tb_remove(npc);
    tb_add(npc, obj_field_int32_get(npc, OBJ_F_TYPE) == OBJ_TYPE_PC ? TB_TYPE_GREEN : TB_TYPE_WHITE, text);
    tb_expire_in(npc, TB_EXPIRE_DEFAULT);
    dialog_ui_speech_start(npc, pc, speech);
}

void dialog_ui_coop_panel_closed(unsigned mode)
{
    if (coop_presenting || !coop_visible || !coop_chooser || !coop_view.active || coop_view.mode != mode) return;
    bool ours = mode == 5 ? coop_sheet_presented : mode == 6 && coop_map_presented;
    if (!ours) return;
    if (mode == 5)
        coop_sheet_presented = false;
    else
        coop_map_presented = false;
    if (coop_runtime_gameplay())
        coop_runtime_service_close();
    else
        coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
}
