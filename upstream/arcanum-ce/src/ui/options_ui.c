#include "coop/localization.h"
#include "coop/modern.h"
#include "ui/options_ui.h"
#include "coop/campaign.h"
#include "coop/display.h"
#include "coop/engine_options.h"
#include "coop/runtime.h"
#include "ui/intgame.h"

#include "game/anim.h"
#include "game/combat.h"
#include "game/gamelib.h"
#include "game/location.h"
#include "game/mes.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/skill.h"
#include "game/tf.h"
#include "ui/cyclic_ui.h"
#include "ui/gameuilib.h"

#define MAX_CONTROLS 8

typedef void (*OptionsUiControlValueGetter)(int* value_ptr, bool* enabled_ptr);
typedef void (*OptionsUiControlValueSetter)(int value);

typedef struct OptionsUiControlInfo {
    /* 0000 */ bool active;
    /* 0004 */ CyclicUiControlType type;
    /* 0008 */ int max_value;
    /* 000C */ const char* debug_name;
    /* 0010 */ const char* mes;
    /* 0014 */ OptionsUiControlValueGetter getter;
    /* 0018 */ OptionsUiControlValueSetter setter;
} OptionsUiControlInfo;

typedef struct OptionsUiControlData {
    /* 0000 */ int id;
    /* 0004 */ bool initialized;
} OptionsUiControlData;

static void options_ui_widescreen_get(int* value, bool* enabled);
static void options_ui_widescreen_set(int value);
static void options_ui_scale_get(int* value, bool* enabled);
static void options_ui_scale_set(int value);
static void options_ui_interactions_get(int* value, bool* enabled);
static void options_ui_interactions_set(int value);
static void options_ui_language_get(int* value,bool* enabled);
static void options_ui_language_set(int value);
static const char* language_names[32];
static const char* display_off_on[] = {"Off", "On"};
static const char* display_scales[] = {"75%", "100%", "125%", "150%", "175%", "200%"};
static const char* interaction_speeds[] = {"Walk", "Run"};

static void options_ui_module_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_difficulty_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_difficutly_set(int value);
static void options_ui_violence_filter_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_violence_filter_set(int value);
static void options_ui_combat_mode_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_combat_mode_set(int value);
static void options_ui_auto_attack_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_auto_attack_set(int value);
static void options_ui_auto_switch_weapons_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_auto_switch_weapons_set(int value);
static void options_ui_awlays_run_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_always_run_set(int value);
static void options_ui_follower_skills_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_follower_skills_set(int value);
static void options_ui_brightness_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_brightness_set(int value);
static void options_ui_text_duration_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_text_duration_set(int value);
static void options_ui_floats_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_floats_set(int value);
static void options_ui_float_speed_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_float_speed_set(int value);
static void options_ui_combat_taunts_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_combat_taunts_set(int value);
static void options_ui_effects_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_effects_volume_set(int value);
static void options_ui_voice_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_voice_volume_set(int value);
static void options_ui_music_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_music_volume_set(int value);

/**
 * 0x5CCD38
 */
static int options_ui_controls_x[MAX_CONTROLS] = {
    261,
    261,
    261,
    261,
    531,
    531,
    531,
    531,
};

/**
 * 0x5CCD58
 */
static int options_ui_controls_y[MAX_CONTROLS] = {
    122,
    197,
    272,
    347,
    122,
    197,
    272,
    347,
};

/**
 * Defines controls metadata.
 *
 * 0x5CCD78
 */
static OptionsUiControlInfo options_ui_tab_controls_meta[OPTIONS_UI_TAB_COUNT][MAX_CONTROLS] = {
    {
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Module", "", options_ui_module_get, NULL },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Difficulty", "mes\\OptionsDifficulty.mes", options_ui_difficulty_get, options_ui_difficutly_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Violence Filter", "mes\\OptionsOffOn.mes", options_ui_violence_filter_get, options_ui_violence_filter_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Default Combat Mode", "mes\\OptionsCombatMode.mes", options_ui_combat_mode_get, options_ui_combat_mode_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Auto Attack", "mes\\OptionsOffOn.mes", options_ui_auto_attack_get, options_ui_auto_attack_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Auto Switch Weapons", "mes\\OptionsOffOn.mes", options_ui_auto_switch_weapons_get, options_ui_auto_switch_weapons_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Always Run", "mes\\OptionsOffOn.mes", options_ui_awlays_run_get, options_ui_always_run_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Follower Skills", "mes\\OptionsOffOn.mes", options_ui_follower_skills_get, options_ui_follower_skills_set },
    },
    {
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 9, "Brightness", "", options_ui_brightness_get, options_ui_brightness_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Text Duration", "", options_ui_text_duration_get, options_ui_text_duration_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Floats", "mes\\OptionsFloats.mes", options_ui_floats_get, options_ui_floats_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Float Speed", "", options_ui_float_speed_get, options_ui_float_speed_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Combat Taunts", "mes\\OptionsOffOn.mes", options_ui_combat_taunts_get, options_ui_combat_taunts_set },
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Widescreen (restart)", "", options_ui_widescreen_get, options_ui_widescreen_set },
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "UI size (restart)", "", options_ui_scale_get, options_ui_scale_set },
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Approach interactions", "", options_ui_interactions_get, options_ui_interactions_set },
    },
    {
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Effects", "", options_ui_effects_volume_get, options_ui_effects_volume_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Voice", "", options_ui_voice_volume_get, options_ui_voice_volume_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Music", "", options_ui_music_volume_get, options_ui_music_volume_set },
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Language (restart)", "", options_ui_language_get, options_ui_language_set },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
    },
};

/**
 * Flag indicating if the options UI is displayed in-game.
 *
 * This flag controls availability of the "Module" control.
 *
 * 0x687260
 */
static bool options_ui_in_play;

/**
 * List of available game modules.
 *
 * 0x687268
 */
static GameModuleList options_ui_modlist;

/**
 * "options_ui.mes"
 *
 * 0x687274
 */
static mes_file_handle_t options_ui_mes_file;

/**
 * Runtime data for controls.
 *
 * 0x687278
 */
static OptionsUiControlData options_ui_controls[MAX_CONTROLS];

/**
 * Flag indicating whether the options UI has been started.
 *
 * 0x6872B8
 */
static bool options_ui_started;

/**
 * Flag indicating if the module list is initialized.
 *
 * 0x6872BC
 */
static bool options_ui_modlist_initialized;

/**
 * Opens the options UI at a specified tab.
 *
 * NOTE: This function and entire module is a little bit strange, it's
 * implementation does not match other UI modules. This function does not create
 * its own window to display options, instead the window handle is passed by the
 * caller. The caller is responsible for drawing the background. This function
 * only deals with controls.
 *
 * 0x589280
 */
void options_ui_start(OptionsUiTab tab, tig_window_handle_t window_handle, bool in_play)
{
    int index;
    CyclicUiControlInfo control_info;
    OptionsUiControlInfo* meta;
    MesFileEntry mes_file_entry;
    int value;

    // Close fate UI if it's currently visible.
    if (options_ui_started) {
        options_ui_close();
    }

    options_ui_in_play = in_play;

    // Load control titles.
    mes_load("mes\\options_ui.mes", &options_ui_mes_file);

    for (index = 0; index < MAX_CONTROLS; index++) {
        meta = &(options_ui_tab_controls_meta[tab][index]);
        if (!meta->active) {
            continue;
        }

        // Initialize cyclic UI control.
        cyclic_ui_control_init(&control_info);

        // Special case - the "Module" control must be turned off when options
        // are opened in the middle of the game.
        if (tab == OPTIONS_UI_TAB_GAME && index == 0) {
            if (options_ui_in_play) {
                options_ui_modlist_initialized = false;
                continue;
            }

            // Load available modules and configure control appropriately.
            gamelib_modlist_create(&options_ui_modlist, 0);
            control_info.text_array = (const char* const*)options_ui_modlist.paths;
            control_info.text_array_size = options_ui_modlist.count;

            options_ui_modlist_initialized = true;
        }

        // Set up control.
        control_info.type = meta->type;
        control_info.window_handle = window_handle;
        control_info.max_value = meta->max_value;

        // Load control title from the message file.
        mes_file_entry.num = index + 100 * (tab + 1);
        control_info.title = mes_search(options_ui_mes_file, &mes_file_entry)
            ? mes_file_entry.str
            : meta->debug_name;
        if (tab == OPTIONS_UI_TAB_VIDEO && index >= 5) {
            control_info.title = meta->debug_name;
            control_info.text_array = index == 5 ? display_off_on : index == 6 ? display_scales : interaction_speeds;
            control_info.text_array_size = index == 6 ? 6 : 2;
        }
        if(tab==OPTIONS_UI_TAB_AUDIO&&index==3) {
            unsigned count=SDL_min(coop_language_count(),SDL_arraysize(language_names));
            for(unsigned i=0;i<count;i++)language_names[i]=coop_tr(coop_language_name(i));
            control_info.title=coop_tr("Language (restart)");
            control_info.text_array=language_names;control_info.text_array_size=count;
        }
        control_info.title=coop_tr(control_info.title);

        // Finish control set up.
        control_info.mes_file_path = meta->mes;
        control_info.value_changed_callback = meta->setter;
        control_info.x = options_ui_controls_x[index];
        control_info.y = options_ui_controls_y[index];

        // Query initial value.
        if (meta->getter != NULL) {
            meta->getter(&value, &(control_info.enabled));
        } else {
            value = 0;
            control_info.enabled = false;
        }

        // Create the control and set its initial value.
        if (cyclic_ui_control_create(&control_info, &(options_ui_controls[index].id))) {
            options_ui_controls[index].initialized = true;
            cyclic_ui_control_set(options_ui_controls[index].id, value);
        } else {
            options_ui_controls[index].initialized = false;
        }
    }

    // Special case - when options are opened in the middle of the game, center
    // viewport on the player, so that PC lens displays correct surroundings.
    if (options_ui_in_play) {
        location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    }

    options_ui_started = true;
    coop_play_settings_link(true);
}

/**
 * Loads the selected game module.
 *
 * 0x589430
 */
bool options_ui_load_module(void)
{
    unsigned int selected;

    if (!options_ui_started
        || !options_ui_modlist_initialized
        || options_ui_in_play) {
        return true;
    }

    selected = cyclic_ui_control_get(options_ui_controls[0].id);
    if (selected == options_ui_modlist.selected) {
        return true;
    }

    if (!gamelib_mod_load(options_ui_modlist.paths[selected])
        || !gameuilib_mod_load()) {
        tig_debug_printf("Can't load module %s\n", options_ui_modlist.paths[selected]);
        return false;
    }

    return true;
}

/**
 * Closes the options UI.
 *
 * 0x5894C0
 */
void options_ui_close(void)
{
    coop_play_settings_link(false);
    int index;

    if (!options_ui_started) {
        return;
    }

    // Destroy initialized controls.
    for (index = 0; index < MAX_CONTROLS; index++) {
        if (options_ui_controls[index].initialized) {
            cyclic_ui_control_destroy(options_ui_controls[index].id, false);
            options_ui_controls[index].initialized = false;
        }
    }

    // Destroy module list.
    if (options_ui_modlist_initialized) {
        gamelib_modlist_destroy(&options_ui_modlist);
        options_ui_modlist_initialized = false;
    }

    mes_unload(options_ui_mes_file);

    options_ui_started = false;
}

/**
 * Handles button press event for the options UI.
 *
 * This function delegates handling to the cyclic UI system. Returns `true`
 * if the specified button is one of left/right buttons (in this case the
 * event should be considered as processed), `false` otherwise.
 *
 * 0x589530
 */
bool options_ui_handle_button_pressed(tig_button_handle_t button_handle)
{
    // Delegate button handling to the cyclic UI system.
    return cyclic_ui_handle_button_pressed(button_handle);
}

/**
 * Called to retrieve the initial value and state of "Module".
 *
 * 0x589540
 */
void options_ui_module_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = options_ui_modlist.selected;
    *enabled_ptr = !options_ui_in_play;
}

/**
 * Called to retrieve the initial value and state of "Difficulty".
 *
 * 0x589560
 */
void options_ui_difficulty_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = gamelib_game_difficulty_get();
    *enabled_ptr = !coop_campaign_active() || (coop_runtime_session() && coop_session_is_host(coop_runtime_session())
        && coop_runtime_phase() == COOP_RUNTIME_PLAYING && !coop_runtime_paused());
}

/**
 * Called when the value of "Difficulty" is changed.
 *
 * 0x589580
 */
void options_ui_difficutly_set(int value)
{
    if (value < 0 || value > 2) return;
    if (coop_campaign_active()) {
        if (!coop_runtime_difficulty((unsigned)value)) intgame_message_window_display_feedback(coop_tr("Only the host can change difficulty while the party is ready."));
        return;
    }
    settings_set_value(&settings, DIFFICULTY_KEY, value);
}

static bool options_ui_personal(unsigned option, int value)
{
    if (!coop_campaign_active()) return true;
    if (coop_runtime_player_option(option, value != 0)) return true;
    intgame_message_window_display_feedback(coop_tr("Finish the current party action before changing character options."));
    return false;
}

/**
 * Called to retrieve the initial value and state of "Violence Filter".
 *
 * 0x5895A0
 */
void options_ui_violence_filter_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, VIOLENCE_FILTER_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Violence Filter" is changed.
 *
 * 0x5895D0
 */
void options_ui_violence_filter_set(int value)
{
    settings_set_value(&settings, VIOLENCE_FILTER_KEY, value ? 1 : 0);
}

/**
 * Called to retrieve the initial value and state of "Combat Mode".
 *
 * 0x589610
 */
void options_ui_combat_mode_get(int* value_ptr, bool* enabled_ptr)
{
    if (combat_is_turn_based()) {
        *value_ptr = combat_fast_turn_based_get() ? 2 : 1;
    } else {
        *value_ptr = 0;
    }
    *enabled_ptr = !coop_campaign_active() || (coop_runtime_session() && coop_session_is_host(coop_runtime_session())
        && coop_runtime_phase() == COOP_RUNTIME_PLAYING && !coop_runtime_paused());
}

/**
 * Called when the value of "Combat Mode" is changed.
 *
 * 0x5896A0
 */
void options_ui_combat_mode_set(int value)
{
    if (coop_campaign_active()) {
        if (value < 0 || value > 2 || !coop_runtime_combat(COOP_COMBAT_POLICY, (uint8_t)value, OBJ_HANDLE_NULL))
            intgame_message_window_display_feedback(coop_tr("Only the host can change combat mode while the party is ready."));
        return;
    }
    switch (value) {
    case 0:
        settings_set_value(&settings, TURN_BASED_KEY, 0);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 0);
        break;
    case 1:
        settings_set_value(&settings, TURN_BASED_KEY, 1);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 0);
        break;
    case 2:
        settings_set_value(&settings, TURN_BASED_KEY, 1);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 1);
        break;
    }
}

/**
 * Called to retrieve the initial value and state of "Auto Attack".
 *
 * 0x589710
 */
void options_ui_auto_attack_get(int* value_ptr, bool* enabled_ptr)
{
    int preference = coop_engine_options_get(player_get_local_pc_obj(), COOP_OPTION_AUTO_ATTACK);
    *value_ptr = preference >= 0 ? preference : combat_auto_attack_get(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Auto Attack" is changed.
 *
 * 0x589740
 */
void options_ui_auto_attack_set(int value)
{
    if (!options_ui_personal(COOP_OPTION_AUTO_ATTACK, value)) return;
    combat_auto_attack_set(value);
}

/**
 * Called to retrieve the initial value and state of "Auto Switch Weapons".
 *
 * 0x589750
 */
void options_ui_auto_switch_weapons_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = combat_auto_switch_weapons_get(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Auto Switch Weapons" is changed.
 *
 * 0x589780
 */
void options_ui_auto_switch_weapons_set(int value)
{
    if (!options_ui_personal(COOP_OPTION_SWITCH_WEAPON, value)) return;
    combat_auto_switch_weapons_set(value);
}

/**
 * Called to retrieve the initial value and state of "Always Run".
 *
 * 0x589790
 */
void options_ui_awlays_run_get(int* value_ptr, bool* enabled_ptr)
{
    int preference = coop_engine_options_get(player_get_local_pc_obj(), COOP_OPTION_ALWAYS_RUN);
    *value_ptr = preference >= 0 ? preference : get_always_run(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Always Run" is changed.
 *
 * 0x5897C0
 */
void options_ui_always_run_set(int value)
{
    if (!options_ui_personal(COOP_OPTION_ALWAYS_RUN, value)) return;
    set_always_run(value);
}

/**
 * Called to retrieve the initial value and state of "Follower Skills".
 *
 * 0x5897D0
 */
void options_ui_follower_skills_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = get_follower_skills(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Follower Skills" is changed.
 *
 * 0x589800
 */
void options_ui_follower_skills_set(int value)
{
    if (!options_ui_personal(COOP_OPTION_FOLLOWER_SKILLS, value)) return;
    set_follower_skills(value);
}

/**
 * Called to retrieve the initial value and state of "Brightness".
 *
 * 0x589810
 */
void options_ui_brightness_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, BRIGHTNESS_KEY);
    *enabled_ptr = tig_video_check_gamma_control() == TIG_OK;
}

/**
 * Called when the value of "Brightness" is changed.
 *
 * 0x589840
 */
void options_ui_brightness_set(int value)
{
    settings_set_value(&settings, BRIGHTNESS_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Text Duration".
 *
 * 0x589860
 */
void options_ui_text_duration_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, TEXT_DURATION_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Text Duration" is changed.
 *
 * 0x589880
 */
void options_ui_text_duration_set(int value)
{
    settings_set_value(&settings, TEXT_DURATION_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Floats".
 *
 * 0x5898A0
 */
void options_ui_floats_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = tf_level_get();
    *enabled_ptr = true;
}

/**
 * Called when the value of "Floats" is changed.
 *
 * 0x5898C0
 */
void options_ui_floats_set(int value)
{
    if (tf_level_get() != value) {
        tf_level_set(value);
    }
}

/**
 * Called to retrieve the initial value and state of "Float Speed".
 *
 * 0x5898E0
 */
void options_ui_float_speed_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, FLOAT_SPEED_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Float Speed" is changed.
 *
 * 0x589900
 */
void options_ui_float_speed_set(int value)
{
    settings_set_value(&settings, FLOAT_SPEED_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Combat Taunts".
 *
 * 0x589920
 */
void options_ui_combat_taunts_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, COMBAT_TAUNTS_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Combat Taunts" is changed.
 *
 * 0x589940
 */
void options_ui_combat_taunts_set(int value)
{
    settings_set_value(&settings, COMBAT_TAUNTS_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Effects".
 *
 * 0x589960
 */
void options_ui_effects_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, EFFECTS_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Effects" is changed.
 *
 * 0x589980
 */
void options_ui_effects_volume_set(int value)
{
    settings_set_value(&settings, EFFECTS_VOLUME_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Voice".
 *
 * 0x5899A0
 */
void options_ui_voice_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, VOICE_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Voice" is changed.
 *
 * 0x5899C0
 */
void options_ui_voice_volume_set(int value)
{
    settings_set_value(&settings, VOICE_VOLUME_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Music".
 *
 * 0x5899E0
 */
void options_ui_music_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, MUSIC_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Music" is changed.
 *
 * 0x589A00
 */
void options_ui_music_volume_set(int value)
{
    settings_set_value(&settings, MUSIC_VOLUME_KEY, value);
}

static void options_ui_widescreen_get(int* value, bool* enabled)
{ *value = coop_display_widescreen(); *enabled = true; }
static void options_ui_scale_get(int* value, bool* enabled)
{ *value = (coop_display_ui_scale() - 75) / 25; *enabled = true; }
static void options_ui_interactions_get(int* value, bool* enabled)
{
    int own = coop_engine_options_get(player_get_local_pc_obj(), COOP_OPTION_RUN_INTERACTIONS);
    *value = own >= 0 ? own : coop_display_interactions_run(); *enabled = true;
}
static void display_setting(bool wide, int scale, bool run)
{
    if (!coop_display_settings_set(wide, scale, run))
        intgame_message_window_display_feedback(coop_tr("Could not save display preferences. Check that the game folder is writable."));
}
static void options_ui_widescreen_set(int value)
{ display_setting(value != 0, coop_display_ui_scale(), coop_display_interactions_run()); }
static void options_ui_scale_set(int value)
{ if (value >= 0 && value < 6) display_setting(coop_display_widescreen(), 75 + value * 25, coop_display_interactions_run()); }
static void options_ui_interactions_set(int value)
{
    if (!options_ui_personal(COOP_OPTION_RUN_INTERACTIONS, value != 0)) return;
    display_setting(coop_display_widescreen(), coop_display_ui_scale(), value != 0);
}
static void options_ui_language_get(int* value,bool* enabled)
{ *value=(int)coop_language_pending();*enabled=true; }
static void options_ui_language_set(int value)
{ if(value<0)return;
    if(!coop_language_available((unsigned)value))intgame_message_window_display_feedback(coop_tr("Install the selected language pack before choosing it."));
    else if(!coop_language_select((unsigned)value))intgame_message_window_display_feedback(coop_tr("Could not save language settings.")); }
