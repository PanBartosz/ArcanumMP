#include "coop/campaign.h"
#include "coop/dialogue_ui.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_documents.h"
#include "coop/engine_environment.h"
#include "coop/engine_journal.h"
#include "coop/engine_object.h"
#include "coop/engine_probe.h"
#include "coop/engine_script.h"
#include "coop/engine_snapshot.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "coop/time_math.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/area.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/fate.h"
#include "game/gamelib.h"
#include "game/gfade.h"
#include "game/gsound.h"
#include "game/item.h"
#include "game/jumppoint.h"
#include "game/light_scheme.h"
#include "game/location.h"
#include "game/map.h"
#include "game/mes.h"
#include "game/monstergen.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/portal.h"
#include "game/proto.h"
#include "game/quest.h"
#include "game/reputation.h"
#include "game/roof.h"
#include "game/rumor.h"
#include "game/script.h"
#include "game/script_name.h"
#include "game/sector.h"
#include "game/sector_script.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/snd.h"
#include "game/stat.h"
#include "game/tile.h"
#include "game/tile_script.h"
#include "game/townmap.h"
#include "tig/movie.h"
#include "ui/charedit_ui.h"
#include "ui/dialog_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/mainmenu_ui.h"
#include "ui/slide_ui.h"
#include "ui/tb_ui.h"
#include "ui/wmap_ui.h"
#include "ui/written_ui.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { CAMPAIGN_PHASES = 22,
    REGIONAL_PHASES = 47,
    QUEST_PHASES = 60,
    VOYAGE_PHASES = 73,
    FOREST_PHASES = 96,
    DARK_PHASES = 123,
    LEGACY_FIRST = 124,
    LEGACY_PHASES = 169,
    FINALE_FIRST = 170,
    FINALE_PHASES = 177,
    TRAVERSAL_FIRST = 178,
    TRAVERSAL_PHASES = 192,
    RESCUE_FIRST = 193,
    RESCUE_PHASES = 206,
    COMBAT_FINALE_FIRST = 207,
    COMBAT_FINALE_PHASES = 213,
    EVIL_FIRST = 214,
    EVIL_PHASES = 243,
    EVIL_ENDING_FIRST = 244,
    EVIL_ENDING_PHASES = 280,
    TAG = 189900,
    SCRIPT_PARENT = 999990,
    SCRIPT_CHILD,
    SCRIPT_CURRENT,
    SCRIPT_TARANT,
    SCRIPT_SECTOR };
static CoopSession* held;
static int bindings[4] = { -1, -1, -1, -1 };
static unsigned current_phase, retail_choices, script_loads;
static bool tail_mode, regional_mode, quest_mode, voyage_mode, forest_mode, dark_mode, legacy_mode;
static bool finale_mode, traversal_mode, traversal_refused, traversal_events, rescue_mode, combat_finale_mode;
static bool evil_mode, evil_ending_mode;
static bool voyage_seen;
static ObjectID voyage_companion;
static unsigned queued_owners;
static bool bates_seen, pillar_seen;
static unsigned final_phase = CAMPAIGN_PHASES;
static unsigned pcm_phases, save_faults, retail_exits;
static unsigned first_phase;
static int64_t pc(unsigned slot)
{
    int member = coop_session_is_host(held) ? coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity) : bindings[slot];
    return member < 0 ? 0 : coop_campaign_object((unsigned)member);
}
static int script_number(int64_t object, int sap)
{
    Script script = { 0 };
    obj_arrayfield_script_get(object, OBJ_F_SCRIPTS_IDX, sap, &script);
    return script.num;
}
static int64_t tagged(bool door)
{
    int64_t object;
    int it;
    if (obj_inst_first(&object, &it)) do {
            int type = obj_field_int32_get(object, OBJ_F_TYPE);
            if (type == (door ? OBJ_TYPE_PORTAL : OBJ_TYPE_SCENERY)
                && obj_field_int32_get(object, door ? OBJ_F_PORTAL_PAD_I_1 : OBJ_F_SCENERY_PAD_I_2) == TAG) return object;
        } while (obj_inst_next(&object, &it));
    return 0;
}
static int64_t scripted(int script, int sap)
{
    int64_t object;
    int it;
    if (obj_inst_first(&object, &it)) do {
            if (script_number(object, sap) == script) return object;
        } while (obj_inst_next(&object, &it));
    return 0;
}
static void quiet(void)
{
    if (traversal_mode || rescue_mode || combat_finale_mode || evil_mode) return; /* Retain new native encounters on these routes. */
    int64_t object;
    int it;
    if (obj_inst_first(&object, &it)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || (obj_field_int32_get(object, OBJ_F_FLAGS) & OF_OFF)) continue;
            if ((forest_mode || dark_mode || legacy_mode) && critter_pc_leader_get(object)) continue;
            int script = script_number(object, SAP_DIALOG);
            if (legacy_mode && (script == 1073 || script == 1074 || script == 1090 || script == 1091 || script == 1108 || script == 1890 || script == 1921 || script == 1171 || (finale_mode && (script == 1116 || script == 1111 || script == 1105 || script_number(object, SAP_DYING) == 2972)) || script_number(object, SAP_HEARTBEAT) == 3022 || script_number(object, SAP_FIRST_HEARTBEAT) == 3022 || script_number(object, SAP_HEARTBEAT) == 2523 || script_number(object, SAP_FIRST_HEARTBEAT) == 2523 || script_number(object, SAP_HEARTBEAT) == 1173 || script_number(object, SAP_FIRST_HEARTBEAT) == 1173)) continue;
            if (dark_mode && (script == 1498 || script == 1078 || script == 1086 || script == 1098 || script == 1096 || script == 1087 || script == 1074 || script_number(object, SAP_HEARTBEAT) == 2782 || script_number(object, SAP_FIRST_HEARTBEAT) == 2782)) continue;
            if (forest_mode && (script == 1073 || script == 1074 || script == 1076 || script == 1079 || script == 1080 || script == 1158)) continue;
            if (voyage_mode && (script == 1061 || script == 1066 || script == 1067 || script == 1071 || script == 1158)) continue;
            if (quest_mode && !voyage_mode && !forest_mode && !dark_mode && !legacy_mode && (script == 1047 || script == 1168)) continue;
            if (current_phase == 13 && (script == 1049 || script == 1054 || script == 1055 || obj_field_int32_get(object, OBJ_F_NAME) == 6412)) continue;
            object_flags_set(object, OF_OFF);
        } while (obj_inst_next(&object, &it));
}
static bool script_file(int number, const ScriptAction* actions, unsigned count, unsigned flags)
{
    char path[100];
    snprintf(path, sizeof(path), "data/scr/%darcmp-progress.scr", number);
    uint8_t header[64] = { 0 };
    memcpy(header + 8, "ARCMP private campaign integration", 34);
    header[48] = (uint8_t)flags;
    header[52] = (uint8_t)count;
    header[56] = (uint8_t)count;
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    bool good = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    for (unsigned i = 0; i < count; i++) {
        ScriptCondition condition = { .type = SCT_TRUE, .action = actions[i] };
        good &= fwrite(&condition, 1, sizeof(condition), file) == sizeof(condition);
    }
    if (fclose(file)) good = false;
    return good;
}
static bool cache_churn(void)
{
    unsigned count = 0;
    char path[TIG_MAX_PATH];
    for (int id = 1000; id < 3000 && count < 220; id++) {
        if (!script_name_build_scr_name(id, path, sizeof(path))) continue;
        Script script = { .num = id };
        ScriptFlags flags = 0;
        if (!script_flags(&script, &flags)) return false;
        count++;
    }
    unsigned entries, locks;
    script_cache_status(&entries, &locks);
    fprintf(stderr, "ARCMP progress script cache loaded=%u entries=%u locks=%u\n", count, entries, locks);
    script_loads = count;
    script_global_var_set(1985, (int)count);
    return count == 220 && entries <= 100 && !locks;
}
static bool place_members_near_radius(int64_t target, unsigned members, int minimum)
{
    if (!target) return false;
    int64_t anchor = obj_field_int64_get(target, OBJ_F_LOCATION), positions[4];
    unsigned count = 0;
    map_precache_sectors(anchor);
    for (unsigned i = 0; i < 4; i++) {
        if (!(members & (1u << i))) continue;
        bool found = false;
        for (int radius = minimum ? minimum : obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? 1
                                                                                                            : 4;
            radius < 14 && !found; radius++)
            for (int dy = -radius; dy <= radius && !found; dy++)
                for (int dx = -radius; dx <= radius && !found; dx++) {
                    if (abs(dx) != radius && abs(dy) != radius) continue;
                    int64_t x = location_get_x(anchor) + dx, y = location_get_y(anchor) + dy, max_x, max_y;
                    location_limits_get(&max_x, &max_y);
                    if (x < 1 || y < 1 || x >= max_x || y >= max_y) continue;
                    int64_t from = location_make(x, y);
                    if (tile_is_blocking(from, false)) continue;
                    bool separate = true;
                    for (unsigned j = 0; j < count; j++)
                        if (location_dist(from, positions[j]) < 2) separate = false;
                    if (!separate) continue;
                    int8_t rotations[128];
                    /* Exact pathing includes the last wall/portal edge. An
                     * interaction path may otherwise stop across that wall. */
                    unsigned flags = (unsigned)obj_field_int32_get(target, OBJ_F_FLAGS);
                    object_flags_set(target, OF_NO_BLOCK);
                    PathCreateInfo path = { .obj = pc(i), .from = from, .to = anchor, .max_rotations = 128, .rotations = rotations, .flags = obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_NPC ? PATH_FLAG_0x0001 : 0 };
                    int steps = path_make(&path);
                    obj_field_int32_set(target, OBJ_F_FLAGS, (int)flags);
                    if (steps <= 0) continue;
                    sub_43E770(pc(i), from, 0, 0);
                    positions[count++] = from;
                    found = true;
                }
        if (!found) {
            fprintf(stderr, "ARCMP progress placement failed member=%u target-type=%d at=%lld\n", i,
                obj_field_int32_get(target, OBJ_F_TYPE), (long long)anchor);
            return false;
        }
    }
    return true;
}
static bool place_members_near(int64_t target, unsigned members) { return place_members_near_radius(target, members, 0); }
static bool place_near(int64_t target) { return place_members_near(target, 15); }
static bool visual_changes(bool restore)
{
    int64_t id;
    Sector* sector;
    if (restore) {
        id = SECTOR_MAKE(script_global_var_get(1986), script_global_var_get(1987));
    } else {
        id = sector_id_from_loc(obj_field_int64_get(pc(0), OBJ_F_LOCATION));
        script_global_var_set(1986, (int)SECTOR_X(id));
        script_global_var_set(1987, (int)SECTOR_Y(id));
    }
    if (!sector_lock(id, &sector)) return false;
    for (SectorBlockListNode* node = sector->lights.head; node;) {
        SectorBlockListNode* next = node->next;
        Light* light = node->data;
        if (sub_4DD110(light)) {
            sector_light_list_remove(&sector->lights, light);
            sub_4D9570(light);
        }
        node = next;
    }
    bool good = true;
    if (restore) {
        sector->roofs.art_ids[0] = TIG_ART_ID_INVALID;
    } else {
        tig_art_id_t art = TIG_ART_ID_INVALID, roof = TIG_ART_ID_INVALID;
        good = tig_art_light_id_create(0, 0, 0, 0, &art) == TIG_OK
            && tig_art_roof_id_create(1, TIG_ART_ROOF_PIECE_CENTER, 0, 0, &roof) == TIG_OK;
        Light* light = good ? light_static_create(sector_loc_from_id(id), 17, -9, LF_00000020, art, 0x3179b5) : NULL;
        if (light)
            sector_light_list_add(&sector->lights, light);
        else
            good = false;
        sector->roofs.art_ids[0] = roof;
        sector->roofs.empty = false;
        script_global_var_set(1989, (int)roof);
        script_global_var_set(1990, (int)art);
    }
    sector_unlock(id);
    object_invalidate_rect(NULL);
    return good;
}
static bool visuals_match(void)
{
    int64_t id = SECTOR_MAKE(script_global_var_get(1986), script_global_var_get(1987));
    Sector* sector;
    if (!sector_lock(id, &sector)) return false;
    unsigned count = 0;
    bool good = (sector->roofs.art_ids[0] & ~0x3000u) == (uint32_t)script_global_var_get(1989);
    for (SectorBlockListNode* node = sector->lights.head; node; node = node->next) {
        Light* light = node->data;
        if (!sub_4DD110(light)) continue;
        count++;
        good &= light->art_id == (uint32_t)script_global_var_get(1990) && light->offset_x == 17 && light->offset_y == -9
            && light->r == 0x31 && light->g == 0x79 && light->b == 0xb5;
    }
    sector_unlock(id);
    return good && count == 1;
}
static bool separate_views(void)
{
    int64_t base = sector_id_from_loc(obj_field_int64_get(pc(0), OBJ_F_LOCATION));
    for (unsigned i = 0; i < 4; i++) {
        int64_t id = SECTOR_MAKE(SECTOR_X(base) + (i % 2 ? 3 : -3), SECTOR_Y(base) + (i / 2 ? 3 : -3));
        int64_t origin = sector_loc_from_id(id), loc = location_make(location_get_x(origin) + 34, location_get_y(origin) + 34);
        map_precache_sectors(loc);
        if (tile_is_blocking(loc, false)) return false;
        Sector* sector;
        if (!sector_lock(id, &sector)) return false;
        tig_art_id_t art;
        bool good = tig_art_roof_id_create(1, TIG_ART_ROOF_PIECE_CENTER, 0, 0, &art) == TIG_OK;
        sector->roofs.art_ids[136] = art;
        sector->roofs.empty = false;
        /* Retail scheme 2 intentionally has black outdoor light. Use the
         * visible conveyance-cave scheme for separated wilderness captures. */
        int scheme = i % 2 ? 10 : 1;
        if (!light_scheme_campaign_valid(scheme, light_scheme_get_map_default())) scheme = light_scheme_get_map_default();
        sector->light_scheme = scheme;
        sector->flags |= SECTOR_LIGHT_SCHEME_CHANGED;
        sector_unlock(id);
        if (!good) return false;
        script_global_var_set(1940 + (int)i * 2, (int)location_get_x(loc));
        script_global_var_set(1941 + (int)i * 2, (int)location_get_y(loc));
        sub_43E770(pc(i), loc, 0, 0);
    }
    return true;
}
static bool own_view(unsigned slot)
{
    int64_t own = pc(slot);
    if (!own) return false;
    for (unsigned i = 0; i < 4; i++) {
        int64_t loc = location_make(script_global_var_get(1940 + (int)i * 2), script_global_var_get(1941 + (int)i * 2));
        Sector* sector;
        int64_t id = sector_id_from_loc(loc);
        if (!sector_lock(id, &sector)) return false;
        bool fill = tig_art_roof_id_fill_get(sector->roofs.art_ids[136]);
        sector_unlock(id);
        if (fill != (i == slot)) return false;
    }
    int before_scheme = light_scheme_get();
    tig_color_t before_in = light_get_indoor_color(), before_out = light_get_outdoor_color(), expected_in, expected_out;
    Sector* sector;
    int64_t id = sector_id_from_loc(obj_field_int64_get(own, OBJ_F_LOCATION));
    if (!sector_lock(id, &sector)) return false;
    int scheme = sector->light_scheme;
    sector_unlock(id);
    if (!light_scheme_view_colors(scheme, light_scheme_get_hour(), &expected_in, &expected_out)) return false;
    if (scheme == before_scheme) {
        expected_in = before_in;
        expected_out = before_out;
    }
    coop_engine_environment_view_begin();
    bool good = light_get_indoor_color() == expected_in && light_get_outdoor_color() == expected_out;
    coop_engine_environment_view_end();
    return good && before_scheme == light_scheme_get() && before_in == light_get_indoor_color() && before_out == light_get_outdoor_color();
}
static bool remote_sector_entry(void)
{
    ScriptAction action = { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1982 } };
    if (!script_file(SCRIPT_SECTOR, &action, 1, SF_AUTO_REMOVING)) return false;
    int64_t loc = obj_field_int64_get(pc(2), OBJ_F_LOCATION);
    int64_t next = location_make(location_get_x(loc) + 64, location_get_y(loc));
    map_precache_sectors(next);
    if (tile_is_blocking(next, false)) return false;
    Script script = { .num = SCRIPT_SECTOR };
    int64_t id = sector_id_from_loc(next);
    if (!sector_script_set(id, &script)) return false;
    sub_43E770(pc(2), next, 0, 0);
    /* False denotes an absent/auto-removed script; it still returns its record. */
    sector_script_get(id, &script);
    fprintf(stderr, "ARCMP remote sector entry script=%d strength=%d expected=%d\n", script.num,
        script_global_var_get(1982), stat_level_get(pc(2), STAT_STRENGTH));
    return !script.num && script_global_var_get(1982) == stat_level_get(pc(2), STAT_STRENGTH);
}
static bool reload_visuals(void)
{
    int64_t id = SECTOR_MAKE(script_global_var_get(1986), script_global_var_get(1987));
    sector_flush(0);
    if (sector_loaded(id)) return false;
    return visuals_match();
}
static bool transfer_script(int number, int map, int64_t location, unsigned owner);
static int64_t any_npc(void)
{
    int64_t object;
    int it;
    if (obj_inst_first(&object, &it)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC) return object;
        } while (obj_inst_next(&object, &it));
    return 0;
}
static bool region_sound_prepare(void)
{
    mes_file_handle_t index, list;
    if (!mes_load("sound\\SchemeIndex.mes", &index)) return false;
    if (!mes_load("sound\\SchemeList.mes", &list)) {
        mes_unload(index);
        return false;
    }
    int music[2] = { 0 }, ambient = 0;
    for (int i = 0; i < mes_entries_count(index); i++) {
        MesFileEntry entry;
        if (!mes_get_entry(index, i, &entry) || entry.num <= 0) continue;
        char* hash = strchr(entry.str, '#');
        if (!hash) continue;
        int base = atoi(hash + 1);
        bool loop = false, noise = false, song = false;
        for (int j = 0; j < 100; j++) {
            MesFileEntry sound = { .num = base + j };
            if (!mes_search(list, &sound)) continue;
            bool singing = strstr(sound.str, "/loop") || strstr(sound.str, "/anchor") || strstr(sound.str, "/over");
            song |= singing;
            noise |= !singing;
            loop |= strstr(sound.str, "/loop") && !strstr(sound.str, "/time:");
        }
        if (loop && !music[0])
            music[0] = entry.num;
        else if (loop && !music[1])
            music[1] = entry.num;
        if (!song && noise && !ambient) ambient = entry.num;
    }
    mes_unload(index);
    mes_unload(list);
    if (!music[0] || !music[1] || !ambient || !separate_views()) return false;
    script_global_var_set(1950, music[0]);
    script_global_var_set(1951, music[1]);
    script_global_var_set(1952, ambient);
    for (unsigned i = 0; i < 4; i++) {
        int64_t id = sector_id_from_loc(obj_field_int64_get(pc(i), OBJ_F_LOCATION));
        Sector* sector;
        if (!sector_lock(id, &sector)) return false;
        sector->sounds.music_scheme_idx = music[i % 2];
        sector->sounds.ambient_scheme_idx = ambient;
        sector->sounds.flags |= 0x08000000u;
        sector_unlock(id);
    }
    gsound_play_map_scheme(0, 0);
    fprintf(stderr, "ARCMP regional audio retail schemes=%d,%d ambient=%d\n", music[0], music[1], ambient);
    return true;
}
static bool region_sound_move(void)
{
    int64_t loc = obj_field_int64_get(pc(2), OBJ_F_LOCATION), dest = location_make(location_get_x(loc) + 64, location_get_y(loc));
    map_precache_sectors(dest);
    if (tile_is_blocking(dest, false)) return false;
    Sector* sector;
    int64_t id = sector_id_from_loc(dest);
    if (!sector_lock(id, &sector)) return false;
    sector->sounds.music_scheme_idx = script_global_var_get(1951);
    sector->sounds.ambient_scheme_idx = script_global_var_get(1952);
    sector->sounds.flags |= 0x08000000u;
    sector_unlock(id);
    sub_43E770(pc(2), dest, 0, 0);
    return true;
}
static bool scripted_music(void)
{
    ScriptAction action = { .type = SAT_PLAY_SOUND_SCHEME, .op_type = { SVT_NUMBER, SVT_NUMBER }, .op_value = { 0, 0 } };
    action.op_value[0] = script_global_var_get(1950);
    action.op_value[1] = script_global_var_get(1952);
    if (!script_file(999974, &action, 1, 0)) return false;
    Script script = { .num = 999974 };
    ScriptInvocation call = { .script = &script, .triggerer_obj = pc(1), .attachment_point = SAP_USE };
    script_execute(&call);
    CoopEnvironmentState state = { 0 };
    gsound_campaign_state_get(&state);
    return state.scripted_sound && state.sound_members == 15;
}
static bool native_save_failure(void)
{
    char *base, *directory;
    map_paths(&base, &directory);
    (void)base;
    int64_t id = sector_id_from_loc(obj_field_int64_get(pc(0), OBJ_F_LOCATION));
    Sector* sector;
    if (!sector_lock(id, &sector)) return false;
    sector->sounds.music_scheme_idx = script_global_var_get(1951);
    sector->sounds.flags |= 0x08000000u;
    sector_unlock(id);
    char path[TIG_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%" PRId64 ".acsector.tmp", directory, id);
    if (tig_file_exists(path, NULL) || !tig_file_mkdir(path)) return false;
    const char* scratch = "arcmp-progress-save-probe.bin";
    TigFile* stream = tig_file_fopen(scratch, "wb");
    bool refused = stream && !map_save(stream);
    bool good = refused && sector_loaded(id) && coop_campaign_validate();
    if (stream && tig_file_fclose(stream)) good = false;
    bool removed = tig_file_rmdir(path);
    stream = removed ? tig_file_fopen(scratch, "wb") : NULL;
    bool retried = removed && stream && map_save(stream);
    good &= retried;
    if (stream && tig_file_fclose(stream)) good = false;
    good &= tig_file_remove(scratch) == 0;
    if (good) {
        save_faults++;
        script_global_var_set(1953, 1);
    }
    fprintf(stderr, "ARCMP native sector save refusal/retry %s refused=%d removed=%d retried=%d\n", good ? "PASS" : "FAIL", refused, removed, retried);
    return good;
}
static int region_exit(unsigned phase)
{
    switch (phase) {
    case 35:
        return 1212;
    case 36:
        return 2304;
    case 38:
        return 1411;
    case 40:
        return 2484;
    case 41:
        return 2485;
    case 43:
        return 2362;
    case 44:
        return 2363;
    case 45:
        return 2380;
    case 46:
        return 2388;
    case 47:
        return 2389;
    default:
        return 0;
    }
}
static int region_after(unsigned phase)
{
    if (phase == 34 || phase == 35) return 7;
    if (phase == 36) return 1;
    if (phase == 37) return 20;
    if (phase == 38) return 19;
    if (phase == 39 || phase == 41) return 1;
    if (phase == 40) return 59;
    if (phase >= 42 && phase <= 47) return 33;
    return 0;
}
static bool region_transfer(unsigned phase)
{
    int map = region_after(phase);
    int64_t x = 0, y = 0;
    if (!map_get_starting_location(map, &x, &y)) return false;
    if (phase == 34) {
        x = 480;
        y = 224;
    }
    if (phase == 37) {
        x = 100;
        y = 99;
    }
    if (phase == 39) {
        x = 102647;
        y = 116816;
    }
    if (phase == 42) {
        x = 2051;
        y = 2038;
    }
    return transfer_script(999970 + (phase == 34 ? 0 : phase == 37 ? 1
                                   : phase == 39                   ? 2
                                                                   : 3),
        map, location_make(x, y), phase % 4);
}
static int64_t find_attachment(int number, int attachment)
{
    int64_t found = scripted(number, attachment);
    if (found) return found;
    int64_t maxx, maxy;
    sector_limits_get(&maxx, &maxy);
    int64_t center = sector_id_from_loc(obj_field_int64_get(pc(0), OBJ_F_LOCATION));
    for (int radius = 0; radius <= 20; radius++)
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                int64_t x = SECTOR_X(center) + dx, y = SECTOR_Y(center) + dy;
                Sector* sector;
                if (x < 0 || y < 0 || x >= maxx || y >= maxy) continue;
                int64_t id = SECTOR_MAKE(x, y);
                if (!sector_lock(id, &sector)) return 0;
                quiet();
                found = scripted(number, attachment);
                sector_unlock(id);
                if (found) return found;
            }
    fprintf(stderr, "ARCMP regional exit missing script=%d map=%d\n", number, map_current_map());
    return 0;
}
static int64_t find_region_exit(int number) { return find_attachment(number, SAP_USE); }
static bool quest_prepare(unsigned phase)
{
    if (phase == 49 || phase == 58 || phase == 50) {
        int64_t npc = find_attachment(1047, SAP_DIALOG);
        if (!npc) return false;
        object_flags_unset(npc, OF_OFF);
        if (!place_near(npc)) return false;
        if (phase != 50) return true;
    }
    if (phase == 55 || phase == 56) {
        int64_t pillar = find_attachment(1168, SAP_DIALOG);
        if (pillar) object_flags_unset(pillar, OF_OFF);
        return pillar && place_near(pillar);
    }
    if (phase == 51) script_global_var_set(1940, (int)coop_journal_count(coop_engine_journal()));
    if (phase == 52) {
        int64_t npc = scripted(1047, SAP_DIALOG);
        dialog_ui_float_line(npc, pc(3), "This private diagnostic page must disappear after the voted load.", -1);
    }
    if (phase == 50) {
        int64_t npc = scripted(1047, SAP_DIALOG);
        unsigned order[] = { 0, 2, 1, 1, 3 };
        for (unsigned i = 0; i < SDL_arraysize(order); i++)
            object_script_execute(pc(order[i]), npc, 0, SAP_DIALOG, 0);
        Script script = { .num = 1047 };
        ScriptInvocation invocation = { .script = &script, .attachee_obj = npc, .triggerer_obj = pc(3), .attachment_point = SAP_DIALOG };
        if (!script_campaign_line_probe(&invocation, 1054)) return false;
        int64_t loc = obj_field_int64_get(pc(0), OBJ_F_LOCATION);
        ScriptAction action = { .type = SAT_TELEPORT, .op_type = { SFO_PLAYER, SVT_NUMBER, SVT_NUMBER, SVT_NUMBER }, .op_value = { 0, 5012, (int)location_get_x(loc), (int)location_get_y(loc) } };
        if (!script_file(999973, &action, 1, 0)) return false;
        Script trip = { .num = 999973 };
        invocation.script = &trip;
        script_execute(&invocation);
        return coop_runtime_phase() == COOP_RUNTIME_PLAYING && coop_engine_dialogue_pending();
    }
    return true;
}
static bool prepare(void)
{
    quiet();
    for (unsigned i = 0; i < 4; i++) {
        stat_base_set(pc(i), STAT_STRENGTH, 8 + (int)i * 2);
        stat_base_set(pc(i), STAT_INTELLIGENCE, 12);
    }
    ScriptAction child = { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1981 } };
    ScriptAction parent[] = {
        { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1980 } },
        { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1983, 1983, 1 } },
        { .type = SAT_CALL_SCRIPT, .op_type = { SVT_NUMBER, SVT_NUMBER, SFO_ATTACHEE, SFO_ATTACHEE }, .op_value = { SCRIPT_CHILD, 0, 0, 0 } },
        { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1984 } }
    };
    ScriptAction sector = { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1982 } };
    if (!script_file(SCRIPT_PARENT, parent, SDL_arraysize(parent), 0) || !script_file(SCRIPT_CHILD, &child, 1, SF_AUTO_REMOVING)
        || !script_file(SCRIPT_SECTOR, &sector, 1, 0) || !cache_churn()) return false;
    int64_t prototype = 0, object;
    tig_art_id_t scene_art = TIG_ART_ID_INVALID;
    int it;
    if (obj_inst_first(&object, &it)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_SCENERY
                && tig_art_scenery_id_type_get(obj_field_int32_get(object, OBJ_F_CURRENT_AID)) != TIG_ART_SCENERY_TYPE_BEDS) {
                prototype = obj_field_handle_get(object, OBJ_F_PROTOTYPE_HANDLE);
                scene_art = (tig_art_id_t)obj_field_int32_get(object, OBJ_F_CURRENT_AID);
                break;
            }
        } while (obj_inst_next(&object, &it));
    if (!prototype) return false;
    int64_t location = obj_field_int64_get(pc(0), OBJ_F_LOCATION), scene = 0, door = 0;
    if (!object_create(prototype, location_make(location_get_x(location) + 8, location_get_y(location)), &scene)
        || !object_create(sub_4685A0(BP_LIGHT_WOOD_DOOR), location_make(location_get_x(location) + 6, location_get_y(location) + 6), &door)) return false;
    obj_field_int32_set(scene, OBJ_F_AID, scene_art);
    object_set_current_aid(scene, scene_art);
    object_flags_unset(scene, OF_OFF | OF_DONTDRAW | OF_CLICK_THROUGH);
    obj_field_int32_set(scene, OBJ_F_SCENERY_PAD_I_2, TAG);
    obj_field_int32_set(door, OBJ_F_PORTAL_PAD_I_1, TAG);
    Script script = { .num = SCRIPT_PARENT };
    obj_arrayfield_script_set(scene, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
    obj_field_int32_set(door, OBJ_F_PORTAL_FLAGS, 0);
    obj_field_int32_set(door, OBJ_F_CURRENT_AID, tig_art_id_frame_set(obj_field_int32_get(door, OBJ_F_CURRENT_AID), 0));
    for (unsigned i = 0; i < 4; i++) {
        ScriptInvocation invocation = { .triggerer_obj = pc(i) };
        if (coop_engine_script_player(&invocation, pc(0)) != pc(i)) return false;
        int64_t item = item_wield_get(pc(i), ITEM_INV_LOC_ARMOR);
        if (item) {
            invocation = (ScriptInvocation) { .attachee_obj = item };
            if (coop_engine_script_player(&invocation, pc(0)) != pc(i)) return false;
        }
    }
    return place_near(scene);
}
static bool click_object(int64_t object)
{
    if (!object) return false;
    location_origin_set(obj_field_int64_get(object, OBJ_F_LOCATION));
    gamelib_redraw();
    iso_redraw();
    tig_window_display();
    TigRect rect;
    object_get_rect(object, 0, &rect);
    for (int y = rect.y; y < rect.y + rect.height; y += 2)
        for (int x = rect.x; x < rect.x + rect.width; x += 2) {
            TargetDescriptor picked;
            if (x < 0 || x >= 800 || y < 80 || y >= 435 || !target_pick_at_screen_xy(x, y, &picked, false) || picked.is_loc || picked.obj != object) continue;
            TigMessage message = { .type = TIG_MESSAGE_MOUSE };
            message.data.mouse.x = x;
            message.data.mouse.y = y;
            message.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
            tig_message_enqueue(&message);
            message.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
            tig_message_enqueue(&message);
            return true;
        }
    fprintf(stderr, "ARCMP progress click failed type=%d art=%u flags=%u rect=%d,%d,%d,%d\n", obj_field_int32_get(object, OBJ_F_TYPE), (unsigned)obj_field_int32_get(object, OBJ_F_CURRENT_AID), (unsigned)obj_field_int32_get(object, OBJ_F_FLAGS), rect.x, rect.y, rect.width, rect.height);
    tig_video_screenshot_save("arcmp-progress-click-failure.bmp");
    return false;
}
static bool transfer_script(int number, int map, int64_t location, unsigned owner)
{
    ScriptAction action = { .type = SAT_TELEPORT, .op_type = { SFO_PLAYER, SVT_NUMBER, SVT_NUMBER, SVT_NUMBER }, .op_value = { 0, map + 4999, (int)location_get_x(location), (int)location_get_y(location) } };
    if (!script_file(number, &action, 1, 0)) return false;
    Script script = { .num = number };
    ScriptInvocation invocation = { .script = &script, .triggerer_obj = pc(owner), .attachment_point = SAP_USE };
    script_execute(&invocation);
    return coop_runtime_phase() == COOP_RUNTIME_DRAINING;
}
static unsigned acting(unsigned phase)
{
    if (phase == 2 || phase == 3 || phase == 8 || phase == 11 || phase == 15) return 2;
    if (phase == 5 || phase == 7 || phase == 12 || phase == 16) return 3;
    return 1;
}
static int stair_script(unsigned phase)
{
    switch (phase) {
    case 10:
        return 1205;
    case 11:
        return 1207;
    case 12:
        return 1209;
    case 14:
        return 1210;
    case 15:
        return 1208;
    case 16:
        return 1206;
    default:
        return 0;
    }
}
static int map_after(unsigned phase)
{
    if (phase == 10 || phase == 15) return 3;
    if (phase == 11 || phase == 14) return 4;
    if (phase == 12) return 5;
    if (phase == 16) return 1;
    return 0;
}
static unsigned voyage_actor(unsigned phase)
{
    if (phase == 63) return 1;
    if (phase == 64) return 2;
    if (phase == 65 || phase == 71) return 3;
    if (phase == 67) return (unsigned)script_global_var_get(1937);
    if (phase == 69 || phase == 70) return 2;
    return 1;
}
static uint64_t campaign_clock(void)
{
    DateTime now = sub_45A7C0();
    return (uint64_t)now.days * COOP_DAY_MS + now.milliseconds;
}
static bool voyage_prepare(unsigned phase)
{
    if (phase == 62 || phase == 63 || phase == 64 || phase == 65) {
        int64_t teacher = find_attachment(1061, SAP_DIALOG);
        if (!teacher) return false;
        object_flags_unset(teacher, OF_OFF);
        if (!place_near(teacher)) return false;
        if (phase == 62) {
            for (unsigned i = 0; i < 4; i++) {
                stat_base_set(pc(i), STAT_LEVEL, 20);
                stat_base_set(pc(i), STAT_CHARISMA, 20);
                stat_base_set(pc(i), STAT_INTELLIGENCE, 14 + (int)i);
                basic_skill_points_set(pc(i), BASIC_SKILL_PERSUATION, 5);
            }
            ScriptAction one = { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_STRENGTH, 0, 1931 } };
            ScriptAction two = { .type = SAT_GET_STAT, .op_type = { SVT_NUMBER, SFO_PLAYER, SVT_GL_VAR }, .op_value = { STAT_INTELLIGENCE, 0, 1932 } };
            if (!script_file(999962, &one, 1, 0) || !script_file(999963, &two, 1, 0)) return false;
            DateTime now = sub_45A7C0();
            ScriptAction delayed[] = {
                { .type = SAT_CALL_SCRIPT_IN, .op_type = { SVT_NUMBER, SVT_NUMBER, SFO_PLAYER, SFO_ATTACHEE, SVT_NUMBER }, .op_value = { 999962, 0, 0, 0, 1 } },
                { .type = SAT_CALL_SCRIPT_AT, .op_type = { SVT_NUMBER, SVT_NUMBER, SFO_PLAYER, SFO_ATTACHEE, SVT_NUMBER }, .op_value = { 999963, 0, 0, 0, (int)(now.milliseconds / 1000 + 2) % 86400 } }
            };
            if (!script_file(999961, delayed, 2, 0)) return false;
            script_global_var_set(1931, 0);
            script_global_var_set(1932, 0);
            Script script = { .num = 999961 };
            ScriptInvocation call = { .script = &script, .triggerer_obj = pc(2), .attachee_obj = teacher, .attachment_point = SAP_USE };
            script_execute(&call);
            uint64_t before = campaign_clock(), real = SDL_GetTicks();
            FadeData fade = { .color = tig_color_make(0, 0, 0), .steps = 12, .duration = 0.15f };
            gfade_run_campaign(&fade);
            fade.flags = FADE_IN;
            gfade_run_campaign(&fade);
            if (campaign_clock() != before || script_global_var_get(1931) || script_global_var_get(1932) || SDL_GetTicks() - real < 280) return false;
            script_global_var_set(1934, 1);
        }
        if (phase == 63 || phase == 64) {
            const char* text = "{1}{Choose a native panel.}{Choose a native panel.}{}{}{}{}\n{2}{Let me inspect your abilities.}{}{1}{}{0}{ce}\n{3}{L:}{}{1}{}{0}{}\n{4}{Goodbye.}{}{1}{}{0}{}\n";
            FILE* file = fopen("data/dlg/999960arcmp-progress.dlg", "wb");
            bool good = file && fwrite(text, 1, strlen(text), file) == strlen(text);
            if (file && fclose(file)) good = false;
            ScriptAction action = { .type = SAT_RETURN_AND_SKIP_DEFAULT };
            if (!good || !script_file(999960, &action, 1, 0)) return false;
            dialog_ui_start_dialog(pc(voyage_actor(phase)), teacher, 999960, 0, 1);
        }
        if (phase == 65) {
            DateTime now = sub_45A7C0();
            script_global_var_set(1935, (int)now.days);
            script_global_var_set(1936, (int)now.milliseconds);
        }
    }
    if (phase == 66 || phase == 67) {
        int64_t npc = find_attachment(1066, SAP_DIALOG);
        if (!npc) return false;
        object_flags_unset(npc, OF_OFF);
        voyage_companion = obj_get_id(npc);
        if (phase == 67) {
            /* Controlled advanced characters still pass the native recruitment
             * rules. Race operands in .dlg are one-based (ra -5: no gnomes). */
            int owner = -1;
            for (unsigned n = 0; n < 4; n++) {
                unsigned i = (n + 1) % 4;
                int level = stat_level_get(npc, STAT_LEVEL) + 1;
                if (stat_level_get(pc(i), STAT_LEVEL) < level) stat_base_set(pc(i), STAT_LEVEL, level);
                int reason = ai_check_follow(npc, pc(i), true);
                fprintf(stderr, "ARCMP voyage recruitment candidate=%u pc-level=%d npc-level=%d race=%d reason=%d\n", i, stat_level_get(pc(i), STAT_LEVEL), stat_level_get(npc, STAT_LEVEL), stat_level_get(pc(i), STAT_RACE), reason);
                if (owner < 0 && stat_level_get(pc(i), STAT_RACE) != RACE_GNOME && reason == AI_FOLLOW_OK) owner = (int)i;
            }
            if (owner < 0) return false;
            script_global_var_set(1937, owner);
        }
        return place_near(npc);
    }
    if (phase == 69) {
        int64_t npc = find_attachment(1067, SAP_DIALOG);
        if (npc) object_flags_unset(npc, OF_OFF);
        return npc && place_near(npc);
    }
    if (phase == 70) {
        int64_t hatch = find_attachment(1190, SAP_USE);
        if (!hatch) hatch = find_attachment(1211, SAP_USE);
        if (!hatch) return false;
        script_global_var_set(1938, script_number(hatch, SAP_USE));
        object_flags_unset(hatch, OF_OFF | OF_DONTDRAW | OF_CLICK_THROUGH);
        fprintf(stderr, "ARCMP voyage secret entrance script=%d location=%lld flags=%x\n", script_number(hatch, SAP_USE), (long long)obj_field_int64_get(hatch, OBJ_F_LOCATION), obj_field_int32_get(hatch, OBJ_F_FLAGS));
        return place_near(hatch);
    }
    if (phase == 71) {
        script_global_var_set(1939, -1);
        int64_t npc = find_attachment(1071, SAP_DIALOG);
        if (npc) object_flags_unset(npc, OF_OFF);
        return npc && place_near(npc);
    }
    if (phase == 72) return place_members_near(find_attachment(1212, SAP_USE), 1u << voyage_actor(phase));
    if (phase == 73) {
        int owner = -1;
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
            if (item_find_by_name(coop_campaign_object(i), 5203)) {
                if (owner >= 0) return false;
                owner = (int)i;
            }
        if (owner < 0) return false;
        script_global_var_set(1939, owner);
    }
    return true;
}

static uint32_t forest_chart_hash(void)
{
    size_t size = 0;
    uint8_t* bytes = townmap_campaign_capture(&size);
    if (!bytes) return 0;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    free(bytes);
    return hash & 0x7fffffffu;
}
static int forest_member(const CoopDialogueView* view)
{
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
        if (!memcmp(coop_campaign_member(i)->object_id.d.g.data, view->pc_guid, 16)) return (int)i;
    return -1;
}
static unsigned forest_actor(unsigned phase)
{
    if (phase == 79 || phase == 81) {
        int owner = script_global_var_get(1925);
        for (unsigned i = 0; i < 4; i++)
            if (owner >= 0 && pc(i) == coop_campaign_object((unsigned)owner)) return i;
    }
    if (phase == 76 || phase == 86 || phase == 92) return 1;
    if (phase == 84 || phase == 90 || phase == 93) return 3;
    return 2;
}
static int forest_dialog(unsigned phase)
{
    if (phase == 76 || phase == 81) return 1076;
    if (phase == 78) return 1079;
    if (phase == 84 || phase == 88 || phase == 92) return 1074;
    if (phase == 86) return 1080;
    if (phase == 90) return 1073;
    return 0;
}
static bool forest_save(unsigned phase)
{
    return phase == 76 || phase == 78 || phase == 79 || phase == 81 || phase == 82 || phase == 84 || phase == 86 || phase == 88 || phase == 90 || phase == 92 || phase == 94;
}
static bool forest_transfer(unsigned phase)
{
    return phase == 75 || phase == 77 || phase == 80 || phase == 83 || phase == 85 || phase == 87;
}
static unsigned forest_epochs(unsigned phase)
{
    if (phase == 76) return 2;
    return forest_transfer(phase) || forest_save(phase) || phase == 89 || phase == 91 || phase == 96;
}
static int64_t forest_pelt(void)
{
    int64_t object, fallback = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (!item_is_item(object) || obj_field_int32_get(object, OBJ_F_NAME) != 5612) continue;
            int64_t parent = 0;
            item_parent(object, &parent);
            if (!parent || obj_field_int32_get(parent, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) return object;
            fallback = object;
        } while (obj_inst_next(&object, &iterator));
    return fallback;
}
static bool forest_ready(unsigned phase)
{
    if (phase == 76) return quest_state_get(pc(0), 1043) == QUEST_STATE_ACCEPTED;
    if (phase == 78) {
        int member = script_global_var_get(1925);
        return script_global_flag_get(1093) && member >= 0 && coop_campaign_member((unsigned)member) != NULL && item_find_by_name(coop_campaign_object((unsigned)member), 4406);
    }
    if (phase == 79) return item_find_by_name(pc(forest_actor(phase)), 5612) != 0;
    if (phase == 81) return script_story_state_get() >= 9 && quest_state_get(pc(0), 1018) == QUEST_STATE_COMPLETED;
    if (phase == 84) return quest_state_get(pc(0), 1019) == QUEST_STATE_ACCEPTED && area_is_known(pc(0), 12);
    if (phase == 86) return script_global_flag_get(1070);
    if (phase == 88) return rumor_known_get(pc(0), 2053);
    if (phase == 90) return script_global_flag_get(1076) && script_global_flag_get(1081) && rumor_known_get(pc(0), 2057);
    if (phase == 92) return script_story_state_get() >= 10 && quest_state_get(pc(0), 1020) == QUEST_STATE_ACCEPTED && quest_state_get(pc(0), 1021) == QUEST_STATE_ACCEPTED;
    return true;
}
static bool forest_party(void)
{
    unsigned count = 0;
    ObjectList list;
    object_list_party(pc(2), &list);
    for (ObjectNode* n = list.head; n; n = n->next) {
        if (coop_campaign_slot_for_object(n->obj) < 0) {
            object_list_destroy(&list);
            return false;
        }
        count++;
    }
    object_list_destroy(&list);
    if (count != 4) return false;
    object_list_team(pc(2), &list);
    unsigned team = 0;
    for (ObjectNode* n = list.head; n; n = n->next)
        team++;
    object_list_destroy(&list);
    script_global_var_set(1920, 0);
    script_global_var_set(1921, 0);
    script_global_var_set(1922, (int)team);
    ScriptAction actions[] = {
        { .type = SAT_LOOP_FOR, .op_type = { SFO_EVERYONE_IN_PARTY } },
        { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1920, 1920, 1 } },
        { .type = SAT_LOOP_END },
        { .type = SAT_LOOP_FOR, .op_type = { SFO_EVERYONE_IN_TEAM } },
        { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1921, 1921, 1 } },
        { .type = SAT_LOOP_END }
    };
    if (!script_file(999950, actions, SDL_arraysize(actions), 0)) return false;
    Script script = { .num = 999950 };
    ScriptInvocation call = { .script = &script, .triggerer_obj = pc(2), .attachment_point = SAP_USE };
    script_execute(&call);
    return script_global_var_get(1920) == 4 && script_global_var_get(1921) == (int)team;
}
static bool forest_unfog(void)
{
    ScriptAction actions[] = {
        { .type = SAT_UNFOG_TOWNMAP, .op_type = { SVT_NUMBER }, .op_value = { 11 } },
        { .type = SAT_UNFOG_TOWNMAP, .op_type = { SVT_NUMBER }, .op_value = { 85 } }
    };
    if (!script_file(999951, actions, 2, 0)) return false;
    Script script = { .num = 999951 };
    ScriptInvocation call = { .script = &script, .triggerer_obj = pc(3), .attachment_point = SAP_USE };
    script_execute(&call);
    char path[TIG_MAX_PATH];
    snprintf(path, sizeof(path), "Save\\Current\\%s.tmf.tmp", townmap_name(85));
    if (tig_file_exists(path, NULL) || !tig_file_mkdir(path)) return false;
    bool refused = !townmap_flush(), removed = tig_file_rmdir(path), retried = removed && townmap_flush();
    fprintf(stderr, "ARCMP forest townmap write refusal=%d removed=%d retry=%d\n", refused, removed, retried);
    if (!refused || !retried) return false;
    script_global_var_set(1923, 1);
    script_global_var_set(1928, (int)forest_chart_hash());
    return script_global_var_get(1928) != 0;
}
static int forest_tile(int64_t loc)
{
    int map = townmap_get(sector_id_from_loc(loc));
    TownMapInfo info;
    if (!map || !townmap_info(map, &info)) return -1;
    int64_t x, y, center_x, center_y;
    /* Visited bits use the full bitmap canvas. loc_to_coords returns scaled
     * UI pixels and cannot be divided by the unscaled canvas dimensions. */
    location_xy(info.loc, &center_x, &center_y);
    location_xy(loc, &x, &y);
    x += info.width / 2 - center_x;
    y += info.height / 2 - center_y;
    if (x < 0 || y < 0 || x >= info.width || y >= info.height) return -1;
    return (int)((int64_t)y * info.num_vert_tiles / info.height) * info.num_hor_tiles + (int)((int64_t)x * info.num_hor_tiles / info.width);
}
static bool forest_explore(void)
{
    int64_t actor = pc(3), from = obj_field_int64_get(actor, OBJ_F_LOCATION);
    int map = townmap_get(sector_id_from_loc(from));
    int start = forest_tile(from);
    if (map <= 0 || start < 0 || !townmap_set_known(map, false)) return false;
    for (unsigned i = 0; i < 4; i++)
        townmap_mark_visited(obj_field_int64_get(pc(i), OBJ_F_LOCATION));
    for (int radius = 16; radius <= 72; radius += 8)
        for (int dy = -radius; dy <= radius; dy += 8)
            for (int dx = -radius; dx <= radius; dx += 8) {
                int64_t to = location_make(location_get_x(from) + dx, location_get_y(from) + dy);
                if (forest_tile(to) < 0 || forest_tile(to) == start || townmap_get(sector_id_from_loc(to)) != map || tile_is_blocking(to, false)) continue;
                int8_t rotations[128];
                PathCreateInfo path = { .obj = actor, .from = from, .to = to, .max_rotations = 128, .rotations = rotations, .flags = PATH_FLAG_0x1000 | PATH_FLAG_0x0200 };
                if (path_make(&path) <= 0) continue;
                script_global_var_set(1930, 1);
                script_global_var_set(1924, map);
                script_global_var_set(1926, (int)location_get_x(to));
                script_global_var_set(1927, (int)location_get_y(to));
                return true;
            }
    return false;
}
static bool forest_prepare(unsigned phase)
{
    if (phase == 74) return forest_party();
    int number = forest_dialog(phase);
    if (number) {
        int64_t npc = find_attachment(number, SAP_DIALOG);
        if (!npc) return false;
        bool gate = number == 1076 && (script_number(npc, SAP_FIRST_HEARTBEAT) == 2516 || script_number(npc, SAP_HEARTBEAT) == 2516);
        if (!gate) object_flags_unset(npc, OF_OFF);
        fprintf(stderr, "ARCMP forest prepare NPC=%d flags=%x first=%d heartbeat=%d hour=%d\n", number, obj_field_int32_get(npc, OBJ_F_FLAGS), script_number(npc, SAP_FIRST_HEARTBEAT), script_number(npc, SAP_HEARTBEAT), datetime_current_hour());
        if (phase == 76) {
            script_global_var_set(1919, 0);
            return true;
        }
        if (phase == 78) {
            for (unsigned i = 0; i < 4; i++)
                item_gold_transfer(OBJ_HANDLE_NULL, pc(i), 10000, OBJ_HANDLE_NULL);
            script_global_var_set(1925, -1);
        }
        return place_near(npc);
    }
    if (phase == 79) {
        int64_t pelt = forest_pelt(), parent = 0;
        if (!pelt) return false;
        item_parent(pelt, &parent);
        fprintf(stderr, "ARCMP forest pelt parent-type=%d flags=%x\n", parent ? obj_field_int32_get(parent, OBJ_F_TYPE) : -1, obj_field_int32_get(pelt, OBJ_F_FLAGS));
        return place_members_near(parent ? parent : pelt, 1u << forest_actor(phase));
    }
    if (phase == 82) return forest_unfog();
    if (phase == 89) return place_members_near(find_attachment(1410, SAP_USE), 1u << forest_actor(phase));
    if (phase == 91) return place_members_near(find_attachment(1411, SAP_USE), 1u << forest_actor(phase));
    if (phase == 93) return forest_explore();
    if (phase == 94) {
        script_global_var_set(1929, (int)forest_chart_hash());
        return script_global_var_get(1929) != 0;
    }
    if (phase == 95) return townmap_set_known(85, false);
    return true;
}
static bool forest_do_transfer(unsigned phase)
{
    int map = 1;
    int64_t loc = 0;
    if (phase == 75 || phase == 80) loc = location_make(76113, 48162);
    if (phase == 77) loc = location_make(62243, 65664);
    if (phase == 83 || phase == 87) {
        map = 19;
        loc = location_make(2007, 2030);
    }
    if (phase == 85) loc = location_make(104172, 35969);
    int script = 999952;
    for (unsigned before = 74; before < phase; before++)
        if (forest_transfer(before)) script++;
    return loc && transfer_script(script, map, loc, forest_actor(phase));
}

static unsigned voyage_epochs(unsigned stage)
{
    if (stage == 65 || stage == 73) return 2;
    return stage == 61 || stage == 62 || stage == 67 || stage == 68 || stage == 69 || stage == 70 || stage == 71 || stage == 72;
}
static int dark_dialog(unsigned phase)
{
    return phase == 98 ? 1498 : phase == 102 ? 1078
        : phase == 104                       ? 1086
        : (phase == 108 || phase == 112)     ? 1098
        : phase == 121                       ? 1087
                                             : 0;
}
static bool dark_save(unsigned phase)
{
    return phase == 99 || phase == 102 || phase == 104 || phase == 106 || phase == 108 || phase == 111 || phase == 112 || phase == 116 || phase == 117 || phase == 119 || phase == 121 || phase == 122;
}
static bool dark_transfer(unsigned phase) { return phase == 97 || phase == 107 || phase == 113 || phase == 118; }
static unsigned dark_epochs(unsigned phase) { return phase == 122 ? 2 : phase == 101 || phase == 103 || dark_transfer(phase) || dark_save(phase) || phase == 120 || phase == 123; }
static int64_t dark_document(unsigned phase)
{
    int script = (phase == 98 || phase == 99 || phase == 100) ? 2483 : (phase == 110 || phase == 111) ? 1103
        : (phase >= 114 && phase <= 117)                                                              ? 1102
                                                                                                      : 2426;
    int sap = script == 1103 ? SAP_INSERT_ITEM : SAP_USE;
    bool newspaper = phase >= 98 && phase <= 100;
    int64_t object, fallback = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (!item_is_item(object)) continue;
            if (newspaper ? (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_WRITTEN || obj_field_int32_get(object, OBJ_F_WRITTEN_SUBTYPE) != 2) : script_number(object, sap) != script) continue;
            int64_t parent = 0;
            item_parent(object, &parent);
            if (parent && coop_campaign_slot_for_object(parent) >= 0) return object;
            if (!fallback) fallback = object;
        } while (obj_inst_next(&object, &iterator));
    return fallback;
}
static int64_t dark_grave(unsigned phase)
{
    int script = phase < 113 ? 1103 : 1102;
    int64_t book = find_attachment(script, script == 1103 ? SAP_INSERT_ITEM : SAP_USE), parent = 0;
    return book && item_parent(book, &parent) && parent && obj_field_int32_get(parent, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER ? parent : 0;
}
static unsigned dark_actor(unsigned phase)
{
    if (phase == 105) {
        int member = script_global_var_get(1881);
        for (unsigned i = 0; i < 4; i++)
            if (member >= 0 && pc(i) == coop_campaign_object((unsigned)member)) return i;
    }
    if (phase == 99 || phase == 106 || phase == 117) {
        int64_t item = dark_document(phase), parent = 0;
        if (item && item_parent(item, &parent))
            for (unsigned i = 0; i < 4; i++)
                if (pc(i) == parent) return i;
    }
    return phase == 98 || phase == 114 || phase == 115 || phase == 116 ? 1 : phase == 104 || phase == 108 || phase == 112 || phase == 121 ? 2
                                                                                                                                          : 0;
}
static int64_t dark_guard(void)
{
    static ObjectID target_id;
    int64_t target = target_id.type ? obj_pool_perm_lookup(target_id) : 0;
    if (target && !critter_is_dead(target) && !(obj_field_int32_get(target, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return target;
    int64_t object, best = 0;
    int iterator;
    int64_t distance = 100000;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || script_number(object, SAP_DIALOG) != 1096 || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            for (unsigned i = 0; i < 4; i++) {
                int64_t n = location_dist(obj_field_int64_get(pc(i), OBJ_F_LOCATION), obj_field_int64_get(object, OBJ_F_LOCATION));
                if (n < distance && n <= 45) {
                    distance = n;
                    best = object;
                }
            }
        } while (obj_inst_next(&object, &iterator));
    target_id = best ? obj_get_id(best) : (ObjectID) { 0 };
    return best;
}
static bool dark_ready(unsigned phase)
{
    if (phase == 98) {
        int64_t p = 0, item = dark_document(phase);
        return item && item_parent(item, &p) && coop_campaign_slot_for_object(p) >= 0;
    }
    if (phase == 99) return coop_engine_documents()->count >= 1;
    if (phase == 102) return script_story_state_get() >= 11 && quest_state_get(pc(0), 1021) == QUEST_STATE_COMPLETED && quest_state_get(pc(0), 1022) == QUEST_STATE_ACCEPTED;
    if (phase == 104) {
        for (unsigned i = 0; i < 4; i++)
            if (item_find_by_name(pc(i), 5206)) return true;
        return false;
    }
    if (phase == 106) return script_story_state_get() >= 12 && area_is_known(pc(0), 4) && coop_engine_documents()->count >= 2;
    if (phase == 108) return script_global_flag_get(2006) && rumor_known_get(pc(0), 2092);
    if (phase == 109) return script_global_flag_get(2005) && quest_state_get(pc(0), 1048) == QUEST_STATE_COMPLETED;
    if (phase == 110 || phase == 115) {
        int64_t grave = dark_grave(phase);
        return grave && script_number(grave, SAP_USE) == 2943;
    }
    if (phase == 111) return rumor_known_get(pc(0), 2002);
    if (phase == 112) return script_story_state_get() >= 13 && area_is_known(pc(0), 13);
    if (phase == 114) return script_global_flag_get(2646);
    if (phase == 116) {
        int64_t parent = 0, book = dark_document(phase);
        return book && item_parent(book, &parent) && coop_campaign_slot_for_object(parent) >= 0;
    }
    if (phase == 117) return script_story_state_get() >= 14 && quest_state_get(pc(0), 1020) == QUEST_STATE_COMPLETED && quest_state_get(pc(0), 1022) == QUEST_STATE_COMPLETED && area_is_known(pc(0), 16) && coop_engine_documents()->count >= 3;
    if (phase == 119) return script_global_flag_get(2541) && !dark_guard();
    if (phase == 121) return script_story_state_get() >= 15 && script_global_flag_get(2000) && quest_state_get(pc(0), 1016) == QUEST_STATE_COMPLETED;
    return true;
}
static bool dark_equipment(int64_t actor, int prototype, int slot)
{
    int64_t source = sub_4685A0(prototype), item = 0;
    if (!source) return false;
    item = item_find_by_name(actor, obj_field_int32_get(source, OBJ_F_NAME));
    if (!item && !object_create(source, obj_field_int64_get(actor, OBJ_F_LOCATION), &item)) return false;
    return slot < 0 ? item_transfer(item, actor) : item_transfer_ex(item, actor, slot);
}
static bool dark_prepare(unsigned phase)
{
    if (phase == 101 || phase == 103) return place_members_near(find_attachment(phase == 101 ? 1755 : 1756, SAP_USE), 1u << dark_actor(phase));
    if (phase == 97)
        for (unsigned i = 0; i < 4; i++) {
            stat_base_set(pc(i), STAT_INTELLIGENCE, 12);
            stat_base_set(pc(i), STAT_STRENGTH, 18);
            stat_base_set(pc(i), STAT_DEXTERITY, 20);
            basic_skill_points_set(pc(i), BASIC_SKILL_MELEE, 5);
            obj_field_int32_set(pc(i), OBJ_F_HP_PTS, 300);
            obj_field_int32_set(pc(i), OBJ_F_HP_DAMAGE, 0);
            if (!dark_equipment(pc(i), BP_SHOVEL, -1)) return false;
        }
    int number = dark_dialog(phase);
    if (number) {
        int64_t npc = find_attachment(number, SAP_DIALOG);
        if (!npc) return false;
        if (number != 1087 || (script_number(npc, SAP_HEARTBEAT) != 1242 && script_number(npc, SAP_FIRST_HEARTBEAT) != 1242)) object_flags_unset(npc, OF_OFF);
        return place_near(npc);
    }
    if (phase == 105) {
        int64_t item = dark_document(phase), parent = 0;
        if (!item || !item_parent(item, &parent)) return false;
        int member = coop_campaign_slot_for_object(parent);
        if (member < 0) return false;
        script_global_var_set(1881, member);
        unsigned origin = dark_actor(phase);
        for (unsigned step = 1; step < 4; step++) {
            unsigned destination = (origin + step) % 4;
            if (location_dist(obj_field_int64_get(parent, OBJ_F_LOCATION), obj_field_int64_get(pc(destination), OBJ_F_LOCATION)) > 8) continue;
            script_global_var_set(1882, coop_campaign_slot_for_object(pc(destination)));
            return true;
        }
        return false;
    }
    if (phase == 109) return place_members_near(find_attachment(1236, SAP_EXAMINE), 1u << dark_actor(phase));
    if (phase == 110 || phase == 111 || phase == 114 || phase == 115 || phase == 116) return place_members_near(dark_grave(phase), 1u << dark_actor(phase));
    if (phase == 119) {
        int64_t npc = find_attachment(1096, SAP_DIALOG);
        if (!npc || !place_near(npc)) return false;
        /* Earlier controlled chapter fixtures quieted unrelated NPCs on this
         * persistent outdoor map. Restore the actual entrance guards before
         * exercising their ordinary combat and death scripts. */
        int64_t object;
        int iterator;
        if (obj_inst_first(&object, &iterator)) do {
                if (script_number(object, SAP_DIALOG) == 1096
                    && location_dist(obj_field_int64_get(object, OBJ_F_LOCATION), obj_field_int64_get(npc, OBJ_F_LOCATION)) <= 45) object_flags_unset(object, OF_OFF);
            } while (obj_inst_next(&object, &iterator));
        for (unsigned i = 0; i < 4; i++) {
            obj_field_int32_set(pc(i), OBJ_F_HP_DAMAGE, 0);
            obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_PTS, 500);
            obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_DAMAGE, 0);
            basic_skill_points_set(pc(i), BASIC_SKILL_MELEE, 5);
            basic_skill_points_set(pc(i), BASIC_SKILL_DODGE, 5);
            for (int skill = BASIC_SKILL_DODGE; skill <= BASIC_SKILL_MELEE; skill++) {
                if (basic_skill_training_get(pc(i), skill) < TRAINING_APPRENTICE) basic_skill_training_set(pc(i), skill, TRAINING_APPRENTICE);
                if (basic_skill_training_get(pc(i), skill) < TRAINING_EXPERT) basic_skill_training_set(pc(i), skill, TRAINING_EXPERT);
                if (basic_skill_points_get(pc(i), skill) != 5 || basic_skill_training_get(pc(i), skill) < TRAINING_EXPERT) return false;
            }
            int64_t weapon = item_wield_get(pc(i), ITEM_INV_LOC_WEAPON);
            if (weapon && !sub_464C80(weapon)) return false;
            if (!dark_equipment(pc(i), BP_SWORD, ITEM_INV_LOC_WEAPON)) return false;
        }
    }
    if (phase == 120) return place_members_near_radius(find_attachment(2465, SAP_USE), 1u << dark_actor(phase), 1);
    if (phase == 122) {
        script_global_var_set(1884, (int)coop_engine_documents()->count);
        return place_members_near(find_attachment(2467, SAP_USE), 1u << dark_actor(phase));
    }
    if (phase == 123) {
        CoopDocuments empty = { 0 };
        coop_engine_documents_install(&empty);
    }
    return true;
}
static bool dark_do_transfer(unsigned phase)
{
    int64_t where = phase == 97 ? location_make(62243, 65664) : phase == 107 ? location_make(102768, 116894)
        : phase == 113                                                       ? location_make(112027, 96883)
                                                                             : location_make(96416, 18911);
    int script = 999940;
    for (unsigned i = 97; i < phase; i++)
        if (dark_transfer(i)) script++;
    return transfer_script(script, 1, where, dark_actor(phase));
}
/* Connected original late-campaign route. Every row uses the ordinary public
 * multiplayer action entry; private scripts only position the controlled party. */
typedef enum LegacyKind { LG_TRAVEL,
    LG_USE,
    LG_TALK,
    LG_EXAMINE,
    LG_LOOT,
    LG_COLLECT,
    LG_EQUIP,
    LG_READ,
    LG_RUNE,
    LG_TILE,
    LG_FIGHT,
    LG_SAVE,
    LG_LOAD,
    LG_COMPANION,
    LG_DROP,
    LG_USE_ITEM,
    LG_VERIFY,
    LG_UNEQUIP } LegacyKind;
typedef struct LegacyStage {
    LegacyKind kind;
    int script, goal, map, x, y;
    bool save;
} LegacyStage;
static const LegacyStage legacy_stages[] = {
    { LG_TRAVEL, 0, 0, 19, 2007, 2030 }, /* 124 Qintarra */
    { LG_TALK, 1074, 653, 0, 0, 0, true }, /* 125 Raven */
    { LG_USE, 1410, 0, 20 }, /* 126 */
    { LG_TALK, 1073, 121, 0, 0, 0, true }, /* 127 Silver Lady */
    { LG_USE, 1411, 0, 19 }, /* 128 */
    { LG_TALK, 1074, 753, 0, 0, 0, true }, /* 129 interpretation */
    { LG_TRAVEL, 0, 0, 1, 102768, 116894 }, /* 130 Caladon */
    { LG_USE, 2484, 0, 59 }, /* 131 temple */
    { LG_TALK, 1090, 180, 0, 0, 0, true }, /* 132 Hadrian */
    { LG_USE, 2488, 0, 59 }, /* 133 tomb */
    { LG_LOOT, 1183, 5615 }, /* 134 authentic skull */
    { LG_EXAMINE, 1203, 0, 0, 0, 0, true }, /* 135 opened lid/blood message */
    { LG_USE, 2491, 0, 59 }, /* 136 return */
    { LG_TALK, 1090, 203, 0, 0, 0, true }, /* 137 skull hand-in */
    { LG_TALK, 1091, 193, 0, 0, 0, true }, /* 138 Gunther */
    { LG_USE, 2485, 0, 1 }, /* 139 outside */
    { LG_TRAVEL, 0, 0, 1, 46112, 120624 }, /* 140 Thanatos */
    { LG_TALK, 1108, 354, 0, 0, 0, true }, /* 141 Nasrudin/Wastes */
    { LG_TALK, 1108, 312, 0, 0, 0, true }, /* 142 assignment */
    { LG_TRAVEL, 0, 0, 1, 28455, 31171 }, /* 143 Tulla */
    { LG_USE, 2356, 0, 32 }, /* 144 */
    { LG_USE, 2354, 0, 32 }, /* 145 Simeon's office */
    { LG_TALK, 1890, 30, 0, 0, 0, true }, /* 146 */
    { LG_USE, 2686, 0, 32, 0, 0, true }, /* 147 original mural */
    { LG_USE, 2355, 0, 1 }, /* 148 outside */
    { LG_COLLECT, 0, 2848, 0, 0, 0, true }, /* 149 authentic medallion */
    { LG_EQUIP, 0, 2848 }, /* 150 */
    { LG_COLLECT, -14062, 0, 0, 28379, 31142 }, /* 151 original Verses, Canto 1 (stored prototype ID 14082) */
    { LG_READ, -14062, 0, 0, 28379, 31142, true }, /* 152 */
    { LG_RUNE, 2226, 1 }, /* 153 truth */
    { LG_RUNE, 2227, 2 }, /* 154 power */
    { LG_RUNE, 2228, 3 }, /* 155 spirit */
    { LG_RUNE, 2229, 4 }, /* 156 material */
    { LG_RUNE, 2230, 5, 0, 0, 0, true }, /* 157 Pelojian */
    { LG_TALK, 1921, 25, 0, 0, 0, true }, /* 158 ruins revealed */
    { LG_TRAVEL, 0, 0, 1, 19704, 37452 }, /* 159 Vendigroth */
    { LG_USE, 2379, 0, 33 }, /* 160 */
    { LG_USE, 2362, 0, 33 }, /* 161 */
    { LG_USE, 2363, 0, 33 }, /* 162 */
    { LG_USE, 2380, 0, 33 }, /* 163 */
    { LG_USE, 2388, 0, 33 }, /* 164 */
    { LG_COLLECT, 1094, 5616, 0, 0, 0, true }, /* 165 actual device */
    { LG_TRAVEL, 0, 0, 1, 111981, 96786 }, /* 166 Brodgar */
    { LG_TALK, 1171, 40, 8 }, /* 167 original banishment */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 168 Void checkpoint + tile oracle */
    { LG_LOAD }, /* 169 voted restoration */
    { LG_USE, 2642, 0, 72 }, /* 170 Arronax interior */
    { LG_TALK, 1116, 270, 0, 0, 0, true }, /* 171 original revelation/assignment */
    { LG_USE, 2643, 0, 8 }, /* 172 return to Void */
    { LG_FIGHT, 1111, 0, 0, 0, 0, true }, /* 173 original good-route guards/death permission */
    { LG_USE, 2644, 0, 73 }, /* 174 castle */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 175 final native checkpoint before automatic dialogue */
    { LG_TILE, 3070, 0, 73, 428, 112 }, /* 176 original Kerghan chamber ground trigger */
    { LG_TALK, 1105, 636 }, /* 177 original peaceful Device ending */
    { LG_TRAVEL, 0, 0, 1, 96383, 18908 }, /* 178 retained failed tree approach */
    { LG_USE, 2465, 0, 41 }, /* 179 actual approach, no adjacent placement */
    { LG_USE, 2467, 0, 1 }, /* 180 native return */
    { LG_TRAVEL, 0, 0, 1, 61036, 42550 }, /* 181 outside Wheel Clan */
    { LG_COMPANION, 1066, 2806 }, /* 182 original Thorvald spectacles via Inventory */
    { LG_EQUIP, 0, 2806 }, /* 183 native helmet slot */
    { LG_RUNE, 1202, 1 }, /* 184 original optical trigger */
    { LG_USE, 1201, 0, 7 }, /* 185 actual optical entrance */
    { LG_USE, 2304, 0, 1 }, /* 186 original outside return */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 187 discovery and worn spectacles */
    { LG_DROP, 0, 2806 }, /* 188 real inventory divergence */
    { LG_LOAD }, /* 189 restore worn original artifact */
    { LG_COMPANION, 1066 }, /* 190 distinct arrival retries while Inventory is open */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 191 arrival ordering checkpoint */
    { LG_LOAD }, /* 192 restore ordering and inventory after divergence */
    { LG_TRAVEL, 0, 0, 1, 20691, 56027, true }, /* 193 original Isle landing and branch checkpoint */
    { LG_TALK, 2650, -2 }, /* 194 original utility-NPC helm refusal */
    { LG_TALK, 1061, -18, 1, 32205, 62499 }, /* 195 Teach's actual rescue/return */
    { LG_LOAD }, /* 196 restore the original Isle branch after normal return */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 197 native branch checkpoint */
    { LG_FIGHT, 1061 }, /* 198 original captain death / emergency permission */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 199 native corpse and helm permission */
    { LG_TRAVEL, 0, 0, 1, 20691, 56027 }, /* 200 original Isle landing */
    { LG_TALK, 2650, -2, 1, 32227, 62497 }, /* 201 original utility-NPC helm departure */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 202 helm permission consumed */
    { LG_TALK, 1749, -20, 1, 62183, 65692 }, /* 203 paid commercial passage to Tarant */
    { LG_TALK, 1749, 520 }, /* 204 original Tarant captain refuses Ashbury route */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 205 paid journeys and native state */
    { LG_LOAD }, /* 206 unanimous restoration */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 207 pre-entry combat loadout/checkpoint */
    { LG_TILE, 3070, 0, 73, 428, 112 }, /* 208 original chamber entry */
    { LG_TALK, 1105, -72 }, /* 209 original offered fight branch */
    { LG_USE_ITEM, 2862, 5616 }, /* 210 original Device refuses wrong form */
    { LG_FIGHT, 1105 }, /* 211 real robe/dragon/shell transformations */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 212 native regeneration shell checkpoint */
    { LG_USE_ITEM, 2862, 5616 }, /* 213 actual Device on native shell and original ending */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 214 retained pre-Mingorad branch/loadout */
    { LG_TALK, 1087, 239, 41 }, /* 215 original Stillwater assignment */
    { LG_TRAVEL, 0, 0, 1, 76113, 48162 }, /* 216 Stillwater */
    { LG_FIGHT, 1199 }, /* 217 actual town inhabitants and original death counters */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 218 native town destruction/reputation */
    { LG_TRAVEL, 0, 0, 41, 2040, 2038 }, /* 219 original Tsen-Ang entry coordinates */
    { LG_TALK, 1087, 488, 41 }, /* 220 actual report and Kan Hua commission */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 221 evil commission checkpoint */
    { LG_TRAVEL, 0, 0, 1, 102768, 116894 }, /* 222 Panarii exterior */
    { LG_USE, 2484, 0, 59 }, /* 223 original temple entrance, no adjacent placement */
    { LG_TALK, 1092, 365, 59 }, /* 224 original history/Device assignment and Brodgar reveal */
    { LG_VERIFY, 1092 }, /* 225 wait for the departure scheduled by the same encounter */
    { LG_USE, 2485, 0, 1 }, /* 226 original temple exit */
    { LG_TRAVEL, 0, 0, 1, 19704, 37452 }, /* 227 Vendigroth exterior */
    { LG_USE, 2379, 0, 33, 2051, 2038 }, /* 228 original descent */
    { LG_USE, 2362, 0, 33, 1951, 2033 }, /* 229 native first floor traversal */
    { LG_USE, 2363, 0, 33, 1803, 2032 }, /* 230 native second floor traversal */
    { LG_USE, 2380, 0, 33, 1708, 1994 }, /* 231 native third floor traversal */
    { LG_USE, 2388, 0, 33, 1444, 2042 }, /* 232 native fourth floor traversal */
    { LG_COLLECT, 1094, 5616, 0, 0, 0, true }, /* 233 authentic Device and original pickup gate */
    { LG_USE, 2389, 0, 33, 1530, 2008 }, /* 234 original bottom-floor return */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 235 authentic item checkpoint */
    { LG_TRAVEL, 0, 0, 1, 111981, 96786 }, /* 236 Brodgar */
    { LG_TALK, 1172, -2, 8 }, /* 237 actual Device hand-in and original evil banishment */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 238 Void/removed artifact checkpoint */
    { LG_USE, 2644, 0, 73 }, /* 239 original evil-route castle admission */
    { LG_TILE, 3070, 0, 73, 428, 112 }, /* 240 native castle traversal and chamber trigger */
    { LG_TALK, 1105, 382 }, /* 241 original alliance, ring and legendary-kill assignment */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 242 alternate route checkpoint */
    { LG_LOAD }, /* 243 unanimous restoration */
    { LG_TILE, 3069, 0, 73 }, /* 244 original chamber ground exit */
    { LG_TILE, 0, 0, 73, 439, 71 }, /* 245 reverse castle stairs */
    { LG_TILE, 0, 0, 73, 565, 96 }, /* 246 reverse castle stairs */
    { LG_USE, 2645, 0, 8, 1052, 871 }, /* 247 castle exterior */
    { LG_EQUIP, 0, 2857 }, /* 248 original granted ring */
    { LG_TILE, 2550, 0, 8, 1080, 884 }, /* 249 ring redirects to Nexus */
    { LG_UNEQUIP, 0, 2857 }, /* 250 select the normal destination */
    { LG_TILE, 2552, 0, 8, 1046, 1061 }, /* 251 Arronax island */
    { LG_USE, 2642, 0, 72, 361, 349 }, /* 252 original Arronax entrance */
    { LG_FIGHT, 1637, 6436 }, /* 253 actual Arronax death */
    { LG_TILE, 0, 0, 8, 351, 307 }, /* 254 original interior exit */
    { LG_EQUIP, 0, 2857 }, /* 255 */
    { LG_TILE, 2550, 0, 8, 1080, 884 }, /* 256 Nexus */
    { LG_UNEQUIP, 0, 2857 }, /* 257 */
    { LG_TILE, 0, 6449, 8 }, /* 258 original portal to the Bane of Kree's island */
    { LG_FIGHT, 2396, 6449 }, /* 259 Bane of Kree */
    { LG_EQUIP, 0, 2857 }, /* 260 */
    { LG_TILE, 0, 0, 8 }, /* 261 nearest original island portal, ring worn */
    { LG_UNEQUIP, 0, 2857 }, /* 262 */
    { LG_TILE, 0, 6550, 8 }, /* 263 Gorgoth island */
    { LG_FIGHT, 2870, 6550 }, /* 264 Gorgoth */
    { LG_EQUIP, 0, 2857 }, /* 265 */
    { LG_TILE, 0, 0, 8 }, /* 266 Nexus */
    { LG_UNEQUIP, 0, 2857 }, /* 267 */
    { LG_TILE, 0, 6551, 8 }, /* 268 Kraka-tur island */
    { LG_FIGHT, 2868, 6551 }, /* 269 Kraka-tur */
    { LG_EQUIP, 0, 2857 }, /* 270 */
    { LG_TILE, 0, 0, 8 }, /* 271 Nexus */
    { LG_UNEQUIP, 0, 2857 }, /* 272 */
    { LG_TILE, 2549, 0, 8, 1039, 1039 }, /* 273 castle island */
    { LG_USE, 2644, 0, 73, 696, 93 }, /* 274 original castle entrance */
    { LG_TILE, 0, 0, 73, 653, 93 }, /* 275 castle stairs */
    { LG_TILE, 0, 0, 73, 553, 106 }, /* 276 castle stairs */
    { LG_TILE, 3070, 0, 73, 428, 112 }, /* 277 chamber trigger */
    { LG_SAVE, 0, 0, 0, 0, 0, true }, /* 278 all original deaths/checkpoint */
    { LG_LOAD }, /* 279 unanimous restoration of native quest state */
    { LG_TALK, 1105, -98 } /* 280 offered original alliance ending */
};
static_assert(SDL_arraysize(legacy_stages) == EVIL_ENDING_PHASES - LEGACY_FIRST + 1, "legacy stage count");
static const LegacyStage* legacy_stage(unsigned phase) { return &legacy_stages[phase - LEGACY_FIRST]; }
static unsigned evil_floor_start;
static unsigned evil_void_start;
static const int evil_void_landings[][3] = {
    { 8, 947, 1100 }, { 72, 361, 349 }, { 8, 1077, 875 }, { 73, 696, 93 }
};
static unsigned legacy_epochs(unsigned phase)
{
    if (evil_mode && phase == 239) return 4u - evil_void_start;
    const LegacyStage* stage = legacy_stage(phase);
    return (unsigned)stage->save + (stage->kind == LG_TRAVEL || stage->kind == LG_TILE || (stage->kind == LG_USE && phase != 147 && phase != 194) || phase == 167 || ((rescue_mode || evil_mode) && stage->kind == LG_TALK && stage->map) || stage->kind == LG_LOAD)
        + (evil_mode && phase == 232 ? 3u - evil_floor_start : evil_mode && (phase == 215 || phase == 240) ? 2u : evil_mode && phase == 226 ? 1u
                : rescue_mode && phase == 203                                           ? 1u
                : rescue_mode && phase == 204 && first_phase != 204                     ? 8u
                                                                                        : 0u);
}
static int evil_floor_gate(unsigned phase, unsigned delta)
{
    /* Retail2380 lands in the isolated altar section. Return through2381,
     * enter the third-floor detour2365, then take2383 to the main fourth floor.
     * Only its2388 staircase reaches the Device floor. */
    static const int gates[] = { 2381, 2365, 2383, 2388 };
    if (phase == 232) delta += evil_floor_start;
    return phase == 232 && delta < SDL_arraysize(gates) ? gates[delta] : legacy_stage(phase)->script;
}
static int64_t legacy_named(int name)
{
    int64_t object, fallback = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (!item_is_item(object) || obj_field_int32_get(object, OBJ_F_NAME) != name) continue;
            if (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT)) continue;
            int64_t parent = 0;
            item_parent(object, &parent);
            if (parent && coop_campaign_slot_for_object(parent) >= 0) return object;
            if (!fallback) fallback = object;
        } while (obj_inst_next(&object, &iterator));
    return fallback;
}
static int64_t legacy_enemy(void)
{
    int64_t object;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC && script_number(object, SAP_DYING) == 2972
                && !critter_is_dead(object) && !(obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return object;
        } while (obj_inst_next(&object, &iterator));
    return 0;
}
static int64_t legacy_item(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    if (stage->goal > 0 && stage->kind != LG_TALK && stage->kind != LG_RUNE) return legacy_named(stage->goal);
    if (stage->script < 0) {
        /* Generic written-item prototypes also back unrelated held documents.
         * Follow the original poem's permanent module identity after pickup. */
        ObjectID poem = { .type = OID_TYPE_GUID, .d.g.data = { 0x00, 0xb5, 0xb8, 0xa3, 0x50, 0x75, 0x2b, 0x4e, 0x88, 0xe2, 0x0d, 0x20, 0xf7, 0xf1, 0x05, 0x4c } };
        return obj_pool_perm_lookup(poem);
    }
    return stage->script > 0 ? scripted(stage->script, SAP_GET) : 0;
}
static unsigned legacy_actor(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    if (evil_mode) {
        if (phase >= EVIL_ENDING_FIRST) {
            int64_t ring = legacy_named(2857), parent = 0;
            if (ring && item_parent(ring, &parent))
                for (unsigned owner = 0; owner < 4; owner++) if (pc(owner) == parent) return owner;
        }
        if (phase >= 229 && phase <= 232 && script_global_var_get(1808) == (int)phase)
            return (unsigned)script_global_var_get(1809);
        if (phase == 237) {
            int64_t item = legacy_named(5616), parent = 0;
            if (item && item_parent(item, &parent))
                for (unsigned i = 0; i < 4; i++)
                    if (pc(i) == parent) return i;
            return (unsigned)script_global_var_get(1814);
        }
        return phase == 215 || phase == 220 ? 2 : phase == 224 || phase == 225 ? 3
                                                                               : 1;
    }
    if (stage->kind == LG_COMPANION) {
        int64_t npc = scripted(stage->script, SAP_DIALOG), owner = npc ? critter_pc_leader_get(npc) : 0;
        for (unsigned i = 0; owner && i < 4; i++)
            if (pc(i) == owner) return i;
    }
    if (phase == 137 || phase == 176 || phase == 177 || (combat_finale_mode && phase >= 208)) {
        int64_t item = legacy_named(phase == 137 ? 5615 : 5616), parent = 0;
        if (item && item_parent(item, &parent))
            for (unsigned i = 0; i < 4; i++)
                if (pc(i) == parent) return i;
    }
    if (stage->kind == LG_EQUIP || stage->kind == LG_READ || stage->kind == LG_RUNE || stage->kind == LG_DROP) {
        int64_t item = stage->kind == LG_READ ? legacy_item(phase) : legacy_named(traversal_mode ? 2806 : 2848), parent = 0;
        if (item && item_parent(item, &parent))
            for (unsigned i = 0; i < 4; i++)
                if (pc(i) == parent) return i;
    }
    return stage->kind == LG_TALK ? (phase == 167 ? 3 : 2) : 1;
}
static int64_t branch_target(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    int sap = stage->kind == LG_USE ? SAP_USE : SAP_DIALOG;
    if (!rescue_mode || stage->script != 1749) return find_attachment(stage->script, sap);
    map_precache_sectors(obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION));
    int64_t object, found = 0, distance = INT64_MAX;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (script_number(object, SAP_DIALOG) != 1749 || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)) continue;
            int64_t candidate = object_dist(pc(legacy_actor(phase)), object);
            if (candidate > 128) continue;
            if (candidate < distance) {
                distance = candidate;
                found = object;
            }
        } while (obj_inst_next(&object, &iterator));
    return found;
}
static int64_t branch_generator(unsigned phase)
{
    int64_t object, found = 0, distance = INT64_MAX;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (script_number(object, SAP_DIALOG) != 1749
                || !(obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)) continue;
            int64_t candidate = object_dist(pc(legacy_actor(phase)), object);
            if (candidate > 128) continue;
            if (candidate < distance) {
                distance = candidate;
                found = object;
            }
        } while (obj_inst_next(&object, &iterator));
    return found;
}
static int64_t combat_finale_target(void)
{
    int64_t object, found = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            int prototype = sub_49B290(object);
            if (prototype == 27368 || prototype == 27369) return object;
            if (script_number(object, SAP_DIALOG) == 1105) found = object;
        } while (obj_inst_next(&object, &iterator));
    return found;
}
static bool evil_death_handler(int number)
{
    /* Original wrappers which increment Stillwater's counter or call1199.
     * The native scripts, not the fixture, decide when the town is destroyed. */
    static const int original[] = { 1199, 1484, 1675, 2170, 2185, 2186, 2192, 2893 };
    for (unsigned i = 0; i < SDL_arraysize(original); i++)
        if (original[i] == number) return true;
    return false;
}
static int64_t evil_town_enemy(void)
{
    if (script_global_flag_get(2010)) return 0;
    int64_t object, found = 0, nearest = INT64_MAX;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)
                || !evil_death_handler(script_number(object, SAP_DYING))
                || (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            int64_t distance = object_dist(pc(1), object);
            if (distance < nearest) {
                nearest = distance;
                found = object;
            }
        } while (obj_inst_next(&object, &iterator));
    return found;
}
static bool evil_town_prepare(void)
{
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++)
            map_precache_sectors(location_make(76113 + x * 64, 48162 + y * 64));
    int64_t object;
    int iterator;
    unsigned living = 0;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)
                || !evil_death_handler(script_number(object, SAP_DYING))) continue;
            /* The retained forest fixture quieted these original inhabitants.
             * Restore those actors; do not alter their HP, scripts or counters. */
            object_flags_unset(object, OF_OFF);
            living++;
        } while (obj_inst_next(&object, &iterator));
    fprintf(stderr, "ARCMP evil native town living=%u deaths=%d population=%d destroyed=%d\n",
        living, script_global_var_get(1008), script_global_var_get(1062), script_global_flag_get(2010));
    return living || script_global_flag_get(2010);
}
static int64_t evil_approach_point(int64_t actor, int64_t target, bool* reachable)
{
    if (reachable) *reachable = false;
    int64_t loc = obj_field_int64_get(target, OBJ_F_LOCATION), goals[64];
    int64_t from = obj_field_int64_get(actor, OBJ_F_LOCATION);
    unsigned count = 0;
    bool portal = obj_field_int32_get(target, OBJ_F_TYPE) == OBJ_TYPE_PORTAL;
    for (int radius = portal ? 1 : 2; radius <= (portal ? 1 : 3); radius++)
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                if (portal && dx && dy) continue;
                int64_t near = location_make(location_get_x(loc) + dx, location_get_y(loc) + dy);
                if (!tile_is_blocking(near, false)) goals[count++] = near;
            }
    /* A forty-goal heuristic exhausts the instrumented engine's work budget
     * on a distant outdoor approach. Try nearest individual floor endpoints
     * first. A failed search must not replace them with an occupied scenery
     * tile; any fallback is still reached only by an ordinary public Move. */
    for (unsigned i = 0; i < count; i++)
        for (unsigned j = i + 1; j < count; j++)
            if (location_dist(from, goals[j]) < location_dist(from, goals[i])) {
                int64_t swap = goals[i];
                goals[i] = goals[j];
                goals[j] = swap;
            }
    int8_t rotations[4096];
    unsigned attempts = 0;
    int steps = 0;
    tig_timestamp_t began;
    tig_timer_now(&began);
    int64_t chosen = count ? goals[0] : loc;
    for (unsigned i = 0; i < count && tig_timer_elapsed(began) < 200; i++) {
        PathCreateInfo path = { .obj = actor, .from = from, .to = goals[i], .max_rotations = sizeof(rotations), .rotations = rotations, .flags = PATH_FLAG_0x0010 };
        attempts++;
        steps = path_make_to_any(&path, &goals[i], 1, 100);
        if (steps || goals[i] == from) {
            if (reachable) *reachable = true;
            chosen = path.to;
            break;
        }
    }
    fprintf(stderr, "ARCMP original approach target=%d candidates=%u attempts=%u prefix=%d elapsed=%llu from=%lld,%lld to=%lld,%lld\n",
        obj_field_int32_get(target, OBJ_F_NAME), count, attempts, steps, (unsigned long long)tig_timer_elapsed(began),
        (long long)location_get_x(from), (long long)location_get_y(from), (long long)location_get_x(chosen), (long long)location_get_y(chosen));
    return chosen;
}
static int64_t evil_spare_weapon(int64_t actor);
static bool evil_prepare_hint(unsigned phase, int64_t target)
{
    if (!target) return false;
    bool reachable;
    int64_t actor = pc(legacy_actor(phase));
    int64_t loc = evil_approach_point(actor, target, &reachable);
    script_global_var_set(1818, 0);
    /* A locked native door can separate stairs from the reachable floor.
     * Search the reachable frontier, including corridor doors far from the
     * final staircase. The former eight-tile target radius missed those gates.
     * Every approach and door action still uses ordinary owned commands. */
    if ((!reachable || (phase >= 229 && phase <= 232)) && legacy_stage(phase)->kind == LG_USE) {
        int64_t door, frontier = 0, frontier_loc = 0, frontier_actor = actor; int iterator;
        int64_t best = INT64_MAX;
        if (!reachable && obj_inst_first(&door, &iterator)) do {
            if (obj_field_int32_get(door, OBJ_F_TYPE) != OBJ_TYPE_PORTAL || !object_locked_get(door) || object_is_busted(door) || portal_is_open(door)
                || (obj_field_int32_get(door, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
                || object_dist(actor, door) > 256 || object_dist(door, target) > 256) continue;
            int64_t score = object_dist(door, target) * 4 + object_dist(actor, door);
            if (score >= best) continue;
            bool accessible;
            int64_t approach = evil_approach_point(actor, door, &accessible);
            if (accessible) { frontier = door; frontier_loc = approach; best = score; }
        } while (obj_inst_next(&door, &iterator));
        if (obj_inst_first(&door, &iterator)) do {
            if (obj_field_int32_get(door, OBJ_F_TYPE) != OBJ_TYPE_PORTAL || !object_locked_get(door) || object_is_busted(door)
                || (obj_field_int32_get(door, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
                || (frontier ? door != frontier : object_dist(door, target) > 8)) continue;
            if (!evil_spare_weapon(actor)) for (unsigned slot = 0; slot < 4; slot++) {
                if (stat_level_get(pc(slot), STAT_FATE_POINTS) <= 0 && !fate_is_activated(pc(slot), FATE_CRIT_SUCCESS_PICK_LOCKS)) continue;
                bool tools = false;
                for (unsigned donor = 0; donor < 4; donor++) tools |= item_find_first_generic(pc(donor), OGF_IS_LOCKPICK) != 0;
                if (!tools) break;
                script_global_var_set(1808, (int)phase); script_global_var_set(1809, (int)slot);
                actor = pc(slot);
                fprintf(stderr, "ARCMP original door uses existing Fate and party lockpicks owner=%u\n", slot);
                break;
            }
            if (obj_field_int32_get(door, OBJ_F_PORTAL_FLAGS) & OPF_JAMMED) {
                /* A jammed lock cannot use the retained lockpicker. Choose
                 * the party's existing strongest basher instead of repeatedly
                 * asking that same weak character to punch a metal door. */
                for (unsigned slot = 0; slot < 4; slot++)
                    if (critter_is_active(pc(slot)) && stat_level_get(pc(slot), STAT_DAMAGE_BONUS) > stat_level_get(actor, STAT_DAMAGE_BONUS)) {
                        script_global_var_set(1808, (int)phase); script_global_var_set(1809, (int)slot); actor = pc(slot);
                    }
            } else if (!fate_is_activated(actor, FATE_CRIT_SUCCESS_PICK_LOCKS) && stat_level_get(actor, STAT_FATE_POINTS) <= 0) {
                /* Use the party's existing trained lockpicker when Fate is
                 * spent; a fixed stair user need not pick every later door. */
                for (unsigned slot = 0; slot < 4; slot++)
                    if (critter_is_active(pc(slot)) && tech_skill_level(pc(slot), TECH_SKILL_PICK_LOCKS) > tech_skill_level(actor, TECH_SKILL_PICK_LOCKS)) {
                        script_global_var_set(1808, (int)phase); script_global_var_set(1809, (int)slot); actor = pc(slot);
                    }
            }
            if (frontier && actor == frontier_actor) { loc = frontier_loc; reachable = true; }
            else loc = evil_approach_point(actor, door, &reachable);
            if (!reachable) continue;
            int64_t doorway = obj_field_int64_get(door, OBJ_F_LOCATION);
            script_global_var_set(1816, (int)location_get_x(doorway)); script_global_var_set(1817, (int)location_get_y(doorway));
            script_global_var_set(1818, (int)phase);
            fprintf(stderr, "ARCMP original locked door phase=%u at=%lld,%lld target-script=%d\n", phase,
                (long long)location_get_x(doorway), (long long)location_get_y(doorway), legacy_stage(phase)->script);
            int64_t weapon = item_wield_get(actor, ITEM_INV_LOC_WEAPON);
            fprintf(stderr, "ARCMP original door lock flags=%x difficulty=%d owner-level=%d scheme=%d lockpick=%d training=%d\n",
                obj_field_int32_get(door, OBJ_F_PORTAL_FLAGS), obj_field_int32_get(door, OBJ_F_PORTAL_LOCK_DIFFICULTY),
                stat_level_get(actor, STAT_LEVEL), obj_field_int32_get(actor, OBJ_F_CRITTER_AUTO_LEVEL_SCHEME),
                tech_skill_points_get(actor, TECH_SKILL_PICK_LOCKS), tech_skill_training_get(actor, TECH_SKILL_PICK_LOCKS));
            fprintf(stderr, "ARCMP original door state hp=%d/%d material=%d weapon=%d hp=%d/%d points=%d willpower=%d conveyance=%d unlocking=%d\n",
                object_hp_current(door), object_hp_max(door), obj_field_int32_get(door, OBJ_F_MATERIAL), weapon ? sub_49B290(weapon) : 0,
                weapon ? object_hp_current(weapon) : 0, weapon ? object_hp_max(weapon) : 0, stat_level_get(actor, STAT_UNSPENT_POINTS),
                stat_level_get(actor, STAT_WILLPOWER), spell_college_level_get(actor, COLLEGE_CONVEYANCE), spell_is_known(actor, SPELL_UNLOCKING_CANTRIP));
            for (unsigned slot = 0; slot < 4; slot++) {
                int64_t traveller = pc(slot);
                fprintf(stderr, "ARCMP original route combat slot=%u strength=%d damage-bonus=%d melee=%d hp=%d/%d\n", slot,
                    stat_level_get(traveller, STAT_STRENGTH), stat_level_get(traveller, STAT_DAMAGE_BONUS),
                    basic_skill_level(traveller, BASIC_SKILL_MELEE), object_hp_current(traveller), object_hp_max(traveller));
                fprintf(stderr, "ARCMP original route resources slot=%u CP=%d Fate=%d locks=%d lockpicks=%lld spells=", slot,
                    stat_level_get(traveller, STAT_UNSPENT_POINTS), stat_level_get(traveller, STAT_FATE_POINTS),
                    tech_skill_level(traveller, TECH_SKILL_PICK_LOCKS), (long long)item_find_by_name(traveller, BP_LOCKPICKS));
                for (int spell = 0; spell < SPELL_COUNT; spell++) if (spell_is_known(traveller, spell)) fprintf(stderr, "%d,", spell);
                fputc('\n', stderr);
                int length = obj_field_int32_get(traveller, OBJ_F_CRITTER_INVENTORY_NUM);
                for (int i = 0; i < length; i++) {
                    int64_t item = obj_arrayfield_handle_get(traveller, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
                    if (!item) continue;
                    fprintf(stderr, "ARCMP original route equipment slot=%u prototype=%d name=%d type=%d hp=%d/%d\n", slot,
                        sub_49B290(item), obj_field_int32_get(item, OBJ_F_NAME), obj_field_int32_get(item, OBJ_F_TYPE),
                        object_hp_current(item), object_hp_max(item));
                }
            }
            break;
        } while (obj_inst_next(&door, &iterator));
    }
    script_global_var_set(1810, (int)location_get_x(loc));
    script_global_var_set(1811, (int)location_get_y(loc));
    script_global_var_set(1812, (int)phase);
    script_global_var_set(1813, (int)coop_runtime_epoch());
    if (phase == 224) script_global_var_set(1815, script_global_flag_get(2087));
    return true;
}
static bool evil_walk_to_enemy(unsigned slot, int64_t target)
{
    return coop_runtime_move(evil_approach_point(pc(slot), target, NULL), true);
}
static int64_t evil_route_door(unsigned phase)
{
    if (script_global_var_get(1818) != (int)phase) return 0;
    int64_t door, location = location_make(script_global_var_get(1816), script_global_var_get(1817)); int iterator;
    if (obj_inst_first(&door, &iterator)) do {
        if (obj_field_int32_get(door, OBJ_F_TYPE) == OBJ_TYPE_PORTAL && obj_field_int64_get(door, OBJ_F_LOCATION) == location
            && !(obj_field_int32_get(door, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
            && object_locked_get(door) && !object_is_busted(door) && !portal_is_open(door)) return door;
    } while (obj_inst_next(&door, &iterator));
    return 0;
}
/* Use the ordinary character editor and casting requests when the retained
 * character can afford Unlocking Cantrip. Never grant points or spell knowledge. */
static bool evil_unlock_door(CoopGameplay* game, int64_t actor, int64_t door)
{
    int spell = SPELL_UNLOCKING_CANTRIP;
    CoopProgression* progression = coop_gameplay_progression(game);
    if (!spell_is_known(actor, spell)) {
        int intelligence = spell_min_intelligence(spell) - stat_level_get(actor, STAT_INTELLIGENCE);
        int willpower = spell_min_willpower(spell) - stat_level_get(actor, STAT_WILLPOWER);
        int need = 2 - spell_college_level_get(actor, COLLEGE_CONVEYANCE);
        if (intelligence > 0) need += intelligence;
        if (willpower > 0) need += willpower;
        if (spell_min_level(spell) > stat_level_get(actor, STAT_LEVEL) || need > stat_level_get(actor, STAT_UNSPENT_POINTS)) return false;
        if (anim_is_current_goal_type(actor, AG_ATTACK, NULL)) {
            coop_runtime_halt(obj_field_int64_get(actor, OBJ_F_LOCATION));
        } else if (!charedit_is_created()) {
            charedit_open(actor, CHAREDIT_MODE_ACTIVE);
        } else if (coop_progression_lease(progression) && !coop_progression_busy(progression)) {
            if (intelligence > 0) coop_runtime_progression_change(actor, COOP_PROGRESS_STAT, STAT_INTELLIGENCE, true);
            else if (willpower > 0) coop_runtime_progression_change(actor, COOP_PROGRESS_STAT, STAT_WILLPOWER, true);
            else coop_runtime_progression_change(actor, COOP_PROGRESS_COLLEGE, COLLEGE_CONVEYANCE, true);
        }
    } else if (charedit_is_created()) {
        charedit_close();
    } else if (!coop_progression_editing(progression) && !coop_progression_busy(progression)) {
        if (anim_is_current_goal_type(actor, AG_ATTACK, NULL)) coop_runtime_halt(obj_field_int64_get(actor, OBJ_F_LOCATION));
        else if (!coop_magic_busy(coop_gameplay_magic(game))) {
            TargetDescriptor target = { .obj = door };
            coop_runtime_cast(actor, spell, &target, false);
        }
    }
    return true;
}
static bool evil_pick_door(CoopGameplay* game, int64_t actor, int64_t door)
{
    static ObjectID previous;
    static uint32_t epoch;
    static bool halted;
    ObjectID id = obj_get_id(door);
    if (epoch != coop_runtime_epoch() || !objid_is_equal(id, previous)) {
        previous = id; epoch = coop_runtime_epoch(); halted = false;
    }
    int64_t tool = item_find_first_generic(actor, OGF_IS_LOCKPICK);
    if (!tool || (obj_field_int32_get(door, OBJ_F_PORTAL_FLAGS) & OPF_JAMMED)) return false;
    /* Replica presentation retains animation goals after host Halt. Do not
     * repeatedly cancel the next action by treating that local pose as authority. */
    if (!halted)
        halted = coop_runtime_halt(obj_field_int64_get(actor, OBJ_F_LOCATION));
    else if (!fate_is_activated(actor, FATE_CRIT_SUCCESS_PICK_LOCKS) && stat_level_get(actor, STAT_FATE_POINTS) > 0) {
        if (!coop_progression_busy(coop_gameplay_progression(game))) coop_runtime_fate(actor, FATE_CRIT_SUCCESS_PICK_LOCKS, true);
    } else if (!coop_magic_busy(coop_gameplay_magic(game)) && !coop_progression_busy(coop_gameplay_progression(game))) {
        TargetDescriptor target = { .obj = door };
        coop_runtime_use_item(tool, &target, false);
    }
    return true;
}
static bool evil_share_door_tool(CoopGameplay* game, unsigned slot, unsigned actor)
{
    if (item_find_first_generic(pc(actor), OGF_IS_LOCKPICK)) return false;
    for (unsigned donor = 0; donor < 4; donor++) {
        int64_t tool = item_find_first_generic(pc(donor), OGF_IS_LOCKPICK);
        if (!tool) continue;
        if (slot == donor && !coop_items_busy(coop_gameplay_items(game))) {
            if (object_dist(pc(donor), pc(actor)) > 8) coop_runtime_move(evil_approach_point(pc(donor), pc(actor), NULL), true);
            else coop_runtime_item(COOP_ITEM_GIVE, tool, (uint8_t)coop_campaign_slot_for_object(pc(actor)), -1);
        }
        return true;
    }
    return false;
}
static void evil_yield_door(unsigned slot, unsigned actor, int64_t door)
{
    if (slot == actor) return;
    /* Independent humans can block an intermediate doorway while another
     * owner approaches a distant locked door. Each controller moves only its
     * own PC, serializing those small moves so they choose different spaces. */
    int64_t candidate; int iterator;
    int64_t best = INT64_MAX;
    if (obj_inst_first(&candidate, &iterator)) do {
        if (obj_field_int32_get(candidate, OBJ_F_TYPE) != OBJ_TYPE_PORTAL
            || (obj_field_int32_get(candidate, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
            || object_dist(pc(actor), candidate) > 3 || object_dist(pc(slot), candidate) > 2) continue;
        int64_t distance = object_dist(pc(actor), candidate);
        if (distance < best) { door = candidate; best = distance; }
    } while (obj_inst_next(&candidate, &iterator));
    static ObjectID previous;
    static uint32_t epoch;
    static bool yielded;
    ObjectID id = obj_get_id(door);
    if (epoch != coop_runtime_epoch() || !objid_is_equal(id, previous)) {
        previous = id; epoch = coop_runtime_epoch(); yielded = false;
    }
    if (yielded || object_dist(pc(slot), door) > 2 || (SDL_GetTicks() / 400) % 4 != slot) return;
    int64_t from = obj_field_int64_get(pc(slot), OBJ_F_LOCATION);
    for (int radius = 3; radius >= 1; radius--) for (int dy = -radius; dy <= radius; dy++) for (int dx = -radius; dx <= radius; dx++) {
        if (abs(dx) != radius && abs(dy) != radius) continue;
        int64_t to = location_make(location_get_x(from) + dx, location_get_y(from) + dy);
        if (tile_is_blocking(to, false) || location_dist(to, obj_field_int64_get(door, OBJ_F_LOCATION)) <= 2) continue;
        bool occupied = false;
        for (unsigned other = 0; other < 4; other++) occupied |= obj_field_int64_get(pc(other), OBJ_F_LOCATION) == to;
        if (occupied) continue;
        int8_t rotations[32];
        PathCreateInfo path = { .obj = pc(slot), .from = from, .to = to, .max_rotations = sizeof(rotations), .rotations = rotations };
        if (path_make(&path) <= 0) continue;
        yielded = coop_runtime_move(to, false);
        if (yielded) return;
    }
}
static bool evil_continue_walk(CoopGameplay* game, int64_t actor, int64_t destination, int64_t locked)
{
    if (coop_items_busy(coop_gameplay_items(game))) return false;
    int64_t candidate, corridor = 0; int iterator;
    if (obj_inst_first(&candidate, &iterator)) do {
        if (obj_field_int32_get(candidate, OBJ_F_TYPE) == OBJ_TYPE_PORTAL && candidate != locked
            && !object_locked_get(candidate) && !portal_is_open(candidate)
            && !(obj_field_int32_get(candidate, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
            && object_dist(actor, candidate) <= 3) { corridor = candidate; break; }
    } while (obj_inst_next(&candidate, &iterator));
    return corridor ? coop_runtime_world_use(corridor) : coop_runtime_move(destination, true);
}
static int64_t evil_spare_weapon(int64_t actor)
{
    int count = obj_arrayfield_length_get(actor, OBJ_F_CRITTER_INVENTORY_LIST_IDX);
    for (int i = 0; i < count; i++) {
        int64_t item = obj_arrayfield_handle_get(actor, OBJ_F_CRITTER_INVENTORY_LIST_IDX, i);
        if (item && obj_field_int32_get(item, OBJ_F_TYPE) == OBJ_TYPE_WEAPON
            && object_hp_current(item) > 0 && item_check_remove(item) == ITEM_CANNOT_OK
            && sub_464D20(item, ITEM_INV_LOC_WEAPON, actor) == ITEM_CANNOT_OK) return item;
    }
    return 0;
}
static void evil_share_door_weapon(CoopGameplay* game, unsigned slot, unsigned actor)
{
    if (item_wield_get(pc(actor), ITEM_INV_LOC_WEAPON) || coop_items_busy(coop_gameplay_items(game))) return;
    int64_t spare = evil_spare_weapon(pc(actor));
    if (slot == actor) {
        if (spare) coop_runtime_item(COOP_ITEM_MOVE, spare, 0, ITEM_INV_LOC_WEAPON);
    } else if (!spare) {
        for (unsigned donor = 0; donor < 4; donor++) {
            if (donor == actor) continue;
            int64_t weapon = evil_spare_weapon(pc(donor));
            if (!weapon) continue;
            if (slot == donor) coop_runtime_item(COOP_ITEM_GIVE, weapon, (uint8_t)coop_campaign_slot_for_object(pc(actor)), -1);
            break;
        }
    }
}
static void evil_inspect_approach(unsigned phase, int64_t target)
{
    if (!getenv("ARCMP_TRACE_WORLD")) return;
    int64_t actor = pc(legacy_actor(phase)), from = obj_field_int64_get(actor, OBJ_F_LOCATION);
    int64_t to = obj_field_int64_get(target, OBJ_F_LOCATION);
    for (unsigned i = 0; i < 4; i++) {
        int64_t human = pc(i), position = obj_field_int64_get(human, OBJ_F_LOCATION);
        fprintf(stderr, "ARCMP native approach human=%u x=%lld y=%lld critter=%x spell=%x active=%d stance=%d\n", i,
            (long long)location_get_x(position), (long long)location_get_y(position), obj_field_int32_get(human, OBJ_F_CRITTER_FLAGS),
            obj_field_int32_get(human, OBJ_F_SPELL_FLAGS), critter_is_active(human), combat_critter_is_combat_mode_active(human));
    }
    location_origin_set(from); gamelib_redraw(); iso_redraw(); tig_window_display();
    char frame[80]; snprintf(frame, sizeof(frame), "arcmp-evil-approach-%u.bmp", phase); tig_video_screenshot_save(frame);
    int ax = (int)location_get_x(from), ay = (int)location_get_y(from);
    int tx = (int)location_get_x(to), ty = (int)location_get_y(to);
    int left = (ax < tx ? ax : tx) - 64, top = (ay < ty ? ay : ty) - 64;
    int right = (ax > tx ? ax : tx) + 64, bottom = (ay > ty ? ay : ty) + 64;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    int width = right - left + 1, height = bottom - top + 1;
    if ((int64_t)width * height > 262144) return;
    uint8_t* pixels = calloc((size_t)width * height, 1);
    if (!pixels) return;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int64_t loc = location_make(left + x, top + y);
            if (sector_exists(sector_id_from_loc(loc))) pixels[(size_t)y * width + x] = tile_is_blocking(loc, false) ? 70 : 230;
        }
    char file[80];
    snprintf(file, sizeof(file), "arcmp-evil-approach-%u.pgm", phase);
    FILE* raster = fopen(file, "wb");
    if (raster) {
        fprintf(raster, "P5\n%d %d\n255\n", width, height);
        fwrite(pixels, 1, (size_t)width * height, raster);
        fclose(raster);
    }
    free(pixels);
    snprintf(file, sizeof(file), "arcmp-evil-approach-%u.json", phase);
    FILE* json = fopen(file, "wb");
    if (!json) return;
    fprintf(json, "{\"map\":%d,\"left\":%d,\"top\":%d,\"actor\":[%d,%d],\"target\":[%d,%d],\"objects\":[", map_current_map(), left, top, ax, ay, tx, ty);
    int64_t obj;
    int iterator;
    unsigned count = 0;
    if (obj_inst_first(&obj, &iterator)) do {
            int type = obj_field_int32_get(obj, OBJ_F_TYPE);
            int64_t loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
            int x = (int)location_get_x(loc), y = (int)location_get_y(loc);
            if (x < left || x > right || y < top || y > bottom) continue;
            int use = script_number(obj, SAP_USE);
            bool critter = obj_type_is_critter(type);
            if (critter && (obj_field_int32_get(obj, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) continue;
            if (!critter && type != OBJ_TYPE_PORTAL && type != OBJ_TYPE_WALL && !use && obj != target) continue;
            int owner = critter ? coop_campaign_slot_for_object(type == OBJ_TYPE_PC ? obj : critter_pc_leader_get(obj)) : -1;
            fprintf(json, "%s{\"type\":%d,\"name\":%d,\"x\":%d,\"y\":%d,\"flags\":%u,\"use\":%d,\"first\":%d,\"heartbeat\":%d,\"open\":%d,\"locked\":%d,\"human_owner\":%d}",
                count++ ? "," : "", type, obj_field_int32_get(obj, OBJ_F_NAME), x, y, (unsigned)obj_field_int32_get(obj, OBJ_F_FLAGS), use,
                script_number(obj, SAP_FIRST_HEARTBEAT), script_number(obj, SAP_HEARTBEAT), type == OBJ_TYPE_PORTAL && portal_is_open(obj),
                type == OBJ_TYPE_PORTAL && object_locked_get(obj), owner);
        } while (obj_inst_next(&obj, &iterator));
    fputs("]}\n", json);
    fclose(json);
    if (phase == 224)
        for (unsigned i = 0; i < 4; i++)
            fprintf(stderr, "ARCMP original KanHua gate member=%u rep1025=%d quest1024=%d quest1026=%d first=%d heartbeat=%d\n", i,
                reputation_has(pc(i), 1025), quest_state_get(pc(i), 1024), quest_state_get(pc(i), 1026), script_number(target, SAP_FIRST_HEARTBEAT), script_number(target, SAP_HEARTBEAT));
}
static int64_t evil_threat(unsigned slot)
{
    int64_t object, nearest = INT64_MAX, found = 0;
    int iterator;
    if (obj_inst_first(&object, &iterator)) do {
            if (obj_field_int32_get(object, OBJ_F_TYPE) != OBJ_TYPE_NPC || critter_is_dead(object)
                || (obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))
                || (obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)
                || critter_pc_leader_get(object) || !combat_critter_is_combat_mode_active(object)) continue;
            int64_t focus = obj_field_handle_get(object, OBJ_F_NPC_COMBAT_FOCUS);
            if (!focus || !obj_handle_is_valid(focus)) continue;
            int64_t leader = critter_pc_leader_get(focus);
            if (coop_campaign_slot_for_object(focus) < 0 && coop_campaign_slot_for_object(leader) < 0) continue;
            int64_t distance = object_dist(pc(slot), object);
            /* The original Void retains hostiles on disconnected islands.
             * A combat focus alone does not make a distant actor reachable. */
            if (distance > 60) continue;
            if (distance < nearest) {
                nearest = distance;
                found = object;
            }
        } while (obj_inst_next(&object, &iterator));
    return found;
}
static bool legacy_tile(int64_t loc, Script* script)
{
    Sector* sector;
    int64_t id = sector_id_from_loc(loc);
    if (!sector_lock(id, &sector)) return false;
    bool good = tile_script_list_get(&sector->tile_scripts, tile_id_from_loc(loc), script);
    sector_unlock(id);
    return good;
}
static bool evil_void_prepare(unsigned step)
{
    script_global_var_set(1818, 0);
    if (step >= 4) { script_global_var_set(1813, (int)coop_runtime_epoch()); return true; }
    if (step == 0 || step == 2) {
        int x = step == 0 ? 1037 : 351, y = step == 0 ? 1191 : 307;
        int64_t loc = location_make(x, y);
        map_precache_sectors(loc);
        if (step == 0) {
            Script script;
            if (map_current_map() != 8 || !legacy_tile(loc, &script) || script.num != 2552) return false;
        } else {
            JumpPoint jump;
            if (map_current_map() != 72 || !jumppoint_get(loc, &jump) || jump.dst_map != 8
                || jump.dst_loc != location_make(1077, 875)) return false;
        }
        script_global_var_set(1810, x); script_global_var_set(1811, y);
        script_global_var_set(1812, 239); script_global_var_set(1813, (int)coop_runtime_epoch());
    } else {
        bool guard = step == 3 && !script_global_flag_get(2718);
        int64_t target = find_attachment(step == 1 ? 2642 : guard ? 1111 : 2644, guard ? SAP_DIALOG : SAP_USE);
        if (!target || !evil_prepare_hint(239, target)) return false;
    }
    fprintf(stderr, "ARCMP original Void route step=%u map=%d hint=%d,%d epoch=%u\n", step, map_current_map(),
        script_global_var_get(1810), script_global_var_get(1811), coop_runtime_epoch());
    return true;
}
static int64_t legacy_tile_near(int number, int64_t anchor)
{
    int64_t centre = sector_id_from_loc(anchor);
    for (int radius = 0; radius <= 4; radius++)
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                int64_t id = SECTOR_MAKE(SECTOR_X(centre) + dx, SECTOR_Y(centre) + dy);
                if (!sector_exists(id)) continue;
                Sector* sector;
                if (!sector_lock(id, &sector)) return 0;
                int64_t loc = 0;
                for (TileScriptListNode* node = sector->tile_scripts.head; node; node = node->next)
                    if (node->scr.num == number) {
                        loc = location_make((SECTOR_X(id) << 6) | TILE_X(node->id), (SECTOR_Y(id) << 6) | TILE_Y(node->id));
                        break;
                    }
                sector_unlock(id);
                if (loc) return loc;
            }
    return 0;
}
static int64_t legacy_rune(int number)
{
    return legacy_tile_near(number, location_make(28388, 31169));
}
static int64_t evil_being(int name)
{
    int64_t object; int iterator;
    if (obj_inst_first(&object, &iterator)) do {
        if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && obj_field_int32_get(object, OBJ_F_NAME) == name
            && !(obj_field_int32_get(object, OBJ_F_NPC_FLAGS) & ONF_GENERATOR)
            && !(obj_field_int32_get(object, OBJ_F_FLAGS) & (OF_DESTROYED | OF_EXTINCT))) return object;
    } while (obj_inst_next(&object, &iterator));
    return 0;
}
static bool evil_ending_prepare(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    script_global_var_set(1822, 0);
    if (stage->kind == LG_LOAD) { script_global_var_set(1878, 9000); return true; }
    if (stage->kind == LG_SAVE || stage->kind == LG_TALK || stage->kind == LG_EQUIP || stage->kind == LG_UNEQUIP) return true;
    int64_t limit_x, limit_y;
    if (stage->goal && (stage->kind == LG_FIGHT || stage->kind == LG_TILE) && !evil_being(stage->goal)) {
        /* Load original sectors to discover the original actor. No relocation,
         * spawn, visibility, quest or death-state changes are permitted. */
        sector_limits_get(&limit_x, &limit_y);
        for (int64_t y = 0; y < limit_y && !evil_being(stage->goal); y++) for (int64_t x = 0; x < limit_x && !evil_being(stage->goal); x++) {
            int64_t id = SECTOR_MAKE(x, y); Sector* sector;
            if (sector_exists(id) && sector_lock(id, &sector)) sector_unlock(id);
        }
    }
    if (stage->kind == LG_FIGHT) {
        int64_t being = evil_being(stage->goal);
        if (being) fprintf(stderr, "ARCMP original legendary target phase=%u name=%d dialog=%d dying=%d hp=%d flags=%x\n",
            phase, stage->goal, script_number(being, SAP_DIALOG), script_number(being, SAP_DYING), object_hp_current(being), obj_field_int32_get(being, OBJ_F_FLAGS));
        if (phase == 253 && !script_global_flag_get(2091)
            && !find_attachment(2629, SAP_DYING) && !find_attachment(1256, SAP_DYING)) return false;
        return being != 0;
    }
    int64_t destination = 0;
    int map = stage->map;
    if (stage->kind == LG_USE) {
        int64_t target = find_attachment(stage->script, SAP_USE);
        destination = location_make(stage->x, stage->y);
        if (!target || !evil_prepare_hint(phase, target)) return false;
    } else if (stage->kind == LG_TILE) {
        static const int portals[][4] = {
            {1039,1039,1077,875}, {1037,1046,942,843}, {1038,1054,915,991},
            {1046,1061,947,1100}, {1054,1061,1068,1166}, {1060,1055,1159,1178},
            {1061,1047,1161,1102}, {1055,1039,1173,1060}, {1046,1036,1167,865}
        };
        int64_t loc = location_make(stage->x, stage->y);
        int script = stage->script;
        if (script == 3069) {
            loc = legacy_tile_near(script, obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION));
            if (!loc) return false;
            destination = location_make(426, 109);
        } else if (stage->goal) {
            int64_t being = evil_being(stage->goal);
            if (!being || map_current_map() != 8) return false;
            int64_t nearest = INT64_MAX; unsigned chosen = 0;
            for (unsigned i = 0; i < SDL_arraysize(portals); i++) {
                int64_t distance = location_dist(obj_field_int64_get(being, OBJ_F_LOCATION), location_make(portals[i][2], portals[i][3]));
                if (distance < nearest) { nearest = distance; chosen = i; }
            }
            if (nearest > 100) return false;
            loc = location_make(portals[chosen][0], portals[chosen][1]); script = 2549 + (int)chosen;
            destination = location_make(portals[chosen][2], portals[chosen][3]);
        } else if (!stage->x && !stage->y) {
            int64_t here = obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION), nearest = INT64_MAX;
            for (int number = 2549; number <= 2558; number++) {
                int64_t candidate = legacy_tile_near(number, here);
                int64_t distance = candidate ? location_dist(here, candidate) : INT64_MAX;
                if (distance < nearest) { loc = candidate; script = number; nearest = distance; }
            }
            if (!loc || nearest > 100) return false;
            destination = location_make(1049, 1049);
        } else if (script >= 2549 && script <= 2557) {
            bool worn = item_inventory_location_get(legacy_named(2857)) == ITEM_INV_LOC_RING1 || item_inventory_location_get(legacy_named(2857)) == ITEM_INV_LOC_RING2;
            destination = worn ? location_make(1049, 1049) : location_make(portals[script - 2549][2], portals[script - 2549][3]);
        }
        map_precache_sectors(loc);
        if (script) {
            Script original;
            if (!legacy_tile(loc, &original) || original.num != script) return false;
            if (script == 3070) destination = location_make(96, 667);
            /* Portal stones attach the same original script to several
             * tiles. Walk onto a reachable attached tile, accounting for
             * terrain and the other humans standing around the Nexus. */
            int64_t actor = pc(legacy_actor(phase)), chosen = 0;
            for (int radius = 0; radius <= 2 && !chosen; radius++)
                for (int dy = -radius; dy <= radius && !chosen; dy++)
                    for (int dx = -radius; dx <= radius && !chosen; dx++) {
                        if (abs(dx) != radius && abs(dy) != radius) continue;
                        int64_t candidate = location_make(location_get_x(loc) + dx, location_get_y(loc) + dy);
                        Script attachment;
                        if (tile_is_blocking(candidate, false) || !legacy_tile(candidate, &attachment) || attachment.num != script) continue;
                        int8_t rotations[256];
                        PathCreateInfo path = { .obj = actor, .from = obj_field_int64_get(actor, OBJ_F_LOCATION),
                            .to = candidate, .rotations = rotations, .max_rotations = SDL_arraysize(rotations) };
                        if (path_make(&path) > 0) chosen = candidate;
                    }
            if (!chosen) return false;
            loc = chosen;
        } else {
            JumpPoint jump;
            if (!jumppoint_get(loc, &jump) || jump.dst_map != stage->map) return false;
            destination = jump.dst_loc;
        }
        script_global_var_set(1864, (int)location_get_x(loc)); script_global_var_set(1865, (int)location_get_y(loc));
    } else return false;
    script_global_var_set(1819, map); script_global_var_set(1820, (int)location_get_x(destination)); script_global_var_set(1821, (int)location_get_y(destination));
    fprintf(stderr, "ARCMP original evil itinerary phase=%u map=%d landing=%d,%d\n", phase, map, script_global_var_get(1820), script_global_var_get(1821));
    return destination != 0;
}
static bool evil_ending_landing(void)
{
    if (map_current_map() != script_global_var_get(1819)) return false;
    int64_t expected = location_make(script_global_var_get(1820), script_global_var_get(1821));
    for (unsigned owner = 0; owner < 4; owner++)
        if (location_dist(obj_field_int64_get(pc(owner), OBJ_F_LOCATION), expected) > 10) return false;
    return true;
}
static bool traversal_queue_arrivals(void)
{
    script_global_var_set(1850, 0);
    int64_t anchor = obj_field_int64_get(pc(legacy_actor(190)), OBJ_F_LOCATION);
    unsigned count = 0;
    for (int dx = 5; dx < 20 && count < 2; dx++) {
        int64_t loc = location_make(location_get_x(anchor) + dx, location_get_y(anchor));
        Script previous;
        if (legacy_tile(loc, &previous)) continue;
        ScriptAction actions[] = {
            { .type = SAT_MULTIPLY, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1850, 1850, 10 } },
            { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1850, 1850, (int)count + 1 } },
            { .type = SAT_REMOVE_THIS_SCRIPT }, { .type = SAT_RETURN_AND_RUN_DEFAULT }
        };
        if (!script_file(999934 + (int)count, actions, SDL_arraysize(actions), 0)) return false;
        Sector* sector;
        int64_t id = sector_id_from_loc(loc);
        if (!sector_lock(id, &sector)) return false;
        Script script = { .num = 999934 + (int)count };
        bool good = tile_script_list_set(&sector->tile_scripts, tile_id_from_loc(loc), &script);
        sector_unlock(id);
        script_global_var_set(1853 + (int)count * 2, (int)location_get_x(loc));
        script_global_var_set(1854 + (int)count * 2, (int)location_get_y(loc));
        TimeEvent event = { .type = TIMEEVENT_TYPE_COOP_ARRIVAL };
        event.params[0].object_value = pc(legacy_actor(190));
        event.params[1].location_value = loc;
        event.params[2].integer_value = map_current_map();
        DateTime delay = { .milliseconds = count + 1 };
        if (!good || !timeevent_add_delay(&event, &delay)) return false;
        count++;
    }
    script_global_var_set(1851, (int)count);
    return count == 2;
}
static bool traversal_tile_persistence(void)
{
    Script one, two;
    return script_global_var_get(1850) == 12
        && legacy_tile(location_make(script_global_var_get(1853), script_global_var_get(1854)), &one) && !one.num
        && legacy_tile(location_make(script_global_var_get(1855), script_global_var_get(1856)), &two) && !two.num;
}
static bool legacy_tile_oracle(bool prepare)
{
    if (prepare) {
        ScriptAction actions[] = {
            { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1878, 1878, 1 } },
            { .type = SAT_SET_LOCAL_FLAG, .op_type = { SVT_NUMBER }, .op_value = { 15 } },
            { .type = SAT_ADD, .op_type = { SVT_COUNTER, SVT_COUNTER, SVT_NUMBER }, .op_value = { 4, 4, 1 } },
            { .type = SAT_RETURN_AND_RUN_DEFAULT }
        };
        ScriptAction removed[] = { { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1879, 1879, 1 } }, { .type = SAT_REMOVE_THIS_SCRIPT }, { .type = SAT_RETURN_AND_RUN_DEFAULT } };
        if (!script_file(999932, actions, SDL_arraysize(actions), 0) || !script_file(999933, removed, SDL_arraysize(removed), 0)) return false;
        script_global_var_set(1878, 0);
        script_global_var_set(1879, 0);
        int64_t anchor = obj_field_int64_get(pc(0), OBJ_F_LOCATION);
        unsigned count = 0;
        for (int dx = 2; dx < 12 && count < 2; dx++) {
            int64_t loc = location_make(location_get_x(anchor) + dx, location_get_y(anchor));
            Script previous;
            if (legacy_tile(loc, &previous)) continue;
            Sector* sector;
            int64_t id = sector_id_from_loc(loc);
            if (!sector_lock(id, &sector)) return false;
            Script script = { .num = count ? 999933 : 999932 };
            bool good = tile_script_list_set(&sector->tile_scripts, tile_id_from_loc(loc), &script);
            sector_unlock(id);
            if (!good) return false;
            script_global_var_set(1860 + count * 2, (int)location_get_x(loc));
            script_global_var_set(1861 + count * 2, (int)location_get_y(loc));
            tile_script_exec(loc, pc(1));
            tile_script_exec(loc, pc(2));
            count++;
        }
        if (count != 2) return false;
    }
    Script one, two;
    return script_global_var_get(1878) == 2 && script_global_var_get(1879) == 1
        && legacy_tile(location_make(script_global_var_get(1860), script_global_var_get(1861)), &one)
        && one.num == 999932 && (one.hdr.flags & (1u << 15)) && one.hdr.counters == 2
        && legacy_tile(location_make(script_global_var_get(1862), script_global_var_get(1863)), &two) && !two.num;
}
static bool legacy_ready(unsigned phase)
{
    if (evil_mode) {
        if (phase >= EVIL_ENDING_FIRST) {
            const LegacyStage* stage = legacy_stage(phase);
            if (stage->kind == LG_FIGHT) {
                int64_t being = evil_being(stage->goal);
                return being && critter_is_dead(being) && (phase != 253 || script_global_flag_get(2043));
            }
            if (stage->kind == LG_EQUIP || stage->kind == LG_UNEQUIP) {
                int64_t ring = legacy_named(2857);
                bool worn = ring && (item_inventory_location_get(ring) == ITEM_INV_LOC_RING1 || item_inventory_location_get(ring) == ITEM_INV_LOC_RING2);
                return ring && (stage->kind == LG_EQUIP ? worn : !worn);
            }
            if (phase >= 278) return phase != 280 && script_global_flag_get(2048) && script_global_flag_get(2043)
                && script_global_flag_get(2632) && quest_state_get(pc(0), 1031) == QUEST_STATE_ACCEPTED
                && script_global_var_get(1878) == 1;
            return true;
        }
        if (phase == 215) return script_global_flag_get(2003) && quest_state_get(pc(0), 1025) == QUEST_STATE_ACCEPTED;
        if (phase == 217 || phase == 218) return script_global_flag_get(2010)
            && script_global_var_get(1062) > 0 && script_global_var_get(1008) >= script_global_var_get(1062);
        if (phase == 220 || phase == 221) return script_global_flag_get(2029) && quest_state_get(pc(0), 1026) == QUEST_STATE_ACCEPTED;
        if (phase == 224) return script_story_state_get() == 18 && script_global_flag_get(2088)
            && area_is_known(pc(0), 13) && quest_state_get(pc(0), 1027) >= QUEST_STATE_MENTIONED;
        if (phase == 225) {
            int64_t npc = scripted(1092, SAP_DIALOG);
            return npc && (obj_field_int32_get(npc, OBJ_F_FLAGS) & OF_OFF) && !coop_engine_dialogue_pending();
        }
        if (phase == 233 || phase == 235) return script_global_flag_get(2027) && legacy_named(5616);
        if (phase == 237 || phase == 238) {
            for (unsigned i = 0; i < 4; i++)
                if (item_find_by_name(pc(i), 5616)) return false;
            /* Kan Hua's original offered banishment accepts1029; completion
             * belongs to later Void content. His script does not set2035. */
            return script_story_state_get() == 20
                && quest_state_get(pc(0), 1027) == QUEST_STATE_COMPLETED
                && quest_state_get(pc(0), 1029) == QUEST_STATE_ACCEPTED && map_current_map() == 8;
        }
        if (phase == 241) {
            return script_global_flag_get(2632) && script_global_flag_get(2057)
                && quest_state_get(pc(0), 1031) == QUEST_STATE_ACCEPTED && legacy_named(2857);
        }
        if (phase == 242 || phase == 243) {
            for (unsigned i = 0; i < 4; i++)
                if (item_find_by_name(pc(i), 5616)) return false;
            /* The evil branch retains Kan Hua's story20. Nasrudin's good
             * route advances22; that unrelated state is not an evil gate. */
            return script_story_state_get() >= 20 && map_current_map() == 73
                && script_global_flag_get(2010) && script_global_flag_get(2029)
                && quest_state_get(pc(0), 1027) == QUEST_STATE_COMPLETED && script_global_var_get(1878) == 1
                && legacy_ready(241);
        }
        return true;
    }
    if (rescue_mode) {
        if (phase == 194) return !script_global_flag_get(2591);
        if (phase == 195)
            return script_global_flag_get(1031) && !script_global_flag_get(1029)
                && location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(32205, 62499)) < 100;
        if (phase == 196 || phase == 197) return script_global_flag_get(1029) && !script_global_flag_get(1031)
            && script_global_var_get(1840) == 1
            && location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(20691, 56027)) < 100;
        if (phase == 198 || phase == 199) return script_global_flag_get(2470) && script_global_flag_get(2591);
        if (phase == 201 || phase == 202) return !script_global_flag_get(2591)
            && location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(32227, 62497)) < 100;
        if (phase == 203) {
            const LegacyStage* stage = legacy_stage(phase);
            return location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(stage->x, stage->y)) < 100;
        }
        if (phase == 204) {
            unsigned start = (unsigned)script_global_var_get(1833), count = coop_journal_count(coop_engine_journal());
            for (unsigned i = start; i < count; i++) {
                CoopDialogueView page;
                char speaker[COOP_JOURNAL_SPEAKER];
                if (coop_journal_page(coop_engine_journal(), i, &page, speaker)
                    && page.mode == 0 && page.chooser == legacy_actor(phase) && strstr(page.reply, "steam rails")) {
                    int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(page.npc));
                    if (npc && script_number(npc, SAP_DIALOG) == 1749) return true;
                }
            }
            return false;
        }
        if (phase == 205 || phase == 206) return script_global_flag_get(2470) && !script_global_flag_get(2591)
            && script_global_var_get(1840) == 1
            && location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(62183, 65692)) < 100;
        return true;
    }
    if (combat_finale_mode) {
        int64_t target = combat_finale_target();
        if (phase == 209) return target && combat_critter_is_combat_mode_active(target);
        if (phase == 210) return script_global_var_get(1842) == 1;
        if (phase == 211 || phase == 212) return target && sub_49B290(target) == 27369;
        if (phase == 213) return false;
        return true;
    }
    switch (phase) {
    case 125:
        return script_global_flag_get(1098);
    case 127:
        return script_story_state_get() >= 16 && script_global_flag_get(1088) && quest_state_get(pc(0), 1024) == QUEST_STATE_ACCEPTED;
    case 129:
        return rumor_known_get(pc(0), 2056);
    case 132:
        return quest_state_get(pc(0), 1049) == QUEST_STATE_ACCEPTED;
    case 134:
        return script_global_flag_get(2016) && legacy_named(5615);
    case 135:
        return script_global_flag_get(2096);
    case 137:
        return script_global_flag_get(2033) && quest_state_get(pc(0), 1049) == QUEST_STATE_COMPLETED;
    case 138:
        return script_story_state_get() >= 17 && rumor_known_get(pc(0), 2073);
    case 141:
        return script_story_state_get() >= 18 && area_is_known(pc(0), 20);
    case 142:
        return script_global_flag_get(2089) && quest_state_get(pc(0), 1051) == QUEST_STATE_ACCEPTED;
    case 146:
        return quest_state_get(pc(0), 1139) != QUEST_STATE_UNKNOWN;
    case 147:
        return script_global_flag_get(2375) && coop_engine_documents()->count >= 4;
    case 150: {
        int64_t item = legacy_named(2848);
        return item && item_inventory_location_get(item) == ITEM_INV_LOC_MEDALLION;
    }
    case 153:
    case 154:
    case 155:
    case 156:
        return script_global_var_get(1071) >= legacy_stage(phase)->goal;
    case 157:
        return script_global_flag_get(2552);
    case 158:
        return area_is_known(pc(0), 22) && quest_state_get(pc(0), 1139) == QUEST_STATE_COMPLETED;
    case 165:
        return script_story_state_get() >= 19 && script_global_flag_get(2027) && legacy_named(5616);
    case 167:
        return script_story_state_get() == 20 && quest_state_get(pc(0), 1051) == QUEST_STATE_COMPLETED;
    case 168:
    case 169:
        return legacy_tile_oracle(false) && script_story_state_get() == 20 && legacy_named(5616);
    case 171:
        return script_story_state_get() == 22 && script_global_flag_get(2042) && quest_state_get(pc(0), 1057) == QUEST_STATE_ACCEPTED;
    case 173:
        return script_global_flag_get(2718) && !legacy_enemy();
    case 175:
        return script_story_state_get() == 22 && legacy_named(5616);
    case 182: {
        int64_t item = legacy_named(2806), parent = 0;
        return item && item_parent(item, &parent) && parent == pc(legacy_actor(182));
    }
    case 183:
    case 187:
    case 189:
    case 191:
    case 192: {
        int64_t item = legacy_named(2806), parent = 0;
        return item && item_parent(item, &parent) && coop_campaign_slot_for_object(parent) >= 0
            && item_inventory_location_get(item) == ITEM_INV_LOC_HELMET
            && (phase == 183 || script_global_flag_get(2095))
            && (phase < 191 || traversal_tile_persistence());
    }
    case 184:
        return script_global_flag_get(2095);
    case 188: {
        int64_t item = legacy_named(2806), parent = 0;
        if (item) item_parent(item, &parent);
        return item && !parent;
    }
    case 190:
        return script_global_var_get(1850) == 12 && !coop_runtime_entry_pending();
    case 177:
        return false; /* Only the original terminal presentation completes this stage. */
    default:
        return true;
    }
}
static bool legacy_ambient_scope(void)
{
    CoopDialogue* dialogue = coop_gameplay_dialogue(coop_runtime_gameplay());
    int64_t distant = OBJ_HANDLE_NULL;
    /* A private disposable utility NPC at the original Isle location, while
     * the party is at Ashbury. Exercise both original FLOAT_LINE and the
     * generic callback used by native newspaper headlines. */
    if (!coop_dialogue_settled(dialogue)
        || !object_create(sub_4685A0(17067), location_make(20674, 56009), &distant)) return false;
    Script script = { .num = 2650 };
    ScriptInvocation invocation = { .script = &script, .line = 5, .attachment_point = SAP_DIALOG, .triggerer_obj = distant, .attachee_obj = distant };
    script_execute(&invocation);
    bool good = coop_dialogue_settled(dialogue)
        && coop_engine_dialogue_float(distant, distant, "ARCMP private distant ambient rejection", -1)
        && coop_dialogue_settled(dialogue) && !coop_engine_dialogue_failed();
    object_destroy(distant);
    fprintf(stderr, "ARCMP native ambient vicinity distant-script-and-callback=%d\n", good);
    return good;
}
static bool legacy_path_prefix(void)
{
    int64_t actor = pc(0), from = obj_field_int64_get(actor, OBJ_F_LOCATION);
    for (int rotation = 0; rotation < 8; rotation++) {
        int64_t to = from;
        for (int step = 0; step < 12; step++)
            if (!location_in_dir(to, rotation, &to)) {
                to = 0;
                break;
            }
        if (!to || tile_is_blocking(to, false)) continue;
        int8_t guarded[24];
        memset(guarded, 0xa5, sizeof(guarded));
        PathCreateInfo info = { .obj = actor, .from = from, .to = to, .max_rotations = 8, .rotations = guarded + 8, .flags = PATH_FLAG_0x0010 };
        int count = path_make(&info);
        if (!count) continue;
        bool good = count == 8 && info.to == to && location_dist(from, to) > 8;
        for (unsigned i = 0; i < 8; i++)
            good &= (uint8_t)guarded[i] == 0xa5 && (uint8_t)guarded[16 + i] == 0xa5;
        fprintf(stderr, "ARCMP native player path beyond-buffer prefix=%d distance=%lld canaries=%d\n", count,
            (long long)location_dist(from, to), good);
        return good;
    }
    return false;
}
static bool legacy_prepare(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    if (evil_mode) {
        if (phase >= EVIL_ENDING_FIRST) return evil_ending_prepare(phase);
        if (phase == 214) {
            if (script_story_state_get() < 14 || script_story_state_get() >= 15) return false;
            script_global_var_set(1878, 1);
            /* Character profile for the original evil branch: her offered
             * direct recruitment choice requires alignment <= -100. This is
             * a fixture build choice, not a quest/reputation/visibility grant. */
            stat_base_set(pc(2), STAT_ALIGNMENT, -100);
            for (unsigned i = 0; i < 4; i++) {
                stat_base_set(pc(i), STAT_STRENGTH, 20);
                stat_base_set(pc(i), STAT_DEXTERITY, 20);
                stat_base_set(pc(i), STAT_CHARISMA, 20);
                stat_base_set(pc(i), STAT_INTELLIGENCE, 16);
                obj_field_int32_set(pc(i), OBJ_F_HP_PTS, 3000);
                obj_field_int32_set(pc(i), OBJ_F_HP_DAMAGE, 0);
                obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_PTS, 3000);
                obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_DAMAGE, 0);
                basic_skill_points_set(pc(i), BASIC_SKILL_MELEE, 5);
                basic_skill_points_set(pc(i), BASIC_SKILL_DODGE, 5);
            }
        }
        if (phase == 217) return evil_town_prepare();
        if (phase == 237) {
            script_global_var_set(1814, (int)legacy_actor(phase));
            return item_find_by_name(pc(legacy_actor(phase)), 5616) != 0;
        }
        if (phase == 239) {
            int64_t loc = obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION);
            evil_void_start = map_current_map() == 73 ? 4 : map_current_map() == 72 ? 2
                : location_get_y(loc) < 1000 ? 3 : location_get_x(loc) < 1000 ? 1 : 0;
            char message[64]; snprintf(message, sizeof(message), "void-start:%u", evil_void_start);
            return coop_session_chat(held, message) && evil_void_prepare(evil_void_start);
        }
        if (stage->kind == LG_LOAD) script_global_var_set(1878, 9000);
        if (stage->kind == LG_TILE) {
            script_global_var_set(1864, stage->x);
            script_global_var_set(1865, stage->y);
            map_precache_sectors(location_make(stage->x, stage->y));
            if (phase == 240) {
                /* The original castle has three disconnected floors. Its
                 * map.jmp stairs, rather than direct coordinate placement,
                 * connect the entrance to the final chamber ground trigger. */
                JumpPoint stairs;
                if (!jumppoint_get(location_make(653, 93), &stairs) || stairs.dst_map != 73 || stairs.dst_loc != location_make(564, 96)
                    || !jumppoint_get(location_make(553, 106), &stairs) || stairs.dst_map != 73 || stairs.dst_loc != location_make(439, 72)) return false;
            }
        }
        /* Restore only the original relevant actors quieted by earlier input
         * fixtures. Native visibility heartbeat gates remain responsible for
         * Mingorad and both appearances of Kan Hua. Local approaches, dungeon
         * floors and the castle receive no adjacent placement or new quieting. */
        if (stage->kind == LG_TALK && stage->script == 1087) {
            int64_t npc = find_attachment(1087, SAP_DIALOG);
            if (!npc) return false;
            fprintf(stderr, "ARCMP original Mingorad gate name=%d flags=%x npc-flags=%x first=%d heartbeat=%d dead=%d hp=%d damage=%d\n",
                obj_field_int32_get(npc, OBJ_F_NAME), obj_field_int32_get(npc, OBJ_F_FLAGS), obj_field_int32_get(npc, OBJ_F_NPC_FLAGS),
                script_number(npc, SAP_FIRST_HEARTBEAT), script_number(npc, SAP_HEARTBEAT), critter_is_dead(npc),
                obj_field_int32_get(npc, OBJ_F_HP_PTS), obj_field_int32_get(npc, OBJ_F_HP_DAMAGE));
            if (script_number(npc, SAP_HEARTBEAT) != 1242 && script_number(npc, SAP_FIRST_HEARTBEAT) != 1242)
                object_flags_unset(npc, OF_OFF);
        }
        if (stage->kind == LG_USE && !find_attachment(stage->script, SAP_USE)) return false;
        if (stage->kind == LG_TALK && stage->script != 1087 && !find_attachment(stage->script, SAP_DIALOG)) return false;
        if (stage->kind == LG_COLLECT && !legacy_item(phase) && !find_attachment(stage->script, SAP_GET)) return false;
        if (stage->kind == LG_TALK || stage->kind == LG_USE || stage->kind == LG_COLLECT) {
            int sap = stage->kind == LG_TALK ? SAP_DIALOG : stage->kind == LG_USE ? SAP_USE
                                                                                  : SAP_GET;
            /* Story pickup work can live on INSERT_ITEM rather than GET (the
             * original Device does this). Follow the actual item identity/name
             * instead of requiring an unrelated GET attachment. */
            int64_t target = stage->kind == LG_COLLECT ? legacy_item(phase) : 0, parent = 0;
            if (!target) target = find_attachment(stage->script, sap);
            if (stage->kind == LG_COLLECT && target)
                fprintf(stderr, "ARCMP original collection phase=%u name=%d pickup-script=%d insert-script=%d device-flag=%d\n", phase,
                    obj_field_int32_get(target, OBJ_F_NAME), script_number(target, SAP_GET),
                    script_number(target, SAP_INSERT_ITEM), script_global_flag_get(2027));
            if (phase == 215 || phase == 220) target = find_attachment(2424, SAP_USE);
            if (phase == 224) target = script_global_flag_get(2087) ? find_attachment(2489, SAP_USE) : find_attachment(1089, SAP_DIALOG);
            if (phase == 226) target = find_attachment(2490, SAP_USE);
            if (phase == first_phase && phase >= 229 && phase <= 232 && getenv("ARCMP_PROBE_RESUPPLY")) {
                /* Explicit controlled equipment continuation, separate from
                 * the exhausted-party route. Keep original doors/damage rules;
                 * owners equip these original axes through normal input. */
                for (unsigned slot = 0; slot < 4; slot++) {
                    int64_t axe;
                    if (!object_create(sub_4685A0(BP_AXE), obj_field_int64_get(pc(slot), OBJ_F_LOCATION), &axe)
                        || !item_transfer(axe, pc(slot))) return false;
                }
                fprintf(stderr, "ARCMP controlled equipment resupply: four original axes; acquisition excluded from acceptance\n");
            }
            if (phase == 232) {
                int64_t here = obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION);
                /* Retain progress through the original disjoint map sections
                 * when a public Save interrupts this multi-stair stage. */
                int x = (int)location_get_x(here), y = (int)location_get_y(here);
                evil_floor_start = x < 1640 ? 3u : x < 1760 ? 0u : y < 1980 ? 2u : 1u;
                char message[64]; snprintf(message, sizeof(message), "dungeon-start:%u", evil_floor_start);
                if (!coop_session_chat(held, message)) return false;
                target = find_attachment(evil_floor_gate(phase, 0), SAP_USE);
            }
            if (!target) return false;
            if (stage->kind == LG_COLLECT && item_parent(target, &parent) && parent) target = parent;
            if (phase == 223 || phase == 224 || (phase >= 229 && phase <= 232)) evil_inspect_approach(phase, target);
            if (!evil_prepare_hint(phase, target)) return false;
        }
        return true;
    }
    if (rescue_mode || combat_finale_mode) {
        if (phase == 193 || phase == 207) {
            for (unsigned i = 0; i < 4; i++) {
                stat_base_set(pc(i), STAT_STRENGTH, 20);
                stat_base_set(pc(i), STAT_DEXTERITY, 20);
                stat_base_set(pc(i), STAT_INTELLIGENCE, 16);
                stat_base_set(pc(i), STAT_CHARISMA, 20);
                obj_field_int32_set(pc(i), OBJ_F_HP_PTS, 3000);
                obj_field_int32_set(pc(i), OBJ_F_HP_DAMAGE, 0);
                obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_PTS, 3000);
                obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_DAMAGE, 0);
                basic_skill_points_set(pc(i), BASIC_SKILL_MELEE, 5);
                basic_skill_points_set(pc(i), BASIC_SKILL_DODGE, 5);
                if (!dark_equipment(pc(i), BP_SWORD, ITEM_INV_LOC_WEAPON)) return false;
            }
            script_global_var_set(1840, 1);
            script_global_var_set(1842, 0);
            script_global_var_set(1843, 0);
            if (phase == 207 && !legacy_path_prefix()) return false;
        }
        if (stage->kind == LG_LOAD) {
            script_global_var_set(1840, 9000);
            return true;
        }
        if (stage->kind == LG_TRAVEL || stage->kind == LG_SAVE || stage->kind == LG_USE_ITEM) return true;
        if (stage->kind == LG_TILE) {
            int64_t loc = location_make(stage->x, stage->y);
            script_global_var_set(1864, stage->x);
            script_global_var_set(1865, stage->y);
            map_precache_sectors(loc);
            if (!legacy_tile(loc, &(Script) { 0 })) return false;
            /* Match the retained peaceful-ending fixture's entry setup.
             * Organic traversal through the castle remains a separate gap. */
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int64_t from = location_make(stage->x + dx, stage->y + dy);
                    Script other;
                    if (tile_is_blocking(from, false) || (legacy_tile(from, &other) && other.num)) continue;
                    sub_43E770(pc(legacy_actor(phase)), from, 0, 0);
                    return true;
                }
            return false;
        }
        if (combat_finale_mode) return true;
        if (stage->kind == LG_TALK) {
            DateTime time = sub_45A7C0();
            script_global_var_set(1830, time.days);
            script_global_var_set(1831, time.milliseconds);
            script_global_var_set(1832, item_gold_get(pc(legacy_actor(phase))));
            if (phase == 204) {
                script_global_var_set(1833, (int)coop_journal_count(coop_engine_journal()));
                /* A focused refusal tail uses the retained post-wait archive
                 * with the actual captain already spawned. */
                if (first_phase == 204 && !branch_target(phase)) return false;
            }
            if (phase == 203) {
                unsigned actor = legacy_actor(phase), donor = (actor + 1) % 4;
                script_global_var_set(1825, item_gold_get(pc(actor)) + item_gold_get(pc(donor)));
                script_global_var_set(1826, item_gold_get(pc(donor)));
                script_global_var_set(1828, 0);
            }
        }
        int64_t target = branch_target(phase);
        if (!target) return stage->script == 1749; /* Wait for the original generator's actual captain. */
        object_flags_unset(target, OF_OFF);
        fprintf(stderr, "ARCMP alternate target phase=%u name=%d location=%lld,%lld distance=%lld flags=%x heartbeat=%d\n", phase,
            obj_field_int32_get(target, OBJ_F_NAME), (long long)location_get_x(obj_field_int64_get(target, OBJ_F_LOCATION)),
            (long long)location_get_y(obj_field_int64_get(target, OBJ_F_LOCATION)), (long long)object_dist(pc(legacy_actor(phase)), target),
            obj_field_int32_get(target, OBJ_F_FLAGS), script_number(target, SAP_HEARTBEAT));
        return true;
    }
    if (stage->kind == LG_TRAVEL) return true;
    if (traversal_mode) {
        if (phase == 182 || phase == 190) {
            int64_t npc = scripted(1066, SAP_DIALOG);
            traversal_events = false;
            if (!npc || !critter_pc_leader_get(npc)) return false;
            if (phase == 182) {
                int64_t glasses = legacy_named(2806), parent = 0;
                if (!glasses || !item_parent(glasses, &parent) || parent != npc || script_global_flag_get(2095)) return false;
            }
        }
        if (stage->kind == LG_RUNE) {
            int64_t loc = legacy_tile_near(stage->script, obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION));
            if (!loc) return false;
            script_global_var_set(1864, (int)location_get_x(loc));
            script_global_var_set(1865, (int)location_get_y(loc));
        }
        if (stage->kind == LG_LOAD) {
            script_global_var_set(1878, 9000);
            if (phase == 192) script_global_var_set(1850, 9000);
        }
        return true; /* No adjacent placement, hidden-object changes or retail flag injection. */
    }
    if (finale_mode && phase == final_phase && coop_dialogue_current(coop_gameplay_dialogue(coop_runtime_gameplay()))->active) return true;
    unsigned actor = legacy_actor(phase);
    if (phase == 176) {
        stat_base_set(pc(actor), STAT_CHARISMA, 20);
        basic_skill_points_set(pc(actor), BASIC_SKILL_PERSUATION, 5);
        fprintf(stderr, "ARCMP finale Device owner slot=%u member=%d charisma=%d persuasion=%d points=%d device=%lld\n", actor,
            coop_campaign_slot_for_object(pc(actor)), stat_level_get(pc(actor), STAT_CHARISMA), basic_skill_level(pc(actor), BASIC_SKILL_PERSUATION),
            basic_skill_points_get(pc(actor), BASIC_SKILL_PERSUATION), (long long)item_find_by_name(pc(actor), 5616));
        if (stat_level_get(pc(actor), STAT_CHARISMA) < 20 || basic_skill_level(pc(actor), BASIC_SKILL_PERSUATION) < 20 || !item_find_by_name(pc(actor), 5616)) return false;
    }
    if (stage->kind == LG_FIGHT) {
        int64_t target = find_attachment(stage->script, SAP_DIALOG), object;
        int iterator;
        if (!target) return false;
        if (obj_inst_first(&object, &iterator)) do {
                if (script_number(object, SAP_DYING) == 2972) object_flags_unset(object, OF_OFF);
            } while (obj_inst_next(&object, &iterator));
        for (unsigned i = 0; i < 4; i++) {
            obj_field_int32_set(pc(i), OBJ_F_HP_DAMAGE, 0);
            obj_field_int32_set(pc(i), OBJ_F_CRITTER_FATIGUE_DAMAGE, 0);
            basic_skill_points_set(pc(i), BASIC_SKILL_MELEE, 5);
            basic_skill_points_set(pc(i), BASIC_SKILL_DODGE, 5);
        }
        return place_near(target);
    }
    if (phase == 177) {
        int64_t loc = location_make(96, 667);
        map_precache_sectors(loc);
        Script tile;
        bool original = legacy_tile(loc, &tile) && tile.num == 3071;
        fprintf(stderr, "ARCMP finale arrival tile active=%d number=%d\n", original, legacy_tile(loc, &tile) ? tile.num : -1);
        if (original) {
            script_global_var_set(1864, 96);
            script_global_var_set(1865, 667);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int64_t from = location_make(96 + dx, 667 + dy);
                    Script other;
                    if (tile_is_blocking(from, false) || (legacy_tile(from, &other) && other.num)) continue;
                    sub_43E770(pc(actor), from, 0, 0);
                    return true;
                }
            return false;
        }
    }
    if (phase == 158) {
        map_precache_sectors(location_make(28388, 31169));
        /* Older focused checkpoints quieted the native pool's invisible NPC
         * controller. Restore that controller; its original heartbeat summons
         * Pelojian after the actual rune gate. The corpse's original visibility
         * is restored separately below; the script supplies Conjure Spirit. */
        int64_t pool = scripted(3022, SAP_HEARTBEAT);
        if (!pool) pool = scripted(3022, SAP_FIRST_HEARTBEAT);
        if (!pool) return false;
        object_flags_unset(pool, OF_OFF);
        sub_4AD6E0(pool);
        Script script;
        obj_arrayfield_script_get(pool, OBJ_F_SCRIPTS_IDX, SAP_HEARTBEAT, &script);
        fprintf(stderr, "ARCMP legacy pool name=%d dead=%d flags=%x script=%d locals=%x/%x gate=%d\n", obj_field_int32_get(pool, OBJ_F_NAME), critter_is_dead(pool), obj_field_int32_get(pool, OBJ_F_FLAGS), script.num, script.hdr.flags, script.hdr.counters, script_global_flag_get(2552));
    }
    if (stage->kind == LG_RUNE || stage->kind == LG_TILE) {
        int64_t loc = stage->kind == LG_TILE ? location_make(stage->x, stage->y) : legacy_rune(stage->script);
        if (!loc) return false;
        if (stage->kind == LG_TILE) {
            map_precache_sectors(loc);
            Script script;
            if (!legacy_tile(loc, &script) || script.num != stage->script) return false;
        }
        script_global_var_set(1864, (int)location_get_x(loc));
        script_global_var_set(1865, (int)location_get_y(loc));
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                int64_t from = location_make(location_get_x(loc) + dx, location_get_y(loc) + dy);
                if (tile_is_blocking(from, false)) continue;
                Script other;
                if (legacy_tile(from, &other) && other.num) continue;
                sub_43E770(pc(actor), from, 0, 0);
                return true;
            }
        return false;
    }
    if (stage->kind == LG_SAVE) {
        if (phase != 175) return legacy_tile_oracle(true);
        int64_t item = legacy_named(5616), parent = 0;
        if (!item || !item_parent(item, &parent) || coop_campaign_slot_for_object(parent) < 0) return false;
        stat_base_set(parent, STAT_CHARISMA, 20);
        basic_skill_points_set(parent, BASIC_SKILL_PERSUATION, 5);
        return true;
    }
    if (stage->kind == LG_LOAD) {
        script_global_var_set(1878, 9000);
        return true;
    }
    if (stage->kind == LG_COLLECT && stage->script < 0)
        map_precache_sectors(location_make(stage->x, stage->y));
    int64_t target = stage->kind == LG_TALK                                              ? find_attachment(stage->script, SAP_DIALOG)
        : stage->kind == LG_EQUIP || stage->kind == LG_READ || stage->kind == LG_COLLECT ? legacy_item(phase)
                                                                                         : find_attachment(stage->script, stage->kind == LG_EXAMINE ? SAP_EXAMINE : SAP_USE);
    if (!target && stage->kind == LG_COLLECT && stage->script > 0) target = find_attachment(stage->script, SAP_GET);
    if (!target) return false;
    if (phase == 177) fprintf(stderr, "ARCMP finale actors pc=%u active=%d unconscious=%d at=%lld,%lld npc-name=%d npc-active=%d npc-critter=%x npc-spell=%x npc-speak=%d\n", actor, critter_is_active(pc(actor)), critter_is_unconscious(pc(actor)), (long long)location_get_x(obj_field_int64_get(pc(actor), OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(pc(actor), OBJ_F_LOCATION)), obj_field_int32_get(target, OBJ_F_NAME), critter_is_active(target), obj_field_int32_get(target, OBJ_F_CRITTER_FLAGS), obj_field_int32_get(target, OBJ_F_SPELL_FLAGS), ai_can_speak(target, pc(actor), true));
    fprintf(stderr, "ARCMP legacy prepared phase=%u script=%d target=%lld location=%lld,%lld flags=%x\n", phase, stage->script, (long long)target, (long long)location_get_x(obj_field_int64_get(target, OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(target, OBJ_F_LOCATION)), obj_field_int32_get(target, OBJ_F_FLAGS));
    if (stage->kind == LG_EQUIP || stage->kind == LG_READ) return true;
    if (stage->kind == LG_EXAMINE || stage->kind == LG_LOOT) {
        /* Examination is an authenticated look, not a Use approach to the
         * object's centre. The tomb lid shares its tile with a blocking coffin. */
        int64_t anchor = obj_field_int64_get(target, OBJ_F_LOCATION);
        map_precache_sectors(anchor);
        for (int radius = 1; radius <= 3; radius++)
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++) {
                    if (abs(dx) != radius && abs(dy) != radius) continue;
                    int64_t loc = location_make(location_get_x(anchor) + dx, location_get_y(anchor) + dy);
                    if (tile_is_blocking(loc, false)) continue;
                    sub_43E770(pc(actor), loc, 0, 0);
                    return true;
                }
        return false;
    }
    if (stage->kind == LG_TALK) {
        /* The original Pelojian corpse has flags 0x4434, without OF_OFF.
         * Earlier regional fixtures quieted it. Restore its native visibility
         * so the original pool can cast Conjure Spirit and start his dialogue. */
        bool native_gate = stage->script == 1171;
        if (!native_gate) object_flags_unset(target, OF_OFF);
        if (phase == 158) {
            /* The ghost occupies an impassable pool tile. Talking approaches
             * its bank; requiring an exact path to the centre never succeeds. */
            int64_t anchor = obj_field_int64_get(target, OBJ_F_LOCATION), positions[4];
            for (unsigned i = 0; i < 4; i++) {
                bool placed = false;
                for (int radius = 1; radius <= 8 && !placed; radius++)
                    for (int dy = -radius; dy <= radius && !placed; dy++)
                        for (int dx = -radius; dx <= radius && !placed; dx++) {
                            if (abs(dx) != radius && abs(dy) != radius) continue;
                            int64_t loc = location_make(location_get_x(anchor) + dx, location_get_y(anchor) + dy);
                            if (tile_is_blocking(loc, false)) continue;
                            bool separate = true;
                            for (unsigned j = 0; j < i; j++)
                                if (location_dist(loc, positions[j]) < 2) separate = false;
                            if (!separate) continue;
                            sub_43E770(pc(i), loc, 0, 0);
                            positions[i] = loc;
                            placed = true;
                        }
                if (!placed) return false;
            }
            return true;
        }
        return place_near(target);
    }
    if (stage->kind == LG_COLLECT) {
        int64_t parent = 0;
        item_parent(target, &parent);
        if (parent && coop_campaign_slot_for_object(parent) >= 0) return true;
        if (parent) target = parent;
    }
    return place_members_near_radius(target, 1u << actor, 1);
}
static bool legacy_transfer(unsigned phase)
{
    const LegacyStage* stage = legacy_stage(phase);
    int script = evil_mode ? 999900 : 999920;
    for (unsigned i = evil_mode ? EVIL_FIRST : LEGACY_FIRST; i < phase; i++)
        if (legacy_stage(i)->kind == LG_TRAVEL) script++;
    return transfer_script(script, stage->map, location_make(stage->x, stage->y), legacy_actor(phase));
}
static bool legacy_read_close(unsigned phase, bool framebuffer)
{
    bool good = true;
    if (framebuffer) {
        char file[80];
        snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
        gamelib_redraw();
        iso_redraw();
        tig_window_display();
        good = tig_video_screenshot_save(file) == TIG_OK;
    }
    written_ui_coop_stop();
    return good;
}
typedef struct FinaleObserver {
    bool framebuffer, good, terminal, slide_captured, goal_seen, original_device, combat_movie, original_alliance;
    bool media_drop;
    unsigned sequence, slides, terminal_id, epoch, story;
    unsigned media_epoch, fault_epoch;
    uint64_t at, media_time, media_tick, media_started;
    unsigned media_sequence;
} FinaleObserver;
static void finale_observe(void* context)
{
    FinaleObserver* observer = context;
    const CoopPresentView* view = coop_present_current(coop_runtime_presentation());
    if (!view || !view->sequence) return;
    uint64_t now = SDL_GetTicks();
    unsigned epoch = coop_runtime_epoch();
    if (epoch) observer->epoch = epoch;
    if (epoch != observer->media_epoch) {
        observer->media_epoch = epoch;
        observer->sequence = observer->media_sequence = 0;
        observer->media_tick = 0;
    }
    if (observer->media_sequence == view->sequence && observer->media_tick)
        observer->media_time += now - observer->media_tick;
    else
        observer->media_started = now;
    observer->media_sequence = view->sequence;
    observer->media_tick = now;
    if (current_phase != final_phase) {
        if (view->kind == COOP_PRESENT_MOVIE && now - observer->media_started > 1000) coop_runtime_present_input();
        return;
    }
    if (getenv("ARCMP_PROBE_MEDIA_DROP") && view->kind == COOP_PRESENT_MOVIE && view->phase == COOP_PRESENT_PLAYING) {
        if (!observer->fault_epoch) observer->fault_epoch = epoch;
        if (coop_session_local_slot(held) == 2 && !observer->media_drop && now - observer->media_started > 500) {
            observer->media_drop = coop_session_drop_transport(held);
            fprintf(stderr, "ARCMP native ending movie transport drop=%d epoch=%u\n", observer->media_drop, epoch);
        }
    }
    if (combat_finale_mode && !observer->combat_movie && view->kind == COOP_PRESENT_MOVIE && view->phase == COOP_PRESENT_PLAYING) {
        mes_file_handle_t file;
        if (mes_load("movies\\movies.mes", &file)) {
            MesFileEntry entry = { .num = 3 };
            char original[TIG_MAX_PATH];
            if (mes_search(file, &entry)) {
                snprintf(original, sizeof(original), "movies\\%s", entry.str);
                observer->combat_movie = SDL_strcasecmp(original, view->path) == 0;
                if (observer->combat_movie) fprintf(stderr, "ARCMP original combat movie path=%s\n", view->path);
            }
            mes_unload(file);
        }
    }
    if (observer->sequence != view->sequence) {
        observer->sequence = view->sequence;
        observer->at = SDL_GetTicks();
        if (view->kind == COOP_PRESENT_SLIDE) {
            observer->slides++;
            fprintf(stderr, "ARCMP original ending slide sequence=%u id=%u\n", view->sequence, view->id);
        }
    }
    if (view->kind == COOP_PRESENT_END && view->phase == COOP_PRESENT_COMPLETE) {
        observer->terminal = view->ready == 15 && view->done == 15;
        observer->terminal_id = view->id;
    }
    if (observer->framebuffer && !observer->slide_captured && view->kind == COOP_PRESENT_SLIDE
        && view->phase == COOP_PRESENT_PLAYING && slide_ui_coop_current() == (int)view->id && SDL_GetTicks() - observer->at > 500) {
        observer->good &= tig_video_screenshot_save("arcmp-finale-slide.bmp") == TIG_OK;
        observer->slide_captured = true;
    }
    if ((view->kind == COOP_PRESENT_MOVIE || view->kind == COOP_PRESENT_SLIDE)
        && SDL_GetTicks() - observer->at > 1000) coop_runtime_present_input();
}
static bool announce(unsigned phase)
{
    current_phase = phase;
    bool good = true;
    if (quest_mode) good = legacy_mode ? legacy_prepare(phase) : dark_mode ? dark_prepare(phase)
        : forest_mode                                                      ? forest_prepare(phase)
        : voyage_mode                                                      ? voyage_prepare(phase)
                                                                           : quest_prepare(phase);
    if (legacy_mode && !good) fprintf(stderr, "ARCMP legacy preparation failed phase=%u map=%d\n", phase, map_current_map());
    if ((dark_mode || legacy_mode) && good) script_global_var_set(1889, (int)phase);
    if (phase == 23) good = region_sound_prepare();
    if (phase == 25) good = scripted_music();
    if (phase == 26) good = region_sound_move();
    if (phase == 28) {
        int64_t npc = any_npc();
        good = npc != 0;
        if (npc) gsound_start_combat_music(npc);
    }
    if (phase == 29) gsound_play_script_scheme(script_global_var_get(1951), script_global_var_get(1952));
    if (phase == 30) {
        gsound_stop_combat_music(pc(0));
        gsound_play_map_scheme(0, 0);
    }
    if (phase == 31) gsound_stop_combat_music(pc(0));
    if (phase == 32) gsound_play_map_scheme(0, 0);
    if (phase == 33) good = native_save_failure();
    if (region_exit(phase)) good = place_near(find_region_exit(region_exit(phase)));
    if (phase == 1) {
        good = prepare();
    }
    if (phase == first_phase) {
        for (unsigned i = 0; good && i < 4; i++) {
            char line[64];
            snprintf(line, sizeof(line), "progress-bind:%u:%d", i, coop_campaign_slot_for_owner(coop_session_players(held)[i].identity));
            good = coop_session_chat(held, line);
        }
    }
    if (phase == 4 || phase == 5 || phase == 6 || phase == 7) good = place_near(tagged(true));
    if (phase == 6) {
        int64_t door = tagged(true);
        obj_field_int32_set(door, OBJ_F_PORTAL_FLAGS, OPF_LOCKED | OPF_ALWAYS_LOCKED);
        obj_field_int32_set(door, OBJ_F_PORTAL_KEY_ID, 77);
    }
    if (phase == 7) {
        int64_t key = 0;
        good = good && object_create(sub_4685A0(BP_KEY), obj_field_int64_get(pc(3), OBJ_F_LOCATION), &key);
        if (good) {
            obj_field_int32_set(key, OBJ_F_KEY_KEY_ID, 77);
            good = item_transfer(key, pc(3));
        }
    }
    if (phase == 8) {
        int64_t scene = tagged(false), loc = obj_field_int64_get(pc(0), OBJ_F_LOCATION);
        ScriptAction action = { .type = SAT_TELEPORT, .op_type = { SFO_PLAYER, SVT_NUMBER, SVT_NUMBER, SVT_NUMBER }, .op_value = { 0, 4998, (int)location_get_x(loc) + 12, (int)location_get_y(loc) + 12 } };
        Script script = { .num = SCRIPT_CURRENT };
        good = place_near(scene) && script_file(SCRIPT_CURRENT, &action, 1, 0);
        obj_arrayfield_script_set(scene, OBJ_F_SCRIPTS_IDX, SAP_USE, &script);
    }
    int script = stair_script(phase);
    if (script) {
        int64_t target = scripted(script, SAP_USE);
        if (!target) {
            fprintf(stderr, "ARCMP progress missing retail stairs script=%d map=%d\n", script, map_current_map());
            return false;
        }
        good = place_near(target);
        fprintf(stderr, "ARCMP retail stairs script=%d type=%d flags=%x tile-blocked=%d location=%lld\n",
            script, obj_field_int32_get(target, OBJ_F_TYPE), obj_field_int32_get(target, OBJ_F_FLAGS),
            tile_is_blocking(obj_field_int64_get(target, OBJ_F_LOCATION), false),
            (long long)obj_field_int64_get(target, OBJ_F_LOCATION));
    }
    if (phase == 13) {
        int64_t participant;
        int iterator;
        if (obj_inst_first(&participant, &iterator)) do {
                int number = script_number(participant, SAP_DIALOG);
                if (number == 1054 || number == 1055 || obj_field_int32_get(participant, OBJ_F_NAME) == 6412) object_flags_unset(participant, OF_OFF);
                if (obj_field_int32_get(participant, OBJ_F_TYPE) == OBJ_TYPE_NPC)
                    fprintf(stderr, "ARCMP progress quest NPC name=%d dialog=%d flags=%x\n", obj_field_int32_get(participant, OBJ_F_NAME), number, obj_field_int32_get(participant, OBJ_F_FLAGS));
            } while (obj_inst_next(&participant, &iterator));
        int64_t npc = scripted(1049, SAP_DIALOG);
        if (!npc) {
            fprintf(stderr, "ARCMP progress Winston not resident map=%d\n", map_current_map());
            int64_t object;
            int it;
            if (obj_inst_first(&object, &it)) do {
                    if (obj_field_int32_get(object, OBJ_F_TYPE) == OBJ_TYPE_NPC)
                        fprintf(stderr, "ARCMP progress NPC dialog=%d location=%lld\n", script_number(object, SAP_DIALOG), (long long)obj_field_int64_get(object, OBJ_F_LOCATION));
                } while (obj_inst_next(&object, &it));
            return false;
        }
        object_flags_unset(npc, OF_OFF);
        good = place_near(npc);
    }
    if (phase == 17) {
        good = cache_churn() && visual_changes(false);
    }
    if (phase == 19) good = visual_changes(true);
    if (phase == 20) good = separate_views();
    if (phase == 21) good = remote_sector_entry();
    if (phase == 22) good = reload_visuals();
    char line[64];
    snprintf(line, sizeof(line), "progress-phase:%u", phase);
    return good && coop_session_chat(held, line);
}
/* Read the installed dialogue graph once. Select only already-offered options;
 * conditions/actions remain entirely in the native interpreter. This avoids
 * replaying a whole chapter for race-dependent wording variants. */
typedef struct QuestEdge {
    int source, target;
    uint32_t text;
    unsigned used;
    char generic;
} QuestEdge;
static QuestEdge quest_edges[1024];
static uint32_t quest_replies[2048][2];
static unsigned quest_edge_count;
static int quest_edge_script;
static uint32_t quest_text(const char* text)
{
    uint32_t hash = 2166136261u;
    bool space = false;
    for (const unsigned char* at = (const unsigned char*)text; *at; at++) {
        unsigned char ch = *at;
        if (ch <= 32) {
            space = true;
            continue;
        }
        if (space && hash != 2166136261u) hash = (hash ^ ' ') * 16777619u;
        space = false;
        hash = (hash ^ ch) * 16777619u;
    }
    return hash;
}
static bool quest_matches(const QuestEdge* edge, uint32_t text)
{
    if (edge->text == text) return true;
    int start;
    switch (edge->generic) {
    case 'e':
        start = 400;
        break;
    case 'f':
        start = 800;
        break;
    case 'k':
        start = 1500;
        break;
    case 'n':
        start = 100;
        break;
    case 's':
        start = 200;
        break;
    case 'w':
        start = 1800;
        break;
    case 'c':
        start = 1700 + script_story_state_get();
        break;
    default:
        return false;
    }
    static const char* files[] = { "mes\\gd_pc2m.mes", "mes\\gd_pc2f.mes", "mes\\gd_dumb_pc2m.mes", "mes\\gd_dumb_pc2f.mes" };
    for (unsigned i = 0; i < SDL_arraysize(files); i++) {
        mes_file_handle_t file;
        if (!mes_load(files[i], &file)) continue;
        MesFileEntry entry;
        bool match = false;
        if (mes_find_first(file, &entry)) do {
                if (entry.num >= start && entry.num <= (edge->generic == 'c' ? start : start + 99)
                    && quest_text(entry.str) == text) {
                    match = true;
                    break;
                }
            } while (mes_find_next(file, &entry));
        mes_unload(file);
        if (match) return true;
    }
    return false;
}
static int quest_option(const CoopDialogueView* view, int script, int goal)
{
    if (!goal || goal <= -2048 || goal >= 2048) return -1;
    if (script != quest_edge_script) {
        quest_edge_count = 0;
        quest_edge_script = script;
        memset(quest_replies, 0, sizeof(quest_replies));
    }
    if (!quest_edge_count) {
        char path[TIG_MAX_PATH];
        if (!script_name_build_dlg_name(script, path, sizeof(path))) return -1;
        TigFile* file = tig_file_fopen(path, "rt");
        if (!file) return -1;
        char line[4096];
        int source = -1;
        while (tig_file_fgets(line, sizeof(line), file)) {
            char* fields[8];
            unsigned count = 0;
            char* cursor = line;
            while (count < 8 && (cursor = strchr(cursor, '{'))) {
                fields[count++] = ++cursor;
                char* end = strchr(cursor, '}');
                if (!end) {
                    count = 0;
                    break;
                }
                *end = 0;
                cursor = end + 1;
            }
            if (count < 7) continue;
            /* A PC's optional gender is also nonempty (0/1); IQ distinguishes
             * PC choices from NPC replies in the native file grammar. */
            if (!atoi(fields[3])) {
                source = atoi(fields[0]);
                if (source >= 0 && source < 2048) {
                    quest_replies[source][0] = quest_text(fields[1]);
                    quest_replies[source][1] = quest_text(fields[2]);
                }
            } else if (source >= 0 && quest_edge_count < SDL_arraysize(quest_edges)) {
                int target = atoi(fields[5]);
                /* Retail 1047: -42 calls 1048 -> 1047/44 -> page 1500;
                 * -46 returns the living Bates to page 440. */
                if (script == 1047 && target == -42) target = 1500;
                if (script == 1047 && target == -46) target = 440;
                if (script == 1061 && target == -15) target = 1900;
                /* Original Nasrudin callbacks close the current encounter,
                 * play his history and reopen at the compiled SAT_DIALOG. */
                if (script == 1108 && target == -20) target = 90;
                if (script == 1092 && target == -9) target = 61;
                if (script == 1108 && target == -28) target = 46;
                if (script == 1108 && (target == -35 || target == -40)) target = 192;
                if (script == 1105 && (target == -18 || target == -24 || target == -32 || target == -41)) target = 129;
                if (script == 1105 && target == -100) target = 117;
                if (evil_mode && script == 1105 && target == -53) target = 324;
                if (evil_mode && script == 1105 && target == -68) target = 364;
                char generic = fields[1][0] && fields[1][1] == ':' ? (char)SDL_tolower(fields[1][0]) : 0;
                quest_edges[quest_edge_count++] = (QuestEdge) { source, target, quest_text(fields[1]), 0, generic };
            }
        }
        tig_file_fclose(file);
    }
    int distance[2048];
    for (unsigned i = 0; i < SDL_arraysize(distance); i++)
        distance[i] = 100000;
    if (goal > 0)
        distance[goal] = 0;
    else
        for (unsigned i = 0; i < quest_edge_count; i++)
            if (quest_edges[i].target == goal && quest_edges[i].source >= 0 && quest_edges[i].source < 2048)
                distance[quest_edges[i].source] = 1;
    for (unsigned pass = 0; pass < quest_edge_count; pass++) {
        bool changed = false;
        for (unsigned i = 0; i < quest_edge_count; i++) {
            QuestEdge* edge = &quest_edges[i];
            if (edge->source < 0 || edge->source >= 2048 || edge->target <= 0 || edge->target >= 2048) continue;
            if (distance[edge->source] > distance[edge->target] + 1) {
                distance[edge->source] = distance[edge->target] + 1;
                changed = true;
            }
        }
        if (!changed) break;
    }
    int best = -1, score = 100000;
    unsigned selected_edge = 0;
    uint32_t reply = quest_text(view->reply);
    bool known_reply = false;
    for (unsigned i = 0; i < SDL_arraysize(quest_replies); i++)
        if (reply && (quest_replies[i][0] == reply || quest_replies[i][1] == reply)) known_reply = true;
    for (unsigned option = 0; option < view->option_count; option++)
        for (unsigned i = 0; i < quest_edge_count; i++) {
            QuestEdge* edge = &quest_edges[i];
            if (known_reply && (edge->source < 0 || edge->source >= 2048 || (quest_replies[edge->source][0] != reply && quest_replies[edge->source][1] != reply))) continue;
            if ((edge->target != goal && (edge->target <= 0 || edge->target >= 2048))
                || !quest_matches(edge, quest_text(view->options[option]))) continue;
            int candidate = (edge->target == goal ? 0 : distance[edge->target]) + (int)edge->used * 100;
            if (candidate < score) {
                best = (int)option;
                score = candidate;
                selected_edge = i;
            }
        }
    if (best >= 0) quest_edges[selected_edge].used++;
    return best;
}

int coop_campaign_probe_option(const CoopDialogueView* view, int script, int goal)
{
    return quest_option(view, script, goal);
}
static bool forest_choose(CoopGameplay* game, unsigned slot, uint32_t* revision)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    if (!view->active || view->chooser != slot || view->revision == *revision) return true;
    if (forest_ready(current_phase)) {
        *revision = view->revision;
        return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
    }
    int script = forest_dialog(current_phase), goal = current_phase == 76 ? 198 : current_phase == 78 ? 297
        : current_phase == 81                                                                         ? 223
        : current_phase == 84                                                                         ? 222
        : current_phase == 86                                                                         ? 117
        : current_phase == 88                                                                         ? 292
        : current_phase == 92                                                                         ? 476
                                                                                                      : 85;
    if (current_phase == 81 && strstr(view->reply, "doing in my house")) {
        /* The pelt is offered by the original repeat-greeting page (24),
         * outside the first visitor's introduction/questions subtree. */
        *revision = view->revision;
        return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
    }
    if (current_phase == 90) goal = !script_global_flag_get(1076) ? 55 : !script_global_flag_get(1081) ? 75
                                                                                                       : 85;
    int option = script ? quest_option(view, script, goal) : -1;
    /* A published dialogue page can precede its world/quest update on a
     * replica. At the goal page, wait for that state instead of choosing a
     * branch past it or treating its lack of a return edge as a failure. */
    uint32_t reply = quest_text(view->reply);
    if (option < 0 && (reply == quest_replies[goal][0] || reply == quest_replies[goal][1])) return true;
    if (current_phase == 81) {
        /* A new owner may get the hostile first greeting. The one-question
         * persuasion shortcut offers only a reminder once the pelt job exists.
         * Use the native charisma apology to reach the full hand-in menu. */
        static const char* apology[] = { "Perhaps I could help you", "could we start over" };
        for (unsigned p = 0; p < SDL_arraysize(apology); p++)
            for (unsigned i = 0; i < view->option_count; i++)
                if (strstr(view->options[i], apology[p])) option = (int)i;
    }
    if (option < 0 && view->option_count == 1) option = 0;
    if (option < 0) {
        fprintf(stderr, "ARCMP forest unmatched script=%d goal=%d reply=%s\n", script, goal, view->reply);
        for (unsigned i = 0; i < view->option_count; i++)
            fprintf(stderr, "ARCMP forest option=%u %s\n", i, view->options[i]);
        return false;
    }
    *revision = view->revision;
    retail_choices++;
    fprintf(stderr, "ARCMP forest native choice=%s\n", view->options[option]);
    return coop_engine_dialogue_submit((unsigned)option);
}
static void dark_key(SDL_Scancode key)
{
    TigMessage message = { .type = TIG_MESSAGE_KEYBOARD };
    message.data.keyboard.scancode = key;
    message.data.keyboard.pressed = true;
    tig_message_enqueue(&message);
}
static uint64_t campaign_modal_at;
static bool campaign_modal_tick(TigWindowModalDialogChoice* choice)
{
    if (coop_runtime_modal_tick(choice)) return true;
    if (SDL_GetTicks() - campaign_modal_at > 1500) {
        *choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
        return true;
    }
    return false;
}
static int campaign_menu_check(uint64_t now, bool framebuffer)
{
    static unsigned step;
    static uint64_t began, at, before;
    if (step == 7 || !getenv("ARCMP_PROBE_HOST_MENU")) return 1;
    DateTime game_time, animation;
    timeevent_campaign_clock_get(&game_time, &animation);
    uint64_t clock = (uint64_t)animation.days * COOP_DAY_MS + animation.milliseconds;
    if (step && now - began > 15000) return -1;
    if (!step) {
        mainmenu_ui_start(MM_TYPE_IN_PLAY);
        began = at = now; before = clock; step = 1;
    } else if (step == 1 && now - at > 1500) {
        if (!mainmenu_ui_is_active() || clock < before + 1000) return -1;
        if (framebuffer) {
            tig_window_display();
            if (tig_video_screenshot_save("arcmp-host-menu.bmp") != TIG_OK) return -1;
        }
        dark_key(SDL_SCANCODE_PAUSE); step = 2;
    } else if (step == 2 && coop_runtime_paused()) {
        before = clock; at = now; step = 3;
    } else if (step == 3 && now - at > 1500) {
        if (clock != before) return -1;
        dark_key(SDL_SCANCODE_PAUSE); step = 4;
    } else if (step == 4 && !coop_runtime_paused()) {
        before = clock; at = now; step = 5;
    } else if (step == 5 && now - at > 1500) {
        if (clock < before + 1000) return -1;
        dark_key(SDL_SCANCODE_F12); at = now; step = 6;
    } else if (step == 6 && now - at > 250) {
        campaign_modal_at = now;
        TigWindowModalDialogInfo modal = { .type = TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL, .x = 237, .y = 232,
            .text = "Are you sure you want to quit?", .keys = { 'y', 'n' }, .process = campaign_modal_tick, .redraw = gamelib_redraw };
        TigWindowModalDialogChoice choice;
        if (tig_window_modal_dialog(&modal, &choice) != TIG_OK || choice != TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL) return -1;
        timeevent_campaign_clock_get(&game_time, &animation);
        if ((uint64_t)animation.days * COOP_DAY_MS + animation.milliseconds < clock + 1000) return -1;
        sub_5412D0(); step = 7;
        fprintf(stderr, "ARCMP native host menu PASS clock advances; shared Pause freezes/resumes; native modal keeps simulation alive\n");
        return 1;
    }
    return 0;
}
/* Reopening a collected quest book must not execute its native use script. */
static bool dark_library_read(unsigned phase, unsigned* step, bool framebuffer, bool* good)
{
    const CoopDocuments* documents = coop_engine_documents();
    unsigned selected = 0;
    if (phase != 100) {
        int64_t book = dark_document(117);
        if (!book) {
            *good = false;
            return false;
        }
        unsigned number = (unsigned)obj_field_int32_get(book, OBJ_F_WRITTEN_TEXT_START_LINE);
        while (selected < documents->count && (documents->records[selected].type != 0 || documents->records[selected].number != number))
            selected++;
    }
    if (selected >= documents->count || selected >= 9) {
        *good = false;
        return false;
    }
    CoopDocument document = documents->records[selected];
    if (!*step) {
        dark_key(SDL_SCANCODE_F7);
        *step = 1;
    }
    if (*step == 1 && coop_documents_ui_active()) {
        if (framebuffer) {
            char file[80];
            snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
            gamelib_redraw();
            iso_redraw();
            tig_window_display();
            *good &= tig_video_screenshot_save(file) == TIG_OK;
        }
        dark_key((SDL_Scancode)(SDL_SCANCODE_1 + selected));
        *step = 2;
    }
    unsigned type;
    int number;
    bool variant;
    if (*step == 2 && written_ui_coop_state(&type, &number, &variant)) {
        *good &= type == document.type && (unsigned)number == document.number && variant == (document.variant != 0);
        if (framebuffer) {
            char file[80];
            snprintf(file, sizeof(file), "arcmp-dark-reader-%u.bmp", phase);
            gamelib_redraw();
            iso_redraw();
            tig_window_display();
            *good &= tig_video_screenshot_save(file) == TIG_OK;
        }
        written_ui_coop_stop();
        *step = 3;
    }
    return *step == 3;
}
static bool dark_choose(CoopGameplay* game, unsigned slot, uint32_t* revision)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    if (!view->active || view->chooser != slot || view->revision == *revision) return true;
    if (dark_ready(current_phase)) {
        *revision = view->revision;
        return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
    }
    int option = 0;
    if (current_phase != 98) {
        int goal = current_phase == 102 ? 96 : current_phase == 104 ? 196
            : current_phase == 108                                  ? 137
            : current_phase == 112                                  ? 164
                                                                    : 126;
        option = quest_option(view, dark_dialog(current_phase), goal);
        uint32_t reply = quest_text(view->reply);
        if (option < 0 && (reply == quest_replies[goal][0] || reply == quest_replies[goal][1])) return true;
        if (option < 0 && view->option_count == 1) option = 0;
    }
    if (option < 0) {
        fprintf(stderr, "ARCMP dark unmatched phase=%u reply=%s\n", current_phase, view->reply);
        for (unsigned i = 0; i < view->option_count; i++)
            fprintf(stderr, "ARCMP dark option=%u %s\n", i, view->options[i]);
        return false;
    }
    *revision = view->revision;
    retail_choices++;
    fprintf(stderr, "ARCMP dark native choice=%s\n", view->options[option]);
    return coop_engine_dialogue_submit((unsigned)option);
}
static bool legacy_choose(CoopGameplay* game, unsigned slot, uint32_t* revision)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    if (!view->active || view->chooser != slot || view->revision == *revision) return true;
    const LegacyStage* stage = legacy_stage(current_phase);
    bool kan_hua_departure = evil_mode && current_phase == 224;
    if (legacy_ready(current_phase) && current_phase != 167 && !kan_hua_departure) {
        *revision = view->revision;
        return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
    }
    /* Completing1026 changes Kan Hua's Talk entry. Finish the original
     * encounter's offered -7 departure instead of cancelling/reopening it.
     * First obtain the assignment, map markers and story18 normally. */
    int goal = kan_hua_departure && legacy_ready(current_phase) ? -7 : stage->goal;
    int option = quest_option(view, stage->script, goal);
    if (evil_mode && current_phase == 215) {
        for (unsigned i = 0; i < view->option_count; i++)
            if (!strcmp(view->options[i], "I'm not with the Molochean Hand. I'm here to join your cause.")
                || !strcmp(view->options[i], "Consider it done. [Truth]")) {
                option = (int)i;
                break;
            }
    }
    if (evil_mode && current_phase == 237 && item_find_by_name(pc(slot), 5616)) {
        for (unsigned i = 0; i < view->option_count; i++)
            if (!strcmp(view->options[i], "Here it is.")) {
                option = (int)i;
                break;
            }
    }
    if (evil_mode && current_phase == 241) {
        /* Only native offered choices establish the alliance, create the
         * ring and accept the task. Prefer truth over the parallel lie. */
        for (unsigned i = 0; i < view->option_count; i++)
            if (strstr(view->options[i], "[Tell him the truth.]")) option = (int)i;
    }
    if (!evil_mode && current_phase == final_phase) {
        /* The shortest static graph can choose a branch whose future pages
         * require persuasion that its earlier choice never earned. Follow
         * the offered original philosophical argument, including that gate. */
        static const char* argument[] = {
            "Perhaps I could persuade you otherwise", "inclined to say no", "That's YOUR opinion",
            "I'd have to say you're correct", "Let's speak of the nature", "let's speak of the nature",
            "As you've said, I have only my experience", "Hold, Kerghan. That assumption",
            "Yes, but perhaps that is the nature", "No. Some souls are in pain",
            "No, but the PAIN is of their own making", "The pain is from LOSS of life",
            "I've spoken with souls who weren't in pain", "The distortion is caused by the eyes",
            "I have a solution.  I can free you", "The Vendigroth Device...it will sever",
            "Yes.  I can free you from the pain", "Let me end your journey, Kerghan",
            "Yes, I think you might be right", "Good bye, Kerghan"
        };
        bool found = false;
        for (unsigned n = 0; n < SDL_arraysize(argument) && !found; n++)
            for (unsigned i = 0; i < view->option_count && !found; i++)
                if (strstr(view->options[i], argument[n])) {
                    option = (int)i;
                    found = true;
                }
    }
    uint32_t reply = quest_text(view->reply);
    bool at_goal = stage->goal > 0 && reply && (reply == quest_replies[stage->goal][0] || reply == quest_replies[stage->goal][1]);
    if (option < 0 && at_goal) {
        if (current_phase == 132 || current_phase == 142 || current_phase == 167 || current_phase == 177)
            option = 0;
        else
            return true;
    }
    if (option < 0 && view->option_count == 1) option = 0;
    if (option < 0) {
        fprintf(stderr, "ARCMP legacy unmatched phase=%u script=%d goal=%d reply=%s\n", current_phase, stage->script, stage->goal, view->reply);
        for (unsigned i = 0; i < view->option_count; i++)
            fprintf(stderr, "ARCMP legacy option=%u %s\n", i, view->options[i]);
        return false;
    }
    *revision = view->revision;
    retail_choices++;
    fprintf(stderr, "ARCMP legacy native choice=%s\n", view->options[option]);
    return coop_engine_dialogue_submit((unsigned)option);
}
static bool forest_destination_known(void)
{
    int64_t loc = location_make(script_global_var_get(1926), script_global_var_get(1927));
    int tile = forest_tile(loc);
    if (tile < 0) return false;
    size_t size = 0;
    uint8_t* bytes = townmap_campaign_capture(&size);
    CoopCartography maps = { 0 };
    bool good = bytes && coop_cartography_decode(&maps, bytes, size);
    free(bytes);
    const CoopChart* chart = good ? coop_cartography_find(&maps, (uint32_t)script_global_var_get(1924)) : NULL;
    good = chart && (unsigned)tile < chart->tiles && (chart->bits[tile / 8] & (1u << (tile % 8)));
    coop_cartography_clear(&maps);
    return good;
}

static bool retail_choose(CoopGameplay* game, unsigned slot, uint32_t* revision)
{
    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
    if (!view->active || view->chooser != slot || view->revision == *revision) return true;
    if (voyage_mode) {
        int script = current_phase == 65 ? 1061 : current_phase == 67 ? 1066
            : current_phase == 69                                     ? 1067
            : current_phase == 71                                     ? 1071
                                                                      : 0;
        bool reached = current_phase == 67 ? script_global_flag_get(1035) && critter_pc_leader_get(scripted(1066, SAP_DIALOG)) == pc(slot)
            : current_phase == 69          ? script_global_flag_get(1056)
                                           : current_phase == 71 && script_story_state_get() >= 8;
        if (reached) {
            *revision = view->revision;
            return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
        }
        int goal = current_phase == 65 ? 1900 : current_phase == 67 ? 701
            : current_phase == 69                                   ? 280
                                                                    : 301;
        int option = script ? quest_option(view, script, goal) : -1;
        if (option < 0 && view->option_count == 1) option = 0;
        if (option >= 0) {
            *revision = view->revision;
            retail_choices++;
            fprintf(stderr, "ARCMP voyage native choice=%s\n", view->options[option]);
            return coop_engine_dialogue_submit((unsigned)option);
        }
    }
    if (!legacy_mode && !dark_mode && !forest_mode && !voyage_mode && quest_mode && (current_phase == 49 || current_phase == 58)) {
        bool reached = current_phase == 49 ? quest_state_get(pc(slot), 1013) == QUEST_STATE_ACCEPTED && script_story_state_get() >= 4
                                           : quest_state_get(pc(slot), 1013) == QUEST_STATE_COMPLETED && quest_state_get(pc(slot), 1015) == QUEST_STATE_ACCEPTED && script_global_flag_get(1008);
        if (reached) {
            for (unsigned i = 0; i < view->option_count; i++)
                if (strstr(view->options[i], "I shall return") || strstr(view->options[i], "apprise you of my findings")) {
                    *revision = view->revision;
                    retail_choices++;
                    return coop_engine_dialogue_submit(i);
                }
            *revision = view->revision;
            return coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
        }
        int option = quest_option(view, 1047, current_phase == 58 ? 530 : 470);
        if (option >= 0) {
            *revision = view->revision;
            retail_choices++;
            fprintf(stderr, "ARCMP native quest graph option=%s\n", view->options[option]);
            return coop_engine_dialogue_submit((unsigned)option);
        }
    }
    if (quest_mode && view->option_count == 1) {
        *revision = view->revision;
        retail_choices++;
        return coop_engine_dialogue_submit(0);
    }
    static const char* quest_choices[] = { "were banished to the Isle of Despair", "irresponsibly allowed their technology", "like me to go to the Isle", "Of course. How do I get there", "will take me?", "That is reassuring", "apprise you of my findings", "[more]", "[end]", "about a ring", "about this ring", "A gnome gave it", "A strange looking gnome", "skinny, beardless dwarf", "scar over his left eye", "A dwarf? Clean shaven", "escaped to warn us", "He didn't say", "referred to you as a boy", "How did you come to betray", "I don't understand", "Why would that", "What can we do now", "Accepted.", "I shall return with", "don't have it here", "explore the Black Mountain", "And that would be?", "resourceful", "Thank you", "Good day" };
    if (quest_mode) {
        for (unsigned c = 0; c < SDL_arraysize(quest_choices); c++)
            for (unsigned i = 0; i < view->option_count; i++)
                if (strstr(view->options[i], quest_choices[c])) {
                    *revision = view->revision;
                    retail_choices++;
                    fprintf(stderr, "ARCMP main quest choice=%s\n", view->options[i]);
                    return coop_engine_dialogue_submit(i);
                }
    }
    static const char* choices[] = { "Forgive the intrusion", "utmost importance", "identity of one of your clients", "not here to make trouble",
        "information about a ring", "knowledge you have pertaining to a ring", "Describe the ring", "take a look", "identity of the owner",
        "exactly WHAT", "darker arts", "explains the dwarf zombies", "built your store", "a little leery", "at the crossroads", "some sort of agreement", "might consider this heinous", "come to an agreement",
        "Agreed.", "Your father?", "at a loss", "how talkative", "unnerving", "get on with it", "don't believe we've met", "might already know that", "owner of a ring",
        "What are you talking about?", "speaking in riddles", "Thank you, Pelonious", "Good bye", "Farewell" };
    for (unsigned c = 0; c < SDL_arraysize(choices); c++)
        for (unsigned i = 0; i < view->option_count; i++)
            if (strstr(view->options[i], choices[c])) {
                *revision = view->revision;
                retail_choices++;
                fprintf(stderr, "ARCMP progress retail choice=%s\n", view->options[i]);
                return coop_engine_dialogue_submit(i);
            }
    fprintf(stderr, "ARCMP progress unmatched retail reply=%s\n", view->reply);
    for (unsigned i = 0; i < view->option_count; i++)
        fprintf(stderr, "ARCMP progress option=%u %s\n", i, view->options[i]);
    return false;
}
static CombatAttackObserver previous_attack_observer;
static bool attack_observer_installed;
static void trace_door_attack(const CombatContext* combat, bool resolved)
{
    static unsigned count;
    if (previous_attack_observer) previous_attack_observer(combat, resolved);
    if (!resolved || !combat->target_obj || count >= 16
        || obj_field_int32_get(combat->target_obj, OBJ_F_TYPE) != OBJ_TYPE_PORTAL) return;
    count++;
    fprintf(stderr, "ARCMP original door impact flags=%x damage=%d normal=%d fatigue=%d hp=%d object-flags=%x script=%d attacker-strength=%d\n",
        combat->flags, combat->total_dam, combat->dam[DAMAGE_TYPE_NORMAL], combat->dam[DAMAGE_TYPE_FATIGUE],
        object_hp_current(combat->target_obj), obj_field_int32_get(combat->target_obj, OBJ_F_FLAGS),
        script_number(combat->target_obj, SAP_TAKING_DAMAGE), stat_level_get(combat->attacker_obj, STAT_STRENGTH));
}
void coop_campaign_progress_probe_cleanup(void)
{
    if (attack_observer_installed) combat_attack_observer_set(previous_attack_observer);
    attack_observer_installed = false;
    coop_runtime_present_observer(NULL, NULL);
    coop_runtime_detach();
    coop_session_destroy(held);
    held = NULL;
}
bool coop_campaign_progress_probe(int argc, char** argv)
{
    bool host = false, framebuffer = false;
    unsigned port = 35750, player = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--coop-probe-host")) host = true;
        if (!strcmp(argv[i], "--coop-progress-tail")) tail_mode = true;
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-legacy")) {
            legacy_mode = quest_mode = tail_mode = true;
            final_phase = LEGACY_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-evil")) {
            evil_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = EVIL_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-evil-ending")) {
            evil_ending_mode = evil_mode = finale_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = EVIL_ENDING_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-rescue")) {
            rescue_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = RESCUE_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-combat-finale")) {
            combat_finale_mode = finale_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = COMBAT_FINALE_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-traversal")) {
            traversal_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = TRAVERSAL_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-finale")) {
            finale_mode = legacy_mode = quest_mode = tail_mode = true;
            final_phase = FINALE_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-dark")) {
            dark_mode = quest_mode = tail_mode = true;
            final_phase = DARK_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-forest")) {
            forest_mode = quest_mode = tail_mode = true;
            final_phase = FOREST_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-voyage")) {
            voyage_mode = quest_mode = tail_mode = true;
            final_phase = VOYAGE_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-quests")) {
            quest_mode = tail_mode = true;
            final_phase = QUEST_PHASES;
        }
        if (!strcmp(argv[i], "--coop-engine-probe=runtime-regions")) {
            regional_mode = true;
            final_phase = REGIONAL_PHASES;
            tail_mode = true;
        }
        if (!strncmp(argv[i], "--coop-progress-first=", 22)) first_phase = (unsigned)atoi(argv[i] + 22);
        if (!strcmp(argv[i], "--coop-probe-framebuffer")) framebuffer = true;
        if (!strncmp(argv[i], "--coop-probe-port=", 18)) port = (unsigned)atoi(argv[i] + 18);
        if (!strncmp(argv[i], "--coop-probe-player=", 20)) player = (unsigned)atoi(argv[i] + 20);
    }
    if (host && getenv("ARCMP_TRACE_ATTACK")) {
        previous_attack_observer = combat_attack_observer_set(trace_door_attack);
        attack_observer_installed = true;
    }
    if (!first_phase) first_phase = evil_mode ? EVIL_FIRST : rescue_mode ? RESCUE_FIRST
        : evil_ending_mode                                               ? EVIL_ENDING_FIRST
        : combat_finale_mode                                             ? COMBAT_FINALE_FIRST
        : traversal_mode                                                 ? TRAVERSAL_FIRST
        : finale_mode                                                    ? FINALE_FIRST
        : legacy_mode                                                    ? LEGACY_FIRST
        : dark_mode                                                      ? 97
        : forest_mode                                                    ? 74
        : voyage_mode                                                    ? 61
        : quest_mode                                                     ? 48
        : regional_mode                                                  ? 23
        : tail_mode                                                      ? 10
                                                                         : 1;
    if ((!quest_mode && !regional_mode && ((!tail_mode && first_phase != 1) || (tail_mode && first_phase != 10 && first_phase != 13 && first_phase != 17 && first_phase != 19 && first_phase != 20))) || (regional_mode && (first_phase < 23 || first_phase > REGIONAL_PHASES)) || (!legacy_mode && !dark_mode && !forest_mode && !voyage_mode && quest_mode && (first_phase < 48 || first_phase > QUEST_PHASES)) || (voyage_mode && (first_phase < 61 || first_phase > VOYAGE_PHASES))) return false;
    if (legacy_mode && (first_phase < LEGACY_FIRST || first_phase > final_phase)) return false;
    if (dark_mode && (first_phase < 97 || first_phase > DARK_PHASES)) return false;
    if (forest_mode && (first_phase < 74 || first_phase > FOREST_PHASES)) return false;
    if (!port || port > 65535 || player > 3 || host != (player == 0)) return false;
    CoopConfig config = { 0 };
    snprintf(config.name, sizeof(config.name), "Progress player %u", player);
    FILE* content = fopen("arcmp-content.id", "rb");
    char hex[66] = { 0 };
    if (!content) return false;
    bool good = fread(hex, 1, 65, content) >= 64;
    fclose(content);
    for (unsigned i = 0; good && i < 32; i++) {
        char pair[] = { hex[i * 2], hex[i * 2 + 1], 0 }, *end;
        config.content_id[i] = (uint8_t)strtoul(pair, &end, 16);
        good = !*end;
    }
    good = good && coop_identity_load_or_create("arcmp-player.id", config.identity);
    held = good ? coop_session_create(&config) : NULL;
    if (!held) return false;
    uint64_t start = SDL_GetTicks(), playing = 0, chats = 0, at = 0, ended = 0, dialog_at = 0;
    uint32_t interlude_revision = 0;
    bool interlude_frame = false;
    unsigned phase = 0, completed = 0, ack[4] = { 0 }, dark_acks[4] = { 0 }, baseline = 0, passage_vote_epoch = UINT_MAX;
    bool dark_marked = false;
    uint32_t result_before = 0, dialog_revision = 0;
    unsigned status = 0;
    uint32_t follower_before = 0;
    unsigned lock_step = 0;
    uint64_t measure_at = 0, instance = 0, motion_at = 0, combat_walk_at = 0;
    uint64_t fault_wait_at = 0;
    unsigned fault_epoch = 0;
    bool fault_requested = false;
    int64_t motion_loc = 0;
    TigSoundSignal before = { 0 };
    tig_sound_handle_t measured = TIG_SOUND_HANDLE_INVALID;
    if (regional_mode) tig_sound_probe_enable(true);
    if (evil_mode && getenv("ARCMP_TRACE_WORLD")) coop_runtime_probe_enable();
    bool selected = false, initialized = false, done = false, sent = false, voted = false, finishing = false, talked = false, spirit_talked = false, reveal_seen = false, walked = false, gate_sent = false, office_done = false, guard_done = false, guard_sent = false, door_work = false;
    good = (host ? coop_session_host(held, (uint16_t)port, start) : coop_session_join(held, "127.0.0.1", (uint16_t)port, start)) && coop_runtime_attach(held, config.identity);
    FinaleObserver finale = { .framebuffer = framebuffer, .good = true };
    uint64_t phase_media = 0;
    if (finale_mode || evil_mode) {
        tig_movie_probe_mode(false);
        coop_runtime_present_observer(finale_observe, &finale);
    }
    fprintf(stderr, "ARCMP bootstrap connection host=%d port=%u\n", host, port);
    fflush(stderr);
    while (good && SDL_GetTicks() - start < (legacy_mode ? 1800000u : dark_mode ? 900000u
                           : quest_mode                                         ? 420000u
                           : regional_mode                                      ? 450000u
                                                                                : 360000u)) {
        uint64_t now = SDL_GetTicks();
        tig_ping();
        coop_session_poll(held, now);
        coop_lobby_tick();
        if (finale_mode && finale.terminal && coop_runtime_phase() == COOP_RUNTIME_OFF) {
            good &= !coop_campaign_active() && mainmenu_ui_is_active() && mainmenu_ui_window_type_get() == MM_WINDOW_MAINMENU;
            if (framebuffer) {
                tig_window_display();
                good &= tig_video_screenshot_save("arcmp-probe.bmp") == TIG_OK;
            }
            break;
        }
        if (coop_runtime_phase() == COOP_RUNTIME_FAILED) {
            fprintf(stderr, "ARCMP progress runtime: %s\n", coop_runtime_status());
            good = false;
            break;
        }
        CoopGameplay* game = coop_runtime_gameplay();
        unsigned slot = coop_session_local_slot(held);
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(held) == COOP_LOBBY) {
            if (!selected) {
                char name[32];
                snprintf(name, sizeof(name), "Progress hero %u", player);
                selected = coop_session_character(held, 16067 + player, name);
            } else if (!coop_session_players(held)[slot].ready) {
                bool all = true;
                for (unsigned i = 0; i < 4; i++)
                    all &= coop_session_players(held)[i].connected && coop_session_players(held)[i].prototype;
                if (all) coop_session_ready(held, true);
            }
            if (host && coop_session_all_ready(held)) good = tail_mode ? coop_runtime_recover_checkpoint() : coop_runtime_start();
        }
        if (game && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            if (!playing) playing = now;
            const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
            if (traversal_mode && view->active && view->mode == 0) {
                int64_t speaker = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
                if (speaker && script_number(speaker, SAP_DIALOG) == 1864) {
                    if (framebuffer && !interlude_frame) {
                        gamelib_redraw();
                        iso_redraw();
                        tig_window_display();
                        good &= tig_video_screenshot_save("arcmp-traversal-interlude.bmp") == TIG_OK;
                        interlude_frame = true;
                    }
                    if (view->chooser == slot && view->revision != interlude_revision && now - dialog_at > 180 && view->option_count) {
                        interlude_revision = view->revision;
                        fprintf(stderr, "ARCMP traversal original interlude choice=%s\n", view->options[0]);
                        good &= coop_engine_dialogue_submit(0);
                        dialog_at = now;
                    }
                }
            }
            if (phase != 13 && !quest_mode && view->active && view->chooser == slot) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (host && !initialized && (!view->active || (finale_mode && first_phase == final_phase)
                    || (evil_mode && first_phase == 241)) && now - playing > 1000) {
                int menu = campaign_menu_check(now, framebuffer);
                if (menu < 0) {
                    fprintf(stderr, "ARCMP native host menu failed\n");
                    good = false;
                } else if (menu) {
                    initialized = true;
                    good = announce(first_phase);
                }
            }
        }
        for (; chats < coop_session_chat_count(held); chats++) {
            const CoopChat* chat = coop_session_chat_at(held, chats);
            unsigned value, index;
            int member;
            if (!chat->slot && sscanf(chat->text, "progress-bind:%u:%d", &index, &member) == 2 && index < 4 && member >= 0 && member < 4) bindings[index] = member;
            if (!chat->slot && sscanf(chat->text, "dungeon-start:%u", &value) == 1 && value < 4) evil_floor_start = value;
            if (!chat->slot && sscanf(chat->text, "void-start:%u", &value) == 1 && value <= 4) evil_void_start = value;
            if (!chat->slot && sscanf(chat->text, "progress-phase:%u", &value) == 1 && value == (!phase ? first_phase : phase + 1)) {
                phase = current_phase = value;
                at = now;
                motion_at = now;
                motion_loc = 0;
                phase_media = finale.media_time;
                done = sent = voted = talked = voyage_seen = dark_marked = walked = gate_sent = office_done = guard_done = guard_sent = false;
                baseline = coop_runtime_epoch();
                lock_step = 0;
                measure_at = 0;
                combat_walk_at = 0;
                fault_wait_at = 0;
                fault_epoch = 0;
                fault_requested = false;
                instance = 0;
                dialog_revision = 0;
                quest_edge_script = 0;
                result_before = game ? coop_items_last_result(coop_gameplay_items(game), &status) : 0;
                follower_before = game ? coop_followers_last_result(coop_gameplay_followers(game), &status) : 0;
                fprintf(stderr, "ARCMP progress phase=%u slot=%u epoch=%u map=%d\n", phase, slot, baseline, map_current_map());
            }
            if (chat->slot < 4 && sscanf(chat->text, "progress-done:%u", &value) == 1) ack[chat->slot] = value;
            if (combat_finale_mode && host && !strcmp(chat->text, "device-wrong-form:210") && chat->slot == legacy_actor(210)) script_global_var_set(1842, 1);
            if (traversal_mode && !strcmp(chat->text, "companion-refused:182") && chat->slot == (legacy_actor(182) + 1) % 4) traversal_refused = true;
            if (chat->slot < 4 && (sscanf(chat->text, "dark-ready:%u", &value) == 1 || sscanf(chat->text, "legacy-ready:%u", &value) == 1)) dark_acks[chat->slot] = value;
        }
        if (phase && !done && game && coop_runtime_phase() == COOP_RUNTIME_PLAYING && now - at > 500) {
            bool ready = true;
            CoopItems* items = coop_gameplay_items(game);
            if (phase == 1) ready = script_global_var_get(1985) == 220 && tagged(false) && tagged(true);
            if (phase == 2 || phase == 4 || phase == 5 || phase == 6 || phase == 7) {
                if (slot == acting(phase) && !sent) {
                    good = click_object(tagged(phase != 2));
                    sent = true;
                    ready = false;
                }
                if (slot == acting(phase)) ready &= coop_items_last_result(items, &status) > result_before && !coop_items_busy(items) && status == (phase == 6 ? COOP_LOOT_LOCKED : 0);
                if (phase == 2)
                    ready &= script_global_var_get(1980) == stat_level_get(pc(2), STAT_STRENGTH)
                        && script_global_var_get(1981) == script_global_var_get(1980) && script_global_var_get(1984) == script_global_var_get(1980) && script_global_var_get(1983) == 1;
                else
                    ready &= portal_is_open(tagged(true)) == (phase == 4 || phase == 7);
            }
            if (phase == 3) {
                if (slot == 2 && !sent) {
                    uint8_t bytes[59] = { 0 };
                    CoopObjectId id;
                    good = coop_object_id_from_engine(obj_get_id(tagged(false)), &id);
                    coop_object_write32(bytes, coop_runtime_epoch());
                    coop_object_write32(bytes + 4, coop_items_last_result(items, &status));
                    coop_object_write32(bytes + 8, (uint32_t)map_current_map());
                    memcpy(bytes + 12, obj_get_id(player_get_local_pc_obj()).d.g.data, 16);
                    bytes[28] = COOP_WORLD_USE;
                    memcpy(bytes + 29, id.bytes, sizeof(id.bytes));
                    coop_object_write32(bytes + 47, UINT32_MAX);
                    coop_object_write32(bytes + 55, combat_turn_generation_get());
                    good = good && coop_session_send(held, 0, COOP_ITEM_REQUEST, bytes, sizeof(bytes));
                    sent = true;
                }
                ready = now - at > 1200 && script_global_var_get(1983) == 1;
            }
            if (phase == 8) {
                if (slot == 2 && !sent) {
                    good = click_object(tagged(false));
                    sent = true;
                }
                ready = coop_runtime_epoch() == baseline + 1;
            }
            if (phase == 9) {
                if (host && !sent) {
                    good = transfer_script(SCRIPT_TARANT, 1, location_make(62107, 65623), 1);
                    sent = true;
                }
                ready = coop_runtime_epoch() == baseline + 1;
            }
            if (stair_script(phase)) {
                if (slot == acting(phase) && !sent) {
                    int64_t target = scripted(stair_script(phase), SAP_USE);
                    good = click_object(target);
                    sent = true;
                }
                ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == map_after(phase);
                if (slot == acting(phase) && coop_items_last_result(items, &status) > result_before && status) {
                    fprintf(stderr, "ARCMP progress retail stairs refused script=%d status=%u\n", stair_script(phase), status);
                    good = false;
                    if (framebuffer) tig_video_screenshot_save("arcmp-progress-stairs-failed.bmp");
                }
            }
            if (phase == 13) {
                if (host) {
                    int64_t npc = scripted(1049, SAP_DIALOG);
                    if (npc) object_flags_unset(npc, OF_OFF);
                }
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                if (view->active) talked = true;
                if (slot == 1 && !talked && !view->active) {
                    good = coop_runtime_talk(scripted(1049, SAP_DIALOG));
                    talked = true;
                }
                int64_t spirit = scripted(1054, SAP_DIALOG);
                if (slot == 1 && !spirit_talked && !view->active && spirit
                    && (obj_field_int32_get(spirit, OBJ_F_SPELL_FLAGS) & OSF_SPOKEN_WITH_DEAD)) {
                    good = click_object(spirit);
                    spirit_talked = true;
                }
                if (view->active && strstr(view->reply, "Gilbert Bates") && !reveal_seen) {
                    reveal_seen = true;
                    if (framebuffer) {
                        gamelib_redraw();
                        iso_redraw();
                        tig_window_display();
                        good &= tig_video_screenshot_save("arcmp-progress-campaign-13.bmp") == TIG_OK;
                    }
                }
                if (now - dialog_at > 200) {
                    good = good && retail_choose(game, slot, &dialog_revision);
                    dialog_at = now;
                }
                ready = script_global_flag_get(1022) && script_story_state_get() >= 2 && !view->active;
                for (unsigned i = 0; i < 4; i++)
                    ready &= quest_state_get(pc(i), 1011) == QUEST_STATE_COMPLETED;
            }
            if (phase == 17) ready = script_global_var_get(1985) == 220 && script_global_flag_get(1022) && script_story_state_get() >= 2 && visuals_match();
            if (phase == 18 || phase == 19) {
                unsigned actor = phase == 18 ? 0 : 2;
                const CoopControlView* view = coop_controls_current(coop_runtime_controls());
                if (slot == actor && !sent) {
                    good = coop_runtime_control(phase == 18 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0);
                    sent = true;
                }
                if (phase == 19 && view->phase == COOP_CONTROL_VOTING && slot != actor && !voted) {
                    good = coop_runtime_control(COOP_CONTROL_VOTE, 1, view->proposal, 0);
                    voted = true;
                }
                ready = coop_runtime_epoch() == baseline + 1 && view->phase == COOP_CONTROL_IDLE && view->notice == COOP_CONTROL_OK && script_global_flag_get(1022) && visuals_match();
            }
            if (phase == 20) ready = own_view(slot);
            if (phase == 21) ready = script_global_var_get(1982) == stat_level_get(pc(2), STAT_STRENGTH);
            if (phase == 22) ready = visuals_match();
            if (regional_mode && phase >= 23 && phase <= 32) {
                CoopEnvironmentState canonical = { 0 };
                gsound_campaign_state_get(&canonical);
                uint32_t pair[2];
                bool regional = gsound_view_schemes(pair);
                unsigned music = (unsigned)script_global_var_get((phase == 29 || phase == 30 || phase == 31) ? 1951 : 1950);
                bool want_regional = phase <= 24 || phase == 32 || ((phase == 26 || phase == 27) && slot == 2);
                if (want_regional) music = (unsigned)script_global_var_get(slot % 2 || (phase >= 26 && slot == 2) ? 1951 : 1950);
                unsigned ambient = (unsigned)script_global_var_get(1952);
                bool combat = phase >= 28 && phase <= 30;
                ready = regional == want_regional && (combat ? canonical.combat : pair[0] == (music < ambient ? music : ambient) && pair[1] == (music < ambient ? ambient : music));
                if (phase <= 24 || phase == 32) ready &= !canonical.scripted_sound && !canonical.schemes[0] && !canonical.schemes[1];
                if (phase == 25) ready &= canonical.scripted_sound && canonical.sound_members == 15;
                if (phase == 26) ready &= canonical.scripted_sound && !(canonical.sound_members & (1u << (unsigned)bindings[2]));
                if (phase == 27) {
                    if (!lock_step) {
                        gsound_lock();
                        gsound_lock();
                        lock_step = 1;
                        ready = false;
                    } else if (lock_step == 1 && now - at > 900) {
                        gsound_unlock();
                        lock_step = 2;
                        ready = false;
                    } else if (lock_step == 2 && now - at > 1200) {
                        tig_sound_handle_t ignored;
                        good &= !gsound_campaign_music(&ignored, false);
                        gsound_unlock();
                        lock_step = 3;
                        ready = false;
                    }
                    ready &= lock_step == 3;
                }
                if (phase == 29 || phase == 30) {
                    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                    unsigned actor = phase == 29 ? 0 : 2;
                    if (slot == actor && !sent) {
                        good = coop_runtime_control(phase == 29 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, 0, 0, 0);
                        sent = true;
                    }
                    if (phase == 30 && control->phase == COOP_CONTROL_VOTING && slot != actor && !voted) {
                        good = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                        voted = true;
                    }
                    ready &= coop_runtime_epoch() == baseline + 1 && control->phase == COOP_CONTROL_IDLE && control->notice == COOP_CONTROL_OK && canonical.scripted_sound;
                }
                if (ready && (phase == 24 || phase == 25 || phase == 26 || phase == 28 || phase == 31 || phase == 32)) {
                    tig_sound_handle_t handle;
                    if (!gsound_campaign_music(&handle, combat))
                        ready = false;
                    else if (!measure_at) {
                        measured = handle;
                        instance = tig_sound_instance(handle);
                        good &= tig_sound_probe_signal(handle, &before);
                        measure_at = now;
                        ready = false;
                    } else if (now - measure_at < 600)
                        ready = false;
                    else {
                        TigSoundSignal after = { 0 };
                        good &= measured == handle && instance == tig_sound_instance(handle) && tig_sound_probe_signal(handle, &after);
                        uint64_t frames = after.frames - before.frames;
                        double energy = after.left - before.left + after.right - before.right;
                        good &= frames > 1000 && energy > 0;
                        if (good) {
                            pcm_phases++;
                            fprintf(stderr, "ARCMP regional PCM phase=%u frames=%" PRIu64 " energy=%f instance=%" PRIu64 "\n", phase, frames, energy, instance);
                        }
                    }
                }
            }
            if (phase == 33) ready = script_global_var_get(1953) == 1;
            if (regional_mode && phase >= 34) {
                int exit = region_exit(phase);
                if (host && !exit && !sent) {
                    good = region_transfer(phase);
                    sent = true;
                }
                if (exit && slot == phase % 4 && !sent) {
                    good = click_object(scripted(exit, SAP_USE));
                    sent = true;
                }
                ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == region_after(phase);
                if (ready && exit) retail_exits++;
            }
            if (quest_mode && !voyage_mode) {
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                if (phase == 48 || phase == 54 || phase == 57) {
                    int map = phase == 54 ? 6 : 13;
                    if (host && !sent) {
                        int64_t x, y;
                        good = map_get_starting_location(map, &x, &y) && transfer_script(phase == 48 ? 999970 : phase == 54 ? 999971
                                                                                                                            : 999972,
                                   map, location_make(x, y), phase % 4);
                        sent = true;
                    }
                    ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == map;
                } else if (phase == 49 || phase == 55 || phase == 58) {
                    unsigned actor = phase == 49 ? 1 : phase == 55 ? 2
                                                                   : 3;
                    if (slot == actor && !sent && !view->active) {
                        good = phase == 55 ? click_object(scripted(1168, SAP_DIALOG)) : coop_runtime_talk(scripted(1047, SAP_DIALOG));
                        sent = true;
                    }
                    if (view->active && view->chooser == actor) {
                        if (phase == 49 && strstr(view->reply, "Black Mountain")) bates_seen = true;
                        if (phase == 55 && strstr(view->reply, "banishment of the Black Mountain")) pillar_seen = true;
                        if (framebuffer && ((phase == 49 && bates_seen) || (phase == 55 && pillar_seen)) && !talked) {
                            char path[80];
                            snprintf(path, sizeof(path), "arcmp-progress-campaign-%u.bmp", phase);
                            gamelib_redraw();
                            iso_redraw();
                            tig_window_display();
                            good &= tig_video_screenshot_save(path) == TIG_OK;
                            talked = true;
                        }
                    }
                    if (now - dialog_at > 160) {
                        good &= retail_choose(game, slot, &dialog_revision);
                        dialog_at = now;
                    }
                    ready = !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending();
                    if (phase == 49) {
                        ready &= bates_seen && script_story_state_get() >= 4;
                        for (unsigned i = 0; i < 4; i++)
                            ready &= quest_state_get(pc(i), 1013) == QUEST_STATE_ACCEPTED;
                    } else if (phase == 55)
                        ready &= pillar_seen && script_global_flag_get(1007) && script_story_state_get() >= 5;
                    else
                        for (unsigned i = 0; i < 4; i++)
                            ready &= quest_state_get(pc(i), 1013) == QUEST_STATE_COMPLETED && quest_state_get(pc(i), 1015) == QUEST_STATE_ACCEPTED;
                    if ((phase == 49 || phase == 55) && ready) {
                        if (coop_runtime_epoch() == baseline) {
                            if (host && !voted) {
                                good = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                                voted = true;
                            }
                            ready = false;
                        } else
                            ready = coop_runtime_epoch() == baseline + 1;
                    }
                } else if (phase == 50) {
                    if (view->active) {
                        queued_owners |= 1u << view->chooser;
                        if (view->chooser == slot && view->revision != dialog_revision) {
                            dialog_revision = view->revision;
                            good &= coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                        }
                    }
                    ready = coop_runtime_epoch() == baseline + 1 && queued_owners == 15 && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending();
                } else if (phase == 51 || phase == 53 || phase == 59) {
                    unsigned actor = phase == 53 ? 2 : 0;
                    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                    if (slot == actor && !sent) {
                        good = coop_runtime_control(phase == 53 ? COOP_CONTROL_LOAD : COOP_CONTROL_SAVE, 0, 0, 0);
                        sent = true;
                    }
                    if (phase == 53 && control->phase == COOP_CONTROL_VOTING && slot != actor && !voted) {
                        good = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                        voted = true;
                    }
                    ready = coop_runtime_epoch() == baseline + 1;
                    if (phase == 53) ready &= coop_journal_count(coop_engine_journal()) == (unsigned)script_global_var_get(1940);
                } else if (phase == 52)
                    ready = coop_journal_count(coop_engine_journal()) > (unsigned)script_global_var_get(1940);
                else if (phase == 56) {
                    if (slot == 3 && !sent) {
                        char text[2000];
                        good = sub_57CD60(player_get_local_pc_obj(), scripted(1143, SAP_EXAMINE), text);
                        sent = true;
                    }
                    CoopDialogueView page;
                    char speaker[COOP_JOURNAL_SPEAKER];
                    unsigned count = coop_journal_count(coop_engine_journal());
                    ready = count && coop_journal_page(coop_engine_journal(), count - 1, &page, speaker) && page.mode == COOP_DIALOGUE_FLOAT && strstr(page.reply, "pillar");
                } else if (phase == 60) {
                    if (!sent) {
                        TigMessage key = { .type = TIG_MESSAGE_KEYBOARD };
                        key.data.keyboard.pressed = true;
                        key.data.keyboard.scancode = SDL_SCANCODE_F8;
                        key.data.keyboard.key = SDLK_F8;
                        tig_message_enqueue(&key);
                        key.data.keyboard.pressed = false;
                        tig_message_enqueue(&key);
                        sent = true;
                    }
                    ready = false;
                    if (coop_dialogue_ui_browsing() && !lock_step) {
                        unsigned count = coop_journal_count(coop_engine_journal());
                        CoopDialogueView page;
                        char speaker[COOP_JOURNAL_SPEAKER];
                        unsigned target = count;
                        for (unsigned i = 0; i < count; i++)
                            if (coop_journal_page(coop_engine_journal(), i, &page, speaker) && strstr(page.reply, "banishment of the Black Mountain")) {
                                target = i;
                                break;
                            }
                        if (target == count) {
                            good = false;
                            break;
                        }
                        for (unsigned i = target + 1; i < count; i++) {
                            TigMessage key = { .type = TIG_MESSAGE_KEYBOARD };
                            key.data.keyboard.pressed = true;
                            key.data.keyboard.scancode = SDL_SCANCODE_LEFT;
                            key.data.keyboard.key = SDLK_LEFT;
                            tig_message_enqueue(&key);
                        }
                        TigMessage forbidden = { .type = TIG_MESSAGE_KEYBOARD };
                        forbidden.data.keyboard.pressed = true;
                        forbidden.data.keyboard.scancode = SDL_SCANCODE_1;
                        forbidden.data.keyboard.key = SDLK_1;
                        tig_message_enqueue(&forbidden);
                        instance = target;
                        measure_at = now;
                        lock_step = 1;
                    }
                    if (lock_step && now - measure_at > 150) ready = coop_dialogue_ui_browsing() && coop_dialogue_ui_index() == instance && !view->active && !coop_dialogue_accepted(coop_gameplay_dialogue(game));
                }
            }
            if (voyage_mode) {
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                unsigned actor = voyage_actor(phase);
                if (phase == 61 || phase == 68) {
                    int map = phase == 61 ? 1 : 7;
                    int64_t x = phase == 61 ? 32205 : 480, y = phase == 61 ? 62499 : 224;
                    if (host && !sent) {
                        good = transfer_script(phase == 61 ? 999964 : 999965, map, location_make(x, y), 2);
                        sent = true;
                    }
                    if (phase == 68 && view->active && now - dialog_at > 160) {
                        good &= retail_choose(game, slot, &dialog_revision);
                        dialog_at = now;
                    }
                    ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == map && !view->active;
                    if (phase == 68 && coop_runtime_epoch() == baseline + 1) {
                        if (!measure_at) measure_at = now;
                        int64_t npc = scripted(1066, SAP_DIALOG);
                        ready &= now - measure_at > 2000 && npc && (critter_pc_leader_get(npc) == pc(voyage_actor(67)) || (!critter_pc_leader_get(npc) && script_global_flag_get(1049)));
                    }
                } else if (phase == 62)
                    ready = script_global_var_get(1934) == 1 && script_global_var_get(1931) == stat_level_get(pc(2), STAT_STRENGTH) && script_global_var_get(1932) == stat_level_get(pc(2), STAT_INTELLIGENCE);
                else if (phase == 63 || phase == 64) {
                    unsigned mode = phase == 63 ? 5 : 6;
                    if (slot == actor && view->active && view->mode == 0 && !sent) {
                        good = coop_engine_dialogue_submit(phase == 63 ? 0 : 1);
                        sent = true;
                    }
                    if (view->active && view->mode == mode) {
                        voyage_seen = true;
                        if (slot == actor && !lock_step) {
                            bool visible = phase == 63 ? charedit_is_created() : wmap_ui_is_created();
                            if (visible) {
                                if (framebuffer) {
                                    gamelib_redraw();
                                    iso_redraw();
                                    tig_window_display();
                                    char path[80];
                                    snprintf(path, sizeof(path), "arcmp-progress-campaign-%u.bmp", phase);
                                    good &= tig_video_screenshot_save(path) == TIG_OK;
                                }
                                if (phase == 63) {
                                    TigMessage close = { .type = TIG_MESSAGE_MOUSE };
                                    close.data.mouse.x = 50;
                                    close.data.mouse.y = 95;
                                    close.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
                                    tig_message_enqueue(&close);
                                    close.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
                                    tig_message_enqueue(&close);
                                } else {
                                    int64_t loc = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                                    good = coop_runtime_journey(location_make(location_get_x(loc) + 64, location_get_y(loc)));
                                    wmap_ui_close();
                                }
                                lock_step = 1;
                            }
                        }
                    }
                    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                    if (phase == 64 && control->phase == COOP_CONTROL_VOTING) {
                        good &= !view->active;
                        if (slot != actor && !voted) {
                            good &= coop_runtime_control(COOP_CONTROL_VOTE, 0, control->proposal, 0);
                            voted = true;
                        }
                        talked = true;
                    }
                    if (phase == 64 && control->action == COOP_CONTROL_TRAVEL && control->notice == COOP_CONTROL_DECLINED) talked = true;
                    ready = voyage_seen && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && (slot != actor || lock_step) && (phase == 63 || (talked && control->phase == COOP_CONTROL_IDLE));
                } else if (phase == 65 || phase == 67 || phase == 69 || phase == 71) {
                    int number = phase == 65 ? 1061 : phase == 67 ? 1066
                        : phase == 69                             ? 1067
                                                                  : 1071;
                    bool accepted;
                    uint32_t result = coop_gameplay_last_result(game, &accepted);
                    if (sent && !voyage_seen && !view->active && result != instance && !accepted && now - dialog_at > 1000) sent = false;
                    if (slot == actor && !sent && !view->active) {
                        good = coop_runtime_talk(scripted(number, SAP_DIALOG));
                        instance = result;
                        dialog_at = now;
                        sent = true;
                    }
                    if (view->active) {
                        voyage_seen = true;
                        /* Retail may initiate this scene for an arriving owner
                         * before the fixture's manual talk is admitted. */
                        if (host && phase == 71 && script_number(obj_pool_perm_lookup(coop_object_id_to_engine(view->npc)), SAP_DIALOG) == 1071)
                            for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
                                if (!memcmp(coop_campaign_member(i)->object_id.d.g.data, view->pc_guid, 16)) script_global_var_set(1939, (int)i);
                        if (now - dialog_at > 160) {
                            good &= retail_choose(game, slot, &dialog_revision);
                            dialog_at = now;
                        }
                    }
                    ready = voyage_seen && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending();
                    if (phase == 65) {
                        uint64_t before = (uint64_t)(unsigned)script_global_var_get(1935) * COOP_DAY_MS + (unsigned)script_global_var_get(1936);
                        uint64_t elapsed = campaign_clock() - before;
                        int64_t npc = scripted(1061, SAP_DIALOG);
                        ready &= coop_runtime_epoch() >= baseline + 1 && map_current_map() == 1 && location_dist(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION), location_make(20691, 56027)) < 30
                            && npc && location_dist(obj_field_int64_get(npc, OBJ_F_LOCATION), location_make(20680, 56024)) < 20 && elapsed >= 240000000 && elapsed < 240120000;
                    } else if (phase == 67) {
                        int64_t npc = scripted(1066, SAP_DIALOG);
                        ready &= npc && critter_pc_leader_get(npc) == pc(actor) && script_global_flag_get(1035) && script_story_state_get() >= 7;
                        for (unsigned i = 0; i < 4; i++)
                            ready &= quest_state_get(pc(i), 1015) == QUEST_STATE_COMPLETED;
                    } else if (phase == 69)
                        ready &= script_global_flag_get(1056);
                    else {
                        ready &= script_story_state_get() >= 8;
                        int owner = script_global_var_get(1939);
                        ready &= owner >= 0 && coop_campaign_member((unsigned)owner) != NULL && item_find_by_name(coop_campaign_object((unsigned)owner), 5203) != 0;
                        if (script_story_state_get() >= 8 && !view->active && !measure_at) {
                            measure_at = now;
                            for (unsigned i = 0; i < 4; i++)
                                fprintf(stderr, "ARCMP voyage reward slot=%u letter=%lld inventory=%d quest17=%d quest18=%d\n", i, (long long)item_find_by_name(pc(i), 5203), obj_field_int32_get(pc(i), OBJ_F_CRITTER_INVENTORY_NUM), quest_state_get(pc(i), 1017), quest_state_get(pc(i), 1018));
                            int64_t object;
                            int iterator;
                            if (obj_inst_first(&object, &iterator)) do {
                                    if (item_is_item(object) && obj_field_int32_get(object, OBJ_F_NAME) == 5203) {
                                        int64_t parent = 0;
                                        item_parent(object, &parent);
                                        fprintf(stderr, "ARCMP voyage letter object=%lld parent=%lld location=%lld flags=%x\n", (long long)object, (long long)parent, (long long)obj_field_int64_get(object, OBJ_F_LOCATION), obj_field_int32_get(object, OBJ_F_FLAGS));
                                    }
                                } while (obj_inst_next(&object, &iterator));
                        }
                        for (unsigned i = 0; i < 4; i++)
                            ready &= quest_state_get(pc(i), 1018) == QUEST_STATE_ACCEPTED && quest_state_get(pc(i), 1017) == QUEST_STATE_COMPLETED;
                    }
                } else if (phase == 66)
                    ready = scripted(1066, SAP_DIALOG) != 0;
                else if (phase == 70 || phase == 72) {
                    int script = phase == 70 ? script_global_var_get(1938) : 1212;
                    if (slot == actor && !sent && script > 0) {
                        int64_t here = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                        location_origin_set(here);
                        map_precache_sectors(here);
                        int64_t target = scripted(script, SAP_USE);
                        fprintf(stderr, "ARCMP voyage entrance use script=%d target=%lld local=%lld\n", script, (long long)target, (long long)here);
                        /* The secret door's art is occluded by the throne.
                         * Submit the same owner-authenticated use as world UI. */
                        good = target && (phase == 70 ? coop_runtime_world_use(target) : click_object(target));
                        sent = true;
                    }
                    if (slot == actor && coop_items_last_result(items, &status) != result_before && status) good = false;
                    ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == 7;
                } else if (phase == 73) {
                    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                    if (!lock_step) {
                        if (host && !sent) {
                            good = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                            sent = true;
                        }
                        if (coop_runtime_epoch() == baseline + 1) {
                            lock_step = 1;
                            sent = voted = false;
                        }
                    } else {
                        if (slot == 2 && !sent && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch() && !coop_controls_pending(coop_runtime_controls())) {
                            good = coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0);
                            sent = true;
                        }
                        if (slot != 2 && !voted && control->phase == COOP_CONTROL_VOTING) {
                            good = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                            voted = true;
                        }
                    }
                    int owner = script_global_var_get(1939);
                    ready = coop_runtime_epoch() == baseline + 2 && script_story_state_get() >= 8 && owner >= 0 && coop_campaign_member((unsigned)owner) != NULL && item_find_by_name(coop_campaign_object((unsigned)owner), 5203) != 0;
                }
                if ((phase == 62 || phase == 65 || phase == 67 || phase == 69 || phase == 71) && ready) {
                    unsigned before_save = phase == 65 ? 1 : 0;
                    if (coop_runtime_epoch() == baseline + before_save) {
                        if (host && !voted) {
                            good = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                            voted = true;
                        }
                        ready = false;
                    } else
                        ready = coop_runtime_epoch() == baseline + before_save + 1;
                }
            }

            if (dark_mode) {
                ready = false;
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                if (script_global_var_get(1889) == (int)phase || coop_runtime_epoch() > baseline) {
                    unsigned actor = dark_actor(phase);
                    if (dark_transfer(phase)) {
                        if (host && !sent) {
                            good = dark_do_transfer(phase);
                            sent = true;
                        }
                        ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == 1;
                    } else if (dark_dialog(phase)) {
                        bool accepted;
                        uint32_t result = coop_gameplay_last_result(game, &accepted);
                        if (sent && !voyage_seen && !view->active && result != instance && !accepted && now - dialog_at > 1000) sent = false;
                        if (slot == actor && !sent && !view->active) {
                            int64_t npc = scripted(dark_dialog(phase), SAP_DIALOG);
                            if (npc) {
                                sent = coop_runtime_talk(npc);
                                instance = result;
                                dialog_at = now;
                            }
                        }
                        if (view->active) {
                            voyage_seen = true;
                            if (framebuffer && !talked) {
                                char file[80];
                                snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                                gamelib_redraw();
                                iso_redraw();
                                tig_window_display();
                                good &= tig_video_screenshot_save(file) == TIG_OK;
                                talked = true;
                            }
                            if (now - dialog_at > 180) {
                                good &= dark_choose(game, slot, &dialog_revision);
                                dialog_at = now;
                            }
                        }
                        ready = voyage_seen && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending() && dark_ready(phase);
                    } else if (phase == 99 || phase == 106 || phase == 117) {
                        if (slot == actor && !sent) {
                            int64_t item = dark_document(phase);
                            if (item) sent = coop_runtime_use_item(item, NULL, false);
                        }
                        unsigned type;
                        int number;
                        bool variant;
                        if (slot == actor && written_ui_coop_state(&type, &number, &variant)) {
                            if (framebuffer && !talked) {
                                char file[80];
                                snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                                gamelib_redraw();
                                iso_redraw();
                                tig_window_display();
                                good &= tig_video_screenshot_save(file) == TIG_OK;
                            }
                            talked = true;
                            written_ui_coop_stop();
                        }
                        ready = dark_ready(phase) && (slot != actor || (sent && talked));
                    } else if (phase == 100) {
                        ready = coop_engine_documents()->count == 1 && dark_library_read(phase, &lock_step, framebuffer, &good) && script_story_state_get() == 10;
                    } else if (phase == 105) {
                        int member = script_global_var_get(1882);
                        unsigned destination = 4;
                        for (unsigned n = 0; n < 4; n++)
                            if (member >= 0 && pc(n) == coop_campaign_object((unsigned)member)) destination = n;
                        int64_t item = dark_document(phase), parent = 0;
                        if (slot == actor && !sent && item && destination < 4) sent = coop_runtime_item(COOP_ITEM_GIVE, item, (uint8_t)member, -1);
                        ready = item && item_parent(item, &parent) && member >= 0 && parent == coop_campaign_object((unsigned)member) && coop_items_settled(items);
                    } else if (phase == 109 || phase == 114) {
                        int64_t target = phase == 109 ? scripted(1236, SAP_EXAMINE) : dark_grave(phase);
                        if (slot == actor && !sent && target && (phase == 109 || script_global_flag_get(2646))) sent = coop_runtime_world_examine(target);
                        ready = dark_ready(phase) && (slot != actor || (sent && coop_items_last_result(items, &status) != result_before && !coop_items_busy(items) && !status));
                    } else if (phase == 110 || phase == 115) {
                        if (slot == actor && !sent) {
                            int64_t prototype = sub_4685A0(BP_SHOVEL), shovel = prototype ? item_find_by_name(pc(slot), obj_field_int32_get(prototype, OBJ_F_NAME)) : 0;
                            TargetDescriptor target = { .obj = dark_grave(phase) };
                            if (shovel && target.obj) sent = coop_runtime_use_item(shovel, &target, false);
                        }
                        ready = dark_ready(phase);
                    } else if (phase == 111 || phase == 116) {
                        int64_t book = dark_document(phase), parent = 0;
                        if (book) item_parent(book, &parent);
                        if (slot == actor) {
                            if (!lock_step && parent && obj_field_int32_get(parent, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER) {
                                if (coop_runtime_item(COOP_LOOT_OPEN, parent, 0, -1)) lock_step = 1;
                            } else if (lock_step == 1 && parent && !coop_items_busy(items) && coop_items_loot_target(items) == parent) {
                                if (coop_runtime_item(COOP_LOOT_TAKE, book, 0, -1)) lock_step = 2;
                            } else if (lock_step == 2 && !coop_items_busy(items) && coop_items_loot_target(items)) {
                                if (coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1)) lock_step = 3;
                            }
                            if (coop_items_last_result(items, &status) != result_before && status) good = false;
                        }
                        ready = dark_ready(phase) && coop_items_settled(items) && (slot != actor || lock_step == 3);
                    } else if (phase == 119) {
                        int64_t guard = dark_guard();
                        if (host && (unsigned)((now - at) / 5000) > instance) {
                            instance = (now - at) / 5000;
                            fprintf(stderr, "ARCMP dark combat guard-hp=%d/%d flags=%x\n", guard ? object_hp_current(guard) : -1, guard ? object_hp_max(guard) : -1, guard ? obj_field_int32_get(guard, OBJ_F_FLAGS) : 0);
                            for (unsigned n = 0; n < 4; n++)
                                fprintf(stderr, "ARCMP dark combat member=%u hp=%d fatigue=%d active=%d attack=%d distance=%lld\n", n, obj_field_int32_get(pc(n), OBJ_F_HP_DAMAGE), obj_field_int32_get(pc(n), OBJ_F_CRITTER_FATIGUE_DAMAGE), critter_is_active(pc(n)), anim_is_current_goal_type(pc(n), AG_ATTACK, NULL), guard ? (long long)location_dist(obj_field_int64_get(pc(n), OBJ_F_LOCATION), obj_field_int64_get(guard, OBJ_F_LOCATION)) : -1);
                            if (framebuffer && !talked) {
                                location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                                gamelib_redraw();
                                iso_redraw();
                                tig_window_display();
                                good &= tig_video_screenshot_save("arcmp-progress-campaign-119.bmp") == TIG_OK;
                                talked = true;
                            }
                        }
                        if (now - measure_at > 250) {
                            if (guard && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc(slot))) {
                                if (combat_check_action_points(pc(slot), 5))
                                    coop_runtime_combat(COOP_COMBAT_ATTACK, COOP_ATTACK_FORCE, guard);
                                else if (combat_turn_based_is_active() && !anim_is_current_goal_type(pc(slot), AG_ATTACK, NULL))
                                    coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            } else if (!guard && combat_critter_is_combat_mode_active(pc(slot)))
                                coop_runtime_combat(COOP_COMBAT_STANCE, 0, 0);
                            else if (combat_turn_based_whos_turn_get() == pc(slot))
                                coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            measure_at = now;
                        }
                        ready = dark_ready(phase) && !combat_turn_based_is_active() && !combat_critter_is_combat_mode_active(pc(slot)) && coop_battle_settled(coop_gameplay_battle(game));
                    } else if (phase == 101 || phase == 103 || phase == 120 || phase == 122) {
                        int64_t target = scripted(phase == 101 ? 1755 : phase == 103 ? 1756
                                : phase == 120                                       ? 2465
                                                                                     : 2467,
                            SAP_USE);
                        if (slot == actor && !sent && target && (!evil_mode || critter_is_active(pc(actor)))) {
                            fprintf(stderr, "ARCMP dark exit phase=%u type=%d flags=%x distance=%lld gate=%d\n", phase, obj_field_int32_get(target, OBJ_F_TYPE), obj_field_int32_get(target, OBJ_F_FLAGS), (long long)object_dist(pc(slot), target), script_global_flag_get(2541));
                            fprintf(stderr, "ARCMP dark exit walking speed=%d burden=%d weight=%d capacity=%d\n", stat_level_get(pc(slot), STAT_SPEED), critter_encumbrance_level_get(pc(slot)), item_total_weight(pc(slot)), stat_level_get(pc(slot), STAT_CARRY_WEIGHT));
                            sent = coop_runtime_world_use(target);
                        }
                        if (slot == actor && sent && coop_items_last_result(items, &status) != result_before && !coop_items_busy(items)) {
                            fprintf(stderr, "ARCMP dark exit result phase=%u status=%u\n", phase, status);
                            if (status) good = false;
                        }
                        if (host && now - measure_at > 5000 && target) {
                            fprintf(stderr, "ARCMP dark exit pending phase=%u actor=%lld,%lld target=%lld,%lld distance=%lld busy=%d\n", phase, (long long)location_get_x(obj_field_int64_get(pc(actor), OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(pc(actor), OBJ_F_LOCATION)), (long long)location_get_x(obj_field_int64_get(target, OBJ_F_LOCATION)), (long long)location_get_y(obj_field_int64_get(target, OBJ_F_LOCATION)), (long long)object_dist(pc(actor), target), coop_items_busy(items));
                            measure_at = now;
                        }
                        ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == (phase == 101 ? 23 : phase == 120 ? 41
                                                                                                                               : 1);
                    } else if (phase == 123) {
                        if (!dark_marked && !coop_engine_documents()->count) {
                            char line[64];
                            snprintf(line, sizeof(line), "dark-ready:%u", phase);
                            good &= coop_session_chat(held, line);
                            dark_marked = true;
                        }
                        bool all = true;
                        for (unsigned n = 0; n < 4; n++)
                            all &= dark_acks[n] == phase;
                        if (slot == 2 && all && !sent && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) sent = coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0);
                        if (slot != 2 && !voted && control->phase == COOP_CONTROL_VOTING) {
                            good &= coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                            voted = true;
                        }
                        bool restored = coop_runtime_epoch() == baseline + 1 && script_story_state_get() == 15 && coop_engine_documents()->count == (unsigned)script_global_var_get(1884) && coop_engine_documents()->count >= 3;
                        ready = restored && dark_library_read(phase, &lock_step, framebuffer, &good) && script_story_state_get() == 15;
                    }
                    if (dark_save(phase)) {
                        unsigned before_save = phase == 122 ? 1 : 0;
                        if (coop_runtime_epoch() == baseline + before_save) {
                            if (ready && !dark_marked) {
                                char line[64];
                                snprintf(line, sizeof(line), "dark-ready:%u", phase);
                                good &= coop_session_chat(held, line);
                                dark_marked = true;
                            }
                            bool all = true;
                            for (unsigned n = 0; n < 4; n++)
                                all &= dark_acks[n] == phase;
                            if (host && all && !voted && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) voted = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                            ready = false;
                        } else
                            ready = coop_runtime_epoch() == baseline + before_save + 1;
                    }
                }
            }

            if (legacy_mode) {
                ready = false;
                if (evil_mode && phase == 239 && host && script_global_var_get(1813) != (int)coop_runtime_epoch()) {
                    unsigned step = evil_void_start + coop_runtime_epoch() - baseline;
                    if (!step || step > 4) good = false;
                    else for (unsigned owner = 0; owner < 4; owner++)
                        good &= map_current_map() == evil_void_landings[step - 1][0]
                            && location_dist(obj_field_int64_get(pc(owner), OBJ_F_LOCATION),
                                   location_make(evil_void_landings[step - 1][1], evil_void_landings[step - 1][2])) <= 10;
                    fprintf(stderr, "ARCMP original Void all-owner landing step=%u valid=%d\n", step, good);
                    if (good) good &= evil_void_prepare(step);
                }
                const LegacyStage* stage = legacy_stage(phase);
                if (evil_ending_mode && host && (stage->kind == LG_USE || stage->kind == LG_TILE)
                    && coop_runtime_epoch() == baseline + 1 && script_global_var_get(1822) != (int)phase) {
                    /* Validate before reacting to native arrival combat, which
                     * can legitimately move the humans off their landing. */
                    good &= evil_ending_landing();
                    if (good) script_global_var_set(1822, (int)phase);
                    fprintf(stderr, "ARCMP original evil all-owner landing phase=%u valid=%d\n", phase, good);
                }
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                if (script_global_var_get(1889) == (int)phase || coop_runtime_epoch() > baseline) {
                    unsigned actor = legacy_actor(phase);
                    if (evil_mode && phase == 224 && script_global_flag_get(2087) && !office_done && !view->active) {
                        office_done = true;
                        sent = walked = false;
                        dialog_revision = 0;
                        quest_edge_script = 0;
                    }
                    if (evil_mode && phase == 224 && (script_global_flag_get(2090) || script_global_flag_get(2038)) && !guard_done && !view->active) {
                        guard_done = true;
                        sent = false;
                        dialog_revision = 0;
                        quest_edge_script = 0;
                    }
                    int64_t threat = evil_mode && stage->kind != LG_FIGHT && !view->active ? evil_threat(slot) : 0;
                    int64_t route_door = evil_mode ? evil_route_door(phase) : 0;
                    if (route_door && !door_work) { sent = walked = false; door_work = true; }
                    if (evil_mode && host && script_global_var_get(1818) == (int)phase
                        && (!route_door || object_is_busted(route_door) || portal_is_open(route_door) || !object_locked_get(route_door))) {
                        fprintf(stderr, "ARCMP original locked door cleared phase=%u\n", phase);
                        good &= evil_prepare_hint(phase, scripted(evil_floor_gate(phase, coop_runtime_epoch() - baseline), SAP_USE));
                        route_door = 0; walked = false;
                    }
                    if (!route_door && door_work) { sent = walked = false; door_work = false; }
                    bool approaching = false;
                    unsigned chamber_epochs = phase == 215 ? 3u : phase == 232 ? 4u - evil_floor_start : 1u;
                    if (evil_mode && phase != 239 && coop_runtime_epoch() < baseline + (phase == 226 ? 2u : (phase == 215 || phase == 220 || phase == 224 || phase == 232) ? chamber_epochs
                                                                                                                                           : 1u)
                        && !view->active && !threat && script_global_var_get(1812) == (int)phase) {
                        int sap = stage->kind == LG_TALK ? SAP_DIALOG : stage->kind == LG_USE ? SAP_USE
                                                                                              : SAP_GET;
                        int gate = phase == 215 && coop_runtime_epoch() == baseline ? 2467
                            : phase == 215 && coop_runtime_epoch() == baseline + 1  ? 2465
                            : phase == 224                                          ? 2489
                                                                                    : 2424;
                        int64_t target = (phase == 215 || phase == 220 || phase == 224) ? scripted(gate, SAP_USE)
                            : stage->kind == LG_COLLECT ? legacy_item(phase) : scripted(stage->script, sap);
                        if (phase == 224) {
                            target = script_global_flag_get(2087) ? scripted(2489, SAP_USE) : scripted(1089, SAP_DIALOG);
                            if (host && script_global_var_get(1815) != (int)script_global_flag_get(2087))
                                good &= evil_prepare_hint(phase, target);
                        }
                        if (phase == 226) {
                            target = scripted(coop_runtime_epoch() == baseline ? 2490 : 2485, SAP_USE);
                            if (host && script_global_var_get(1813) != (int)coop_runtime_epoch())
                                good &= evil_prepare_hint(phase, target);
                        }
                        if (phase == 232) {
                            unsigned delta = coop_runtime_epoch() - baseline;
                            target = scripted(evil_floor_gate(phase, delta), SAP_USE);
                            if (host && script_global_var_get(1813) != (int)coop_runtime_epoch()) {
                                static const int landings[][2] = { {1882,1998}, {1836,1930}, {1561,1990} };
                                unsigned step = delta + evil_floor_start;
                                for (unsigned owner = 0; owner < 4; owner++)
                                    good &= step > 0 && step <= SDL_arraysize(landings)
                                        && location_dist(obj_field_int64_get(pc(owner), OBJ_F_LOCATION), location_make(landings[step-1][0], landings[step-1][1])) <= 10;
                                if (!good) fprintf(stderr, "ARCMP original dungeon detour landing failed delta=%u\n", delta);
                                if (good) good &= evil_prepare_hint(phase, target);
                                fprintf(stderr, "ARCMP original dungeon detour delta=%u next-script=%d all-owner-landing=%d\n", delta, evil_floor_gate(phase, delta), good);
                            }
                            if (passage_vote_epoch != coop_runtime_epoch()) {
                                sent = walked = false; passage_vote_epoch = coop_runtime_epoch();
                            }
                        }
                        int64_t parent = 0;
                        if (target && stage->kind == LG_COLLECT && item_parent(target, &parent) && parent) target = parent;
                        bool fresh = ((phase != 226 && phase != 232) || script_global_var_get(1813) == (int)coop_runtime_epoch())
                            && (phase != 224 || script_global_var_get(1815) == (int)script_global_flag_get(2087));
                        if (!fresh || !target || (obj_field_int32_get(target, OBJ_F_FLAGS) & OF_OFF) || object_dist(pc(actor), target) > 20) {
                            approaching = true;
                            int64_t loc = location_make(script_global_var_get(1810), script_global_var_get(1811));
                            if (slot == actor && fresh && !route_door && (!walked || now - motion_at > 1500)
                                && now - combat_walk_at > 1500) {
                                walked = evil_continue_walk(game, pc(actor), loc, 0); combat_walk_at = now;
                            }
                        }
                    }
                    if (threat) {
                        evil_share_door_weapon(game, slot, slot);
                        if (!coop_items_busy(items) && now - measure_at > 250 && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc(slot))) {
                            int64_t weapon = item_wield_get(pc(slot), ITEM_INV_LOC_WEAPON);
                            int range = weapon ? item_weapon_range(weapon, pc(slot)) : 5;
                            if (range < 5) range = 5;
                            if (!combat_turn_based_is_active() && object_dist(pc(slot), threat) > range) {
                                if (now - combat_walk_at > 1500) {
                                    evil_walk_to_enemy(slot, threat);
                                    combat_walk_at = now;
                                }
                            } else if (combat_check_action_points(pc(slot), 5))
                                coop_runtime_combat(COOP_COMBAT_ATTACK, COOP_ATTACK_FORCE, threat);
                            else if (combat_turn_based_is_active())
                                coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            measure_at = now;
                        }
                        /* Resume the interrupted ordinary approach after the
                         * original hostile actor has actually been defeated. */
                        sent = false;
                        walked = false;
                    } else if (evil_mode && phase == 224 && !office_done) {
                        /* Original office doors1162 require Alexander's real
                         * permission2087 before the chamber entrance2489 is
                         * accessible. The evil welcome391 grants it; ordinary
                         * scholar directions43 are an offered fallback. */
                        if (!approaching && slot == actor && !sent && !view->active) {
                            int64_t acolyte = scripted(1089, SAP_DIALOG);
                            if (acolyte) sent = coop_runtime_talk(acolyte);
                        }
                        if (view->active && view->chooser == slot && view->revision != dialog_revision) {
                            if (script_global_flag_get(2087)) {
                                dialog_revision = view->revision;
                                good &= coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                            } else {
                                int option = quest_option(view, 1089, 391);
                                if (option < 0) option = quest_option(view, 1089, 43);
                                if (option < 0) {
                                    /* The dialogue page can arrive before its
                                     * original flag update. At the goal page,
                                     * wait for the corresponding world state. */
                                    uint32_t reply = quest_text(view->reply);
                                    if (reply != quest_replies[391][0] && reply != quest_replies[391][1]
                                        && reply != quest_replies[43][0] && reply != quest_replies[43][1]) {
                                        fprintf(stderr, "ARCMP original office permission unmatched reply=%s\n", view->reply);
                                        good = false;
                                    }
                                } else {
                                    dialog_revision = view->revision;
                                    fprintf(stderr, "ARCMP original office permission choice=%s\n", view->options[option]);
                                    good &= coop_engine_dialogue_submit((unsigned)option);
                                }
                            }
                        }
                    } else if (evil_mode && phase == 224 && coop_runtime_epoch() >= baseline + chamber_epochs && !guard_done) {
                        /* Guard1176 gives the original invitation2090 for the
                         * accepted MinGorad quest. Door1177 otherwise refuses
                         * entry; repeated knocking only provokes the guard. */
                        if (slot == actor && !guard_sent && !view->active) {
                            int64_t guard = scripted(1176, SAP_DIALOG);
                            if (guard) guard_sent = coop_runtime_talk(guard);
                        }
                        if (view->active && view->chooser == slot && view->revision != dialog_revision) {
                            if (script_global_flag_get(2090) || script_global_flag_get(2038)) {
                                dialog_revision = view->revision;
                                good &= coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                            } else {
                                int option = quest_option(view, 1176, 20);
                                uint32_t reply = quest_text(view->reply);
                                if (option < 0 && view->option_count == 1
                                    && (reply == quest_replies[20][0] || reply == quest_replies[20][1])) option = 0;
                                if (option < 0) {
                                    fprintf(stderr, "ARCMP original guard invitation unmatched reply=%s\n", view->reply);
                                    good = false;
                                } else {
                                    dialog_revision = view->revision;
                                    fprintf(stderr, "ARCMP original guard invitation choice=%s\n", view->options[option]);
                                    good &= coop_engine_dialogue_submit((unsigned)option);
                                }
                            }
                        }
                    } else if (evil_mode && route_door && !view->active) {
                        bool tool_pending = evil_share_door_tool(game, slot, actor);
                        evil_yield_door(slot, actor, route_door);
                        if (now - measure_at > 250) evil_share_door_weapon(game, slot, actor);
                        if (host && (now - at) / 10000 != instance) {
                            instance = (now - at) / 10000;
                            AnimID id; AnimRunInfo* run = NULL;
                            if (sub_423300(pc(actor), &id)) anim_id_to_run_info(&id, &run);
                            fprintf(stderr, "ARCMP original door progress hp=%d weapon=%d root=%d goal=%d state=%d flags=%x id=%d range=%lld clock=%lld blocked=%d\n",
                                object_hp_current(route_door), item_wield_get(pc(actor), ITEM_INV_LOC_WEAPON) ? sub_49B290(item_wield_get(pc(actor), ITEM_INV_LOC_WEAPON)) : 0,
                                run ? run->goals[0].type : -1, run && run->cur_stack_data ? run->cur_stack_data->type : -1,
                                run ? run->current_state : -1, run ? run->flags : 0, run ? run->id.unique_id : -1,
                                run && run->cur_stack_data ? (long long)run->cur_stack_data->params[AGDATA_RANGE_DATA].data : -1LL,
                                (long long)sub_45A7C0().value, coop_runtime_blocks_simulation());
                            for (unsigned owner = 0; owner < 4; owner++) {
                                int64_t loc = obj_field_int64_get(pc(owner), OBJ_F_LOCATION);
                                fprintf(stderr, "ARCMP original door position owner=%u x=%lld y=%lld\n", owner,
                                    (long long)location_get_x(loc), (long long)location_get_y(loc));
                            }
                            if (instance == 1 && framebuffer) {
                                char file[80]; snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                                gamelib_redraw(); iso_redraw(); tig_window_display();
                                good &= tig_video_screenshot_save(file) == TIG_OK;
                            }
                        }
                        if (slot == actor && !tool_pending && !fault_wait_at && now - measure_at > 250) {
                            int64_t approach = location_make(script_global_var_get(1810), script_global_var_get(1811));
                            if (obj_field_int64_get(pc(actor), OBJ_F_LOCATION) != approach) {
                                if (!sent) sent = coop_runtime_halt(obj_field_int64_get(pc(actor), OBJ_F_LOCATION));
                                else if (!coop_items_busy(items) && (!walked || now - motion_at > 1500)) {
                                    if (now - combat_walk_at > 1500) {
                                        walked = evil_continue_walk(game, pc(actor), approach, route_door); combat_walk_at = now;
                                    }
                                }
                            } else if (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc(actor)) {
                                if (evil_pick_door(game, pc(actor), route_door) || evil_unlock_door(game, pc(actor), route_door)) {
                                    /* Character points, native requirements and casting are authoritative. */
                                } else if (combat_check_action_points(pc(actor), 5)) coop_runtime_combat(COOP_COMBAT_ATTACK, COOP_ATTACK_FORCE, route_door);
                                else if (!anim_is_current_goal_type(pc(actor), AG_ATTACK, NULL)) coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            }
                            measure_at = now;
                        } else if (slot != actor && combat_turn_based_is_active() && combat_turn_based_whos_turn_get() == pc(slot)
                            && now - measure_at > 250 && !anim_is_current_goal_type(pc(slot), AG_ATTACK, NULL)) {
                            coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0); measure_at = now;
                        }
                    } else if (evil_mode && phase == 239) {
                        unsigned step = evil_void_start + coop_runtime_epoch() - baseline;
                        if (passage_vote_epoch != coop_runtime_epoch()) {
                            sent = walked = guard_done = false; passage_vote_epoch = coop_runtime_epoch();
                            /* Dialogue revisions restart with each native map
                             * epoch; the preceding hand-in can have the same
                             * revision as the original castle welcome. */
                            dialog_revision = 0;
                            result_before = coop_items_last_result(items, &status);
                        }
                        if (view->active) {
                            if (view->chooser == slot && view->revision != dialog_revision && now - dialog_at > 250) {
                                int option = -1;
                                int64_t npc = obj_pool_perm_lookup(coop_object_id_to_engine(view->npc));
                                if (npc && script_number(npc, SAP_DIALOG) == 1111 && !script_global_flag_get(2718)) {
                                    for (unsigned i = 0; i < view->option_count; i++)
                                        if (strstr(view->options[i], "threats are pathetic") || strstr(view->options[i], "Step aside")) option = (int)i;
                                } else if (npc && script_number(npc, SAP_DIALOG) == 1111 && view->option_count) {
                                    option = 0; /* Original offered thanks/exit. */
                                }
                                good &= coop_engine_dialogue_submit(option >= 0 ? (unsigned)option : COOP_DIALOGUE_CANCEL);
                                dialog_revision = view->revision; dialog_at = now;
                            }
                        } else if (step < 4 && script_global_var_get(1813) == (int)coop_runtime_epoch()) {
                            if (step == 3 && script_global_flag_get(2718) && !guard_done) {
                                if (host) good &= evil_void_prepare(step);
                                guard_done = true; sent = walked = false;
                            }
                            if (slot == actor && now - measure_at > 1500 && !coop_items_busy(items)) {
                                if (step == 0 || step == 2) {
                                    int64_t loc = location_make(script_global_var_get(1810), script_global_var_get(1811));
                                    if (!walked || now - motion_at > 1500) walked = evil_continue_walk(game, pc(actor), loc, 0);
                                } else {
                                    bool guard = step == 3 && !script_global_flag_get(2718);
                                    int64_t target = scripted(step == 1 ? 2642 : guard ? 1111 : 2644, guard ? SAP_DIALOG : SAP_USE);
                                    if (target && object_dist(pc(actor), target) > 20) {
                                        if (!walked || now - motion_at > 1500)
                                            walked = evil_continue_walk(game, pc(actor), location_make(script_global_var_get(1810), script_global_var_get(1811)), 0);
                                    } else if (target && !sent) sent = guard ? coop_runtime_talk(target) : coop_runtime_world_use(target);
                                }
                                measure_at = now;
                            }
                            unsigned result_status;
                            uint32_t result = coop_items_last_result(items, &result_status);
                            if (slot == actor && result != result_before && result_status) {
                                fprintf(stderr, "ARCMP original Void use result step=%u status=%u\n", step, result_status);
                                if (result_status == COOP_ITEM_INTERRUPTED) sent = walked = false;
                                else good = false;
                            }
                            result_before = result;
                        }
                        ready = step == 4 && map_current_map() == 73 && script_global_flag_get(2718)
                            && !view->active && !coop_engine_dialogue_pending();
                    } else if (evil_mode && (stage->kind == LG_TALK || stage->kind == LG_TRAVEL || stage->kind == LG_COLLECT)
                        && combat_critter_is_combat_mode_active(pc(slot)) && !view->active) {
                        if (now - measure_at > 250) {
                            coop_runtime_combat(COOP_COMBAT_STANCE, 0, 0);
                            measure_at = now;
                        }
                    } else if (approaching) {
                        /* Ordinary Use/loot clicks admit targets within thirty
                         * tiles. Walk into range before submitting the action;
                         * hidden actors also retain their native heartbeat. */
                    } else if (stage->kind == LG_TRAVEL) {
                        if (host && !sent) {
                            good = legacy_transfer(phase);
                            sent = true;
                        }
                        ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == stage->map;
                    } else if (stage->kind == LG_COMPANION) {
                        int64_t npc = scripted(stage->script, SAP_DIALOG);
                        CoopFollowers* followers = coop_gameplay_followers(game);
                        CoopServices* services = coop_gameplay_services(game);
                        if (phase == 182 && slot == (actor + 1) % 4 && !traversal_refused
                            && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && npc && critter_is_active(npc)) {
                            if (!sent) sent = coop_runtime_follower_order(npc, COOP_FOLLOW_INVENTORY, 0, 0, false);
                            if (sent && !coop_followers_busy(followers) && coop_followers_last_result(followers, &status) != follower_before) {
                                good &= status == COOP_FOLLOW_OWNER;
                                good &= coop_session_chat(held, "companion-refused:182");
                            }
                        }
                        if (slot == actor && !lock_step && (phase != 182 || traversal_refused) && !view->active
                            && coop_dialogue_settled(coop_gameplay_dialogue(game)) && npc && critter_is_active(npc) && critter_is_active(pc(actor))) {
                            if (inven_ui_open(player_get_local_pc_obj(), npc, INVEN_UI_MODE_BARTER)) {
                                follower_before = coop_followers_last_result(followers, &status);
                                lock_step = 1;
                                dialog_at = now;
                            }
                        }
                        if (slot == actor && lock_step == 1 && !view->active && !coop_followers_busy(followers)
                            && coop_followers_last_result(followers, &status) != follower_before && now - dialog_at > 1000) {
                            good &= status == COOP_FOLLOW_BUSY || status == COOP_FOLLOW_UNAVAILABLE || status == COOP_FOLLOW_STALE;
                            if (!good) fprintf(stderr, "ARCMP traversal Inventory refused phase=%u status=%u\n", phase, status);
                            lock_step = 0;
                        }
                        if (view->active && view->mode == 3) {
                            good &= view->chooser == actor;
                            if (!talked && framebuffer) {
                                char file[80];
                                snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                                gamelib_redraw();
                                iso_redraw();
                                tig_window_display();
                                good &= tig_video_screenshot_save(file) == TIG_OK;
                            }
                            talked = true;
                            if (phase == 190 && host && !traversal_events) {
                                good &= traversal_queue_arrivals();
                                traversal_events = true;
                            }
                            if (slot == actor && phase == 182) {
                                int64_t item = legacy_named(2806), parent = 0;
                                if (item) item_parent(item, &parent);
                                if (lock_step == 1 && parent == npc) {
                                    good &= coop_runtime_service_quantity(item, 1, false);
                                    lock_step = 2;
                                }
                                int price;
                                if (lock_step == 2 && coop_services_quote(services, &price)) {
                                    good &= !price && coop_runtime_service_quantity(item, 1, true);
                                    lock_step = 3;
                                }
                                if (lock_step == 3 && parent == pc(actor) && coop_services_settled(services)) {
                                    coop_runtime_service_close();
                                    lock_step = 4;
                                }
                            }
                            if (slot == actor && phase == 190 && script_global_var_get(1851) == 2 && now - at > 2500 && lock_step == 1) {
                                coop_runtime_service_close();
                                lock_step = 4;
                            }
                        }
                        ready = talked && !view->active && legacy_ready(phase) && coop_dialogue_settled(coop_gameplay_dialogue(game))
                            && coop_followers_settled(followers) && coop_services_settled(services);
                    } else if (stage->kind == LG_DROP) {
                        int64_t item = legacy_item(phase);
                        if (slot == actor && !sent && item) sent = coop_runtime_item(COOP_ITEM_DROP, item, 0, -1);
                        ready = legacy_ready(phase) && coop_items_settled(items);
                    } else if (stage->kind == LG_TALK) {
                        bool chamber_gate = evil_mode && (phase == 215 || phase == 220 || phase == 224);
                        if (chamber_gate && coop_runtime_epoch() < baseline + chamber_epochs) {
                            if (passage_vote_epoch != coop_runtime_epoch()) {
                                gate_sent = false;
                                passage_vote_epoch = coop_runtime_epoch();
                            }
                            /* The retained input was captured immediately after
                             * first entry. Leave/re-enter through both original
                             * tree gates so Mingorad's first-heartbeat visibility
                             * gate runs normally before her chamber entrance. */
                            int gate = phase == 215 && coop_runtime_epoch() == baseline ? 2467
                                : phase == 215 && coop_runtime_epoch() == baseline + 1  ? 2465
                                : phase == 224                                          ? 2489
                                                                                        : 2424;
                            int64_t entrance = scripted(gate, SAP_USE);
                            if (slot == actor && !gate_sent && entrance) gate_sent = coop_runtime_world_use(entrance);
                        }
                        if (chamber_gate && gate_sent && coop_runtime_epoch() < baseline + chamber_epochs && !coop_items_busy(items)
                            && coop_items_last_result(items, &status) != result_before && status) {
                            fprintf(stderr, "ARCMP original chamber entrance refused phase=%u status=%u\n", phase, status);
                            good = false;
                        }
                        bool paid_passage = rescue_mode && (phase == 203 || phase == 204);
                        /* Tarant's original generator retries weekly. Its first
                         * onscreen attempt can precede the phase announcement.
                         * Use ordinary unanimous daily waits, with views panned
                         * away, rather than changing its deadline or spawn rate. */
                        unsigned waiting_epochs = phase == 204 ? (first_phase == 204 ? 0u : 8u) : 1u;
                        bool available = (!paid_passage || coop_runtime_epoch() >= baseline + waiting_epochs)
                            && (!chamber_gate || coop_runtime_epoch() >= baseline + chamber_epochs);
                        bool funded = phase != 203 || script_global_var_get(1828) == 1;
                        if (paid_passage && phase == 203 && coop_runtime_epoch() == baseline && !funded) {
                            unsigned donor = (actor + 1) % 4;
                            if (slot == donor && !spirit_talked && !coop_items_busy(items)) {
                                int64_t gold = obj_field_handle_get(pc(slot), OBJ_F_CRITTER_GOLD);
                                if (gold) spirit_talked = coop_runtime_item(COOP_ITEM_GIVE_AMOUNT, gold,
                                              (uint8_t)coop_campaign_slot_for_object(pc(actor)), script_global_var_get(1826));
                            }
                            if (host && !item_gold_get(pc(donor)) && item_gold_get(pc(actor)) == script_global_var_get(1825)
                                && coop_items_settled(items)) {
                                script_global_var_set(1828, 1);
                                fprintf(stderr, "ARCMP original coin transfer donor=%u recipient=%u amount=%d total=%d\n", donor, actor,
                                    script_global_var_get(1826), item_gold_get(pc(actor)));
                            }
                        }
                        if (paid_passage && funded && !available) {
                            if (passage_vote_epoch != coop_runtime_epoch()) {
                                voted = false;
                                passage_vote_epoch = coop_runtime_epoch();
                            }
                            if (slot == actor && !voted && control->phase == COOP_CONTROL_IDLE)
                                voted = coop_runtime_control(COOP_CONTROL_REST, coop_runtime_epoch() == baseline ? 5 : 4, 0, 0);
                            if (slot != actor && !voted && control->phase == COOP_CONTROL_VOTING) {
                                good &= coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                                voted = true;
                            }
                        }
                        if (paid_passage && available && !lock_step) {
                            if (host) {
                                DateTime time = sub_45A7C0();
                                script_global_var_set(1830, time.days);
                                script_global_var_set(1831, time.milliseconds);
                                script_global_var_set(1832, item_gold_get(pc(actor)));
                            }
                            lock_step = 1;
                        }
                        if (paid_passage && !view->active) {
                            int64_t target = branch_target(phase);
                            if (!target) {
                                int64_t generator = branch_generator(phase);
                                GeneratorInfo info;
                                if (generator) {
                                    monstergen_get(generator, &info);
                                    if (info.flags & GENERATOR_INACTIVE_ON_SCREEN) {
                                        /* Original port captains can only spawn offscreen.
                                         * Pan each owner's view; leave the generator's flags,
                                         * heartbeat, limits and actual spawn untouched. */
                                        int64_t loc = obj_field_int64_get(pc(slot), OBJ_F_LOCATION);
                                        location_origin_set(location_make(location_get_x(loc) + 40, location_get_y(loc) + 40));
                                    }
                                }
                            } else {
                                location_origin_set(obj_field_int64_get(pc(slot), OBJ_F_LOCATION));
                            }
                        }
                        bool accepted;
                        uint32_t result = coop_gameplay_last_result(game, &accepted);
                        if ((rescue_mode || evil_mode) && host && !view->active && now - measure_at > 5000) {
                            int64_t target = evil_mode ? scripted(stage->script, SAP_DIALOG) : branch_target(phase), loc = obj_field_int64_get(pc(actor), OBJ_F_LOCATION);
                            fprintf(stderr, "ARCMP alternate talk approach phase=%u actor=%lld,%lld distance=%lld target-flags=%x\n", phase,
                                (long long)location_get_x(loc), (long long)location_get_y(loc), target ? (long long)object_dist(pc(actor), target) : -1,
                                target ? obj_field_int32_get(target, OBJ_F_FLAGS) : 0);
                            measure_at = now;
                        }
                        if (sent && !voyage_seen && !view->active && result != instance && !accepted && now - dialog_at > 1000) sent = false;
                        if (available && slot == actor && !sent && !view->active) {
                            int64_t target = rescue_mode ? branch_target(phase) : scripted(stage->script, SAP_DIALOG);
                            if (!target && paid_passage && lock_step == 1) {
                                int64_t generator = branch_generator(phase);
                                if (generator) {
                                    GeneratorInfo info;
                                    monstergen_get(generator, &info);
                                    fprintf(stderr, "ARCMP original captain generator phase=%u flags=%x enabled=%d concurrent=%d/%d total=%d/%d hour=%d\n",
                                        phase, info.flags, info.enabled, info.cur_concurrent, info.max_concurrent, info.cur_total, info.max_total, datetime_current_hour());
                                    if (coop_runtime_move(obj_field_int64_get(generator, OBJ_F_LOCATION), true)) lock_step = 2;
                                }
                            }
                            if (target && !(obj_field_int32_get(target, OBJ_F_FLAGS) & OF_OFF)) {
                                Script tile;
                                int64_t arrival = location_make(96, 667);
                                sent = phase == 177 && legacy_tile(arrival, &tile) && tile.num == 3071
                                    ? coop_runtime_move(arrival, false)
                                    : coop_runtime_talk(target);
                                instance = result;
                                dialog_at = now;
                            }
                        }
                        if (view->active) {
                            voyage_seen = true;
                            if (framebuffer && !talked) {
                                char file[80];
                                snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                                gamelib_redraw();
                                iso_redraw();
                                tig_window_display();
                                good &= tig_video_screenshot_save(file) == TIG_OK;
                                talked = true;
                            }
                            if (finale_mode && phase == final_phase)
                                finale.goal_seen |= strstr(view->reply, "My road ends here") != NULL;
                            if (now - dialog_at > 180) {
                                good &= legacy_choose(game, slot, &dialog_revision);
                                dialog_at = now;
                            }
                        }
                        if (rescue_mode && phase == 194) {
                            unsigned count = coop_journal_count(coop_engine_journal());
                            for (unsigned page = 0; page < count && !voyage_seen; page++) {
                                CoopDialogueView floated;
                                char speaker[COOP_JOURNAL_SPEAKER];
                                if (coop_journal_page(coop_engine_journal(), page, &floated, speaker)
                                    && floated.mode == COOP_DIALOGUE_FLOAT && strstr(floated.reply, "not yours to control")) voyage_seen = true;
                            }
                        }
                        ready = voyage_seen && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending() && legacy_ready(phase)
                            && (phase != 204 || coop_runtime_epoch() == baseline + waiting_epochs)
                            && (!stage->map || (coop_runtime_epoch() == baseline + (paid_passage ? waiting_epochs + 1 : chamber_gate ? chamber_epochs
                                                                                                                                     : 1)
                                    && map_current_map() == stage->map));
                    } else if (stage->kind == LG_USE || stage->kind == LG_EXAMINE) {
                        int64_t target = rescue_mode ? branch_target(phase) : scripted(stage->script, stage->kind == LG_EXAMINE ? SAP_EXAMINE : SAP_USE);
                        if (evil_mode && phase == 232) target = scripted(evil_floor_gate(phase, coop_runtime_epoch() - baseline), SAP_USE);
                        if (evil_mode && phase == 226) {
                            target = scripted(coop_runtime_epoch() == baseline ? 2490 : 2485, SAP_USE);
                            if (passage_vote_epoch != coop_runtime_epoch()) {
                                sent = walked = false;
                                passage_vote_epoch = coop_runtime_epoch();
                            }
                        }
                        if (traversal_mode && host && target && now - measure_at > 3000) {
                            int64_t loc = obj_field_int64_get(pc(actor), OBJ_F_LOCATION), destination = obj_field_int64_get(target, OBJ_F_LOCATION);
                            fprintf(stderr, "ARCMP traversal approach phase=%u from=%lld,%lld target=%lld,%lld distance=%lld\n", phase,
                                (long long)location_get_x(loc), (long long)location_get_y(loc), (long long)location_get_x(destination), (long long)location_get_y(destination), (long long)location_dist(loc, destination));
                            measure_at = now;
                        }
                        if (slot == actor && !sent && target) {
                            if (evil_mode) result_before = coop_items_last_result(items, &status);
                            sent = stage->kind == LG_USE ? coop_runtime_world_use(target) : coop_runtime_world_examine(target);
                        }
                        if (rescue_mode && view->active && now - dialog_at > 180) {
                            good &= legacy_choose(game, slot, &dialog_revision);
                            dialog_at = now;
                        }
                        if (slot == actor && sent && coop_items_last_result(items, &status) != result_before && status) {
                            fprintf(stderr, "ARCMP legacy world result phase=%u status=%u\n", phase, status);
                            if (evil_mode && coop_items_settled(items) && (status == COOP_ITEM_INTERRUPTED
                                    || (status == COOP_ITEM_UNAVAILABLE && !critter_is_active(pc(actor)) && !critter_is_dead(pc(actor))))) {
                                /* A native combat/path goal can interrupt an
                                 * ordinary click. Retry that owned action;
                                 * its refusal never satisfies the route gate. */
                                sent = walked = false;
                                result_before = coop_items_last_result(items, &status);
                            } else good = false;
                        }
                        if (phase == 147) {
                            unsigned type;
                            int number;
                            bool variant;
                            if (slot == actor && written_ui_coop_state(&type, &number, &variant)) {
                                good &= legacy_read_close(phase, framebuffer);
                                talked = true;
                            }
                            ready = legacy_ready(phase) && (slot != actor || talked) && coop_items_settled(items);
                        } else if (stage->kind == LG_USE && phase != 194) {
                            ready = coop_runtime_epoch() == baseline + legacy_epochs(phase) && map_current_map() == stage->map;
                            if (evil_ending_mode && ready) ready = script_global_var_get(1822) == (int)phase;
                            /* Installed literal destinations distinguish a native
                             * same-map floor transfer from an unrelated save. */
                            if (ready && evil_mode && stage->x && stage->y)
                                for (unsigned i = 0; i < 4; i++)
                                    ready &= location_dist(obj_field_int64_get(pc(i), OBJ_F_LOCATION), location_make(stage->x, stage->y)) <= 10;
                        } else
                            ready = legacy_ready(phase) && (slot != actor || (sent && coop_items_last_result(items, &status) != result_before && !coop_items_busy(items) && !status));
                    } else if (stage->kind == LG_COLLECT || stage->kind == LG_LOOT) {
                        int64_t item = legacy_item(phase), parent = 0;
                        if (item) item_parent(item, &parent);
                        int64_t target = stage->kind == LG_LOOT ? scripted(stage->script, SAP_USE) : parent;
                        if (slot == actor) {
                            if (parent && coop_campaign_slot_for_object(parent) >= 0) {
                                if (coop_items_loot_target(items) && !coop_items_busy(items)) {
                                    if (coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1)) lock_step = 3;
                                } else if (coop_items_settled(items))
                                    lock_step = 3;
                            } else if (!lock_step && (item || stage->kind == LG_LOOT)) {
                                if (target && (stage->kind == LG_LOOT || (parent && obj_field_int32_get(parent, OBJ_F_TYPE) == OBJ_TYPE_CONTAINER))) {
                                    if (coop_runtime_item(COOP_LOOT_OPEN, target, 0, -1)) lock_step = 1;
                                } else if (!parent && coop_runtime_item(COOP_ITEM_PICKUP, item, 0, -1))
                                    lock_step = 2;
                            } else if (lock_step == 1 && item && !coop_items_busy(items) && coop_items_loot_target(items) == target) {
                                if (coop_runtime_item(COOP_LOOT_TAKE, item, 0, -1)) lock_step = 2;
                            }
                            if (coop_items_last_result(items, &status) != result_before && status) {
                                fprintf(stderr, "ARCMP legacy loot result phase=%u status=%u\n", phase, status);
                                result_before = coop_items_last_result(items, &status);
                                if (evil_mode && (status == COOP_ITEM_INTERRUPTED
                                        || (status == COOP_ITEM_UNAVAILABLE && !critter_is_active(pc(actor)) && !critter_is_dead(pc(actor)))))
                                    lock_step = 0;
                                else good = false;
                            }
                        }
                        ready = parent && coop_campaign_slot_for_object(parent) >= 0 && legacy_ready(phase) && coop_items_settled(items) && (slot != actor || lock_step == 3);
                    } else if (stage->kind == LG_EQUIP || stage->kind == LG_UNEQUIP) {
                        int64_t item = legacy_item(phase);
                        int destination = stage->kind == LG_UNEQUIP ? -1 : evil_ending_mode ? ITEM_INV_LOC_RING1
                            : traversal_mode ? ITEM_INV_LOC_HELMET : ITEM_INV_LOC_MEDALLION;
                        if (slot == actor && !sent && item) sent = coop_runtime_item(COOP_ITEM_MOVE, item, 0, destination);
                        ready = legacy_ready(phase) && coop_items_settled(items);
                    } else if (stage->kind == LG_READ) {
                        int64_t item = legacy_item(phase);
                        if (slot == actor && !sent && item) sent = coop_runtime_use_item(item, NULL, false);
                        unsigned type;
                        int number;
                        bool variant;
                        if (slot == actor && written_ui_coop_state(&type, &number, &variant)) {
                            good &= legacy_read_close(phase, framebuffer);
                            talked = true;
                        }
                        ready = (slot != actor || (sent && talked)) && coop_engine_documents()->count >= 5;
                    } else if (stage->kind == LG_RUNE || stage->kind == LG_TILE) {
                        int64_t loc = location_make(script_global_var_get(1864), script_global_var_get(1865));
                        if (evil_mode && phase == 240) {
                            unsigned floor = coop_runtime_epoch() - baseline;
                            if (passage_vote_epoch != coop_runtime_epoch()) {
                                sent = false;
                                passage_vote_epoch = coop_runtime_epoch();
                                if (host) fprintf(stderr, "ARCMP original castle floor=%u\n", floor);
                            }
                            if (floor == 0)
                                loc = location_make(653, 93);
                            else if (floor == 1)
                                loc = location_make(553, 106);
                        }
                        if ((combat_finale_mode || evil_mode) && host && now - measure_at > 5000) {
                            int64_t from = obj_field_int64_get(pc(actor), OBJ_F_LOCATION);
                            fprintf(stderr, "ARCMP native chamber approach actor=%lld,%lld target=%lld,%lld distance=%lld\n",
                                (long long)location_get_x(from), (long long)location_get_y(from), (long long)location_get_x(loc),
                                (long long)location_get_y(loc), (long long)location_dist(from, loc));
                            measure_at = now;
                        }
                        if (evil_ending_mode) {
                            if (slot == actor && coop_runtime_epoch() == baseline && (!sent || now - motion_at > 1500)
                                && now - combat_walk_at > 1500) {
                                sent = evil_continue_walk(game, pc(actor), loc, 0); combat_walk_at = now;
                            }
                        } else if (slot == actor && !sent) sent = coop_runtime_move(loc, combat_finale_mode || evil_mode);
                        ready = stage->kind == LG_TILE ? coop_runtime_epoch() == baseline + legacy_epochs(phase) && map_current_map() == stage->map : legacy_ready(phase);
                        if (evil_ending_mode && ready) ready = script_global_var_get(1822) == (int)phase;
                    } else if (stage->kind == LG_USE_ITEM) {
                        CoopMagic* magic = coop_gameplay_magic(game);
                        int64_t target = combat_finale_target(), item = legacy_named(5616);
                        if (!lock_step) {
                            instance = coop_magic_last_result(magic, &status);
                            lock_step = 1;
                        }
                        if (target && item && slot == actor && !sent && !coop_magic_busy(magic)
                            && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc(slot))) {
                            TargetDescriptor descriptor = { .obj = target };
                            sent = coop_runtime_use_item(item, &descriptor, false);
                        }
                        if (phase == 210 && slot == actor && sent && !coop_magic_busy(magic)
                            && coop_magic_last_result(magic, &status) != instance && lock_step == 1) {
                            good &= !status && target && sub_49B290(target) != 27369 && item_find_by_name(pc(actor), 5616);
                            if (good) good &= coop_session_chat(held, "device-wrong-form:210");
                            lock_step = 2;
                        }
                        if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() == pc(slot)
                            && (slot != actor || (sent && !coop_magic_busy(magic))) && now - measure_at > 250) {
                            coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            measure_at = now;
                        }
                        if (framebuffer && !talked && target) {
                            char file[80];
                            snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                            gamelib_redraw();
                            iso_redraw();
                            tig_window_display();
                            good &= tig_video_screenshot_save(file) == TIG_OK;
                            talked = true;
                        }
                        ready = phase == 210 && legacy_ready(phase) && coop_magic_settled(magic);
                    } else if (stage->kind == LG_FIGHT) {
                        int64_t enemy = evil_ending_mode ? evil_being(stage->goal) : evil_mode ? evil_town_enemy() : rescue_mode ? branch_target(phase)
                            : combat_finale_mode                                    ? combat_finale_target()
                                                                                    : legacy_enemy();
                        if (evil_ending_mode) {
                            if (phase == 253 && !script_global_flag_get(2091)) {
                                /* Warven's original death or the original
                                 * control's destruction releases the prison.
                                 * Arronax's own heartbeat removes his shield. */
                                enemy = scripted(2629, SAP_DYING);
                                if (!enemy || critter_is_dead(enemy)) enemy = scripted(1256, SAP_DYING);
                            }
                            int64_t threat = !view->active ? evil_threat(slot) : 0;
                            if (threat && (phase != 253 || script_global_flag_get(2091)
                                    || obj_field_int32_get(threat, OBJ_F_NAME) != 6436)) enemy = threat;
                        }
                        if (enemy && (critter_is_dead(enemy) || (combat_finale_mode && sub_49B290(enemy) == 27369))) enemy = 0;
                        if ((rescue_mode || combat_finale_mode || evil_mode) && !enemy && !sent) {
                            bool accepted;
                            instance = coop_gameplay_last_result(game, &accepted);
                            sent = coop_runtime_halt(obj_field_int64_get(pc(slot), OBJ_F_LOCATION));
                        }
                        if (evil_ending_mode && view->active) {
                            if (view->chooser == slot && now - dialog_at > 250 && view->revision != dialog_revision) {
                                good &= coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                                dialog_revision = view->revision; dialog_at = now;
                            }
                        } else if (!fault_wait_at && now - measure_at > 250) {
                            if (enemy && (!combat_turn_based_is_active() || combat_turn_based_whos_turn_get() == pc(slot))) {
                                if (evil_mode && !combat_turn_based_is_active() && object_dist(pc(slot), enemy) > 20) {
                                    /* Attack admits targets within thirty tiles.
                                     * Use ordinary walking between encounters,
                                     * then let the native attack goal approach. */
                                    if (now - combat_walk_at > 1500) {
                                        evil_walk_to_enemy(slot, enemy);
                                        combat_walk_at = now;
                                    }
                                } else if (combat_check_action_points(pc(slot), 5))
                                    coop_runtime_combat(COOP_COMBAT_ATTACK, COOP_ATTACK_FORCE, enemy);
                                else if (combat_turn_based_is_active() && !anim_is_current_goal_type(pc(slot), AG_ATTACK, NULL))
                                    coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            } else if (!enemy && combat_critter_is_combat_mode_active(pc(slot)))
                                coop_runtime_combat(COOP_COMBAT_STANCE, 0, 0);
                            else if (combat_turn_based_whos_turn_get() == pc(slot))
                                coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
                            measure_at = now;
                        }
                        if (framebuffer && !talked && now - at > 4000) {
                            location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                            char file[80];
                            snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                            gamelib_redraw();
                            iso_redraw();
                            tig_window_display();
                            good &= tig_video_screenshot_save(file) == TIG_OK;
                            talked = true;
                        }
                        ready = legacy_ready(phase) && (combat_finale_mode || (!combat_turn_based_is_active() && !combat_critter_is_combat_mode_active(pc(slot))))
                            && coop_battle_settled(coop_gameplay_battle(game));
                        if (rescue_mode || combat_finale_mode || evil_mode) {
                            bool accepted;
                            ready &= !enemy && sent && coop_gameplay_last_result(game, &accepted) != instance && accepted;
                        }
                    } else if (stage->kind == LG_VERIFY)
                        ready = legacy_ready(phase) && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game));
                    else if (stage->kind == LG_SAVE)
                        ready = legacy_ready(phase);
                    else if (stage->kind == LG_LOAD) {
                        if (slot == 2 && !sent && (rescue_mode ? script_global_var_get(1840) == 9000 : script_global_var_get(1878) == 9000) && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) sent = coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0);
                        if (slot != 2 && !voted && control->phase == COOP_CONTROL_VOTING) {
                            good &= coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                            voted = true;
                        }
                        ready = coop_runtime_epoch() == baseline + 1 && legacy_ready(phase);
                    }
                    if (stage->save) {
                        unsigned before_save = stage->kind == LG_TRAVEL ? 1 : 0;
                        if (coop_runtime_epoch() == baseline + before_save) {
                            if (rescue_mode && phase == 202 && host && ready && !dark_marked) {
                                if (!coop_dialogue_settled(coop_gameplay_dialogue(game)))
                                    ready = false;
                                else
                                    good &= legacy_ambient_scope();
                            }
                            if (ready && !dark_marked) {
                                char line[64];
                                snprintf(line, sizeof(line), "legacy-ready:%u", phase);
                                good &= coop_session_chat(held, line);
                                dark_marked = true;
                            }
                            bool all = true;
                            for (unsigned n = 0; n < 4; n++)
                                all &= dark_acks[n] == phase;
                            if (host && all && !voted && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) voted = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                            ready = false;
                        } else
                            ready = coop_runtime_epoch() == baseline + before_save + 1;
                    }
                }
            }

            if (forest_mode) {
                const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                const CoopControlView* control = coop_controls_current(coop_runtime_controls());
                unsigned actor = forest_actor(phase);
                if (forest_transfer(phase)) {
                    if (host && !sent) {
                        good = forest_do_transfer(phase);
                        sent = true;
                    }
                    ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == ((phase == 83 || phase == 87) ? 19 : 1);
                } else if (forest_dialog(phase)) {
                    bool available = phase != 76;
                    if (phase == 76) {
                        if (coop_runtime_epoch() == baseline && host && !sent && control->phase == COOP_CONTROL_IDLE && control->epoch == baseline)
                            sent = coop_runtime_control(COOP_CONTROL_REST, 5, 0, 0);
                        if (coop_runtime_epoch() == baseline && !host && !voted && control->phase == COOP_CONTROL_VOTING) {
                            good = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                            voted = true;
                        }
                        if (coop_runtime_epoch() == baseline + 1 && !lock_step && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) {
                            if (host) {
                                good = place_near(scripted(1076, SAP_DIALOG));
                                script_global_var_set(1919, 1);
                            }
                            lock_step = 1;
                            sent = voted = false;
                        }
                        available = coop_runtime_epoch() >= baseline + 1 && script_global_var_get(1919) == 1;
                    }
                    bool accepted;
                    uint32_t result = coop_gameplay_last_result(game, &accepted);
                    if (phase == 81 && voyage_seen && !view->active && !forest_ready(phase)
                        && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending() && lock_step < 2) {
                        sent = voyage_seen = false;
                        dialog_revision = 0;
                        lock_step++;
                    }
                    if (sent && !voyage_seen && !view->active && result != instance && !accepted && now - dialog_at > 1000) sent = false;
                    if (available && slot == actor && !sent && !view->active) {
                        int64_t npc = scripted(forest_dialog(phase), SAP_DIALOG);
                        if (npc) {
                            good = coop_runtime_talk(npc);
                            instance = result;
                            dialog_at = now;
                            sent = true;
                        }
                    }
                    if (available && view->active) {
                        voyage_seen = true;
                        if (host && phase == 78) script_global_var_set(1925, forest_member(view));
                        if (framebuffer && !talked) {
                            char path[80];
                            snprintf(path, sizeof(path), "arcmp-progress-campaign-%u.bmp", phase);
                            location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                            gamelib_redraw();
                            iso_redraw();
                            tig_window_display();
                            good &= tig_video_screenshot_save(path) == TIG_OK;
                            talked = true;
                        }
                        if (now - dialog_at > 160) {
                            good &= forest_choose(game, slot, &dialog_revision);
                            dialog_at = now;
                        }
                    }
                    ready = available && voyage_seen && !view->active && coop_dialogue_settled(coop_gameplay_dialogue(game)) && !coop_engine_dialogue_pending() && forest_ready(phase);
                } else if (phase == 74)
                    ready = script_global_var_get(1920) == 4 && script_global_var_get(1921) == script_global_var_get(1922) && script_global_var_get(1921) >= 4;
                else if (phase == 79) {
                    int64_t pelt = forest_pelt(), parent = 0;
                    if (pelt) item_parent(pelt, &parent);
                    if (slot == actor && !forest_ready(phase)) {
                        if (!lock_step && pelt) {
                            good = coop_runtime_item(parent ? COOP_LOOT_OPEN : COOP_ITEM_PICKUP, parent ? parent : pelt, 0, -1);
                            lock_step = 1;
                        } else if (lock_step == 1 && parent && !coop_items_busy(items) && coop_items_loot_target(items) == parent) {
                            good = coop_runtime_item(COOP_LOOT_TAKE, pelt, 0, -1);
                            lock_step = 2;
                        }
                    }
                    if (slot == actor && forest_ready(phase) && !coop_items_busy(items) && coop_items_loot_target(items) && lock_step < 3) {
                        good = coop_runtime_item(COOP_LOOT_CLOSE, 0, 0, -1);
                        lock_step = 3;
                    }
                    if (slot == actor && coop_items_last_result(items, &status) != result_before && status) good = false;
                    ready = forest_ready(phase) && coop_items_settled(items);
                } else if (phase == 82)
                    ready = script_global_var_get(1923) == 1 && forest_chart_hash() == (uint32_t)script_global_var_get(1928);
                else if (phase == 89 || phase == 91) {
                    if (slot == actor && !sent) {
                        int64_t here = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
                        map_precache_sectors(here);
                        int64_t target = scripted(phase == 89 ? 1410 : 1411, SAP_USE);
                        if (target) {
                            good = coop_runtime_world_use(target);
                            sent = true;
                        }
                    }
                    if (slot == actor && coop_items_last_result(items, &status) != result_before && status) good = false;
                    ready = coop_runtime_epoch() == baseline + 1 && map_current_map() == (phase == 89 ? 20 : 19);
                } else if (phase == 93) {
                    int64_t target = location_make(script_global_var_get(1926), script_global_var_get(1927));
                    if (slot == actor && !sent && script_global_var_get(1926) > 0) {
                        good = coop_runtime_move(target, false);
                        sent = true;
                    }
                    if (slot != actor && !lock_step) {
                        wmap_ui_open();
                        lock_step = 1;
                    }
                    ready = script_global_var_get(1926) > 0 && location_dist(obj_field_int64_get(pc(actor), OBJ_F_LOCATION), target) <= 1 && forest_destination_known();
                    if (ready && slot != actor) {
                        if (framebuffer) {
                            gamelib_redraw();
                            iso_redraw();
                            tig_window_display();
                            good &= tig_video_screenshot_save("arcmp-progress-campaign-93.bmp") == TIG_OK;
                        }
                        wmap_ui_close();
                    }
                } else if (phase == 94)
                    ready = true;
                else if (phase == 95)
                    ready = forest_chart_hash() != (uint32_t)script_global_var_get(1929);
                else if (phase == 96) {
                    if (slot == 2 && !sent && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) {
                        good = coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0);
                        sent = true;
                    }
                    if (slot != 2 && !voted && control->phase == COOP_CONTROL_VOTING) {
                        good = coop_runtime_control(COOP_CONTROL_VOTE, 1, control->proposal, 0);
                        voted = true;
                    }
                    ready = coop_runtime_epoch() == baseline + 1 && script_story_state_get() == 10 && forest_chart_hash() == (uint32_t)script_global_var_get(1929);
                }
                if (forest_save(phase) && ready) {
                    unsigned before_save = phase == 76 ? 1 : 0;
                    if (coop_runtime_epoch() == baseline + before_save) {
                        if (host && !voted && control->phase == COOP_CONTROL_IDLE && control->epoch == coop_runtime_epoch()) voted = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                        ready = false;
                    } else
                        ready = coop_runtime_epoch() == baseline + before_save + 1;
                }
            }

            if (host && ready && rescue_mode && phase == 204) {
                int paid = script_global_var_get(1832) - item_gold_get(pc(legacy_actor(phase)));
                good &= paid == 0 && location_dist(obj_field_int64_get(pc(0), OBJ_F_LOCATION), location_make(62183, 65692)) < 100;
                fprintf(stderr, "ARCMP original unavailable ship route refusal paid=%d party=%d\n", paid, script_global_var_get(1035));
            }
            if (host && ready && rescue_mode && (phase == 195 || phase == 203)) {
                uint64_t before = (uint64_t)script_global_var_get(1830) * COOP_DAY_MS + (unsigned)script_global_var_get(1831);
                uint64_t elapsed = campaign_clock() - before, expected = (phase == 195 ? UINT64_C(240000000) : UINT64_C(864000000));
                int paid = script_global_var_get(1832) - item_gold_get(pc(legacy_actor(phase)));
                good &= elapsed >= expected && elapsed < expected + UINT64_C(7200000) && (phase == 195 || paid > 0);
                if (phase != 195) {
                    unsigned group = coop_campaign_count();
                    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
                        group += obj_arrayfield_length_get(coop_campaign_object(i), OBJ_F_CRITTER_FOLLOWER_IDX);
                    if (group > 13) group = 13;
                    good &= script_global_var_get(1035) == (int)group && paid == (int)group * 100;
                }
                fprintf(stderr, "ARCMP original ship journey phase=%u elapsed=%llu expected=%llu paid=%d party=%d\n", phase,
                    (unsigned long long)elapsed, (unsigned long long)expected, paid, script_global_var_get(1035));
            }
            /* A recovery save is a fault boundary, never stage acceptance. */
            if (fault_wait_at) ready = false;
            if (host && ready) {
                unsigned entries, locks;
                script_cache_status(&entries, &locks);
                ready = entries <= 100 && !locks;
            }
            if (ready) {
                if (framebuffer && ((evil_mode && phase == 229 && slot == legacy_actor(phase)) || (traversal_mode && (phase == 184 || phase == 185 || phase == 189 || phase == 192)) || (rescue_mode && (phase == 194 || phase == 196 || phase == 199 || phase == 202 || phase == 205 || phase == 206)) || (combat_finale_mode && (phase == 208 || phase == 212)))) {
                    char file[80];
                    snprintf(file, sizeof(file), "arcmp-progress-campaign-%u.bmp", phase);
                    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                    gamelib_redraw();
                    iso_redraw();
                    tig_window_display();
                    good &= tig_video_screenshot_save(file) == TIG_OK;
                }
                if (framebuffer && (phase == 4 || phase == 7 || phase == 10 || phase == 12 || phase == 19 || phase == 20 || phase == 22 || (regional_mode && (phase == 24 || phase == 35 || phase == 40 || phase == 47)) || (quest_mode && (phase == 53 || phase == 56 || phase == 60)) || (voyage_mode && (phase == 65 || phase == 67 || phase == 69 || phase == 71 || phase == 73)) || (forest_mode && ((forest_dialog(phase) && !talked) || phase == 79 || phase == 96)))) {
                    if (phase >= 20 && (!quest_mode || forest_mode)) location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
                    if (phase == 20) {
                        int64_t ox, oy, center = 0;
                        location_origin_get(&ox, &oy);
                        bool centered = location_at(400, 250, &center);
                        fprintf(stderr, "ARCMP render owner=%u origin=%lld,%lld center=%d:%lld,%lld tile=%x colors=%x,%x hw=%d\n",
                            slot, (long long)ox, (long long)oy, centered, (long long)location_get_x(center), (long long)location_get_y(center),
                            tile_art_id_at(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION)), light_get_indoor_color(), light_get_outdoor_color(), tig_video_3d_check_initialized() == TIG_OK);
                    }
                    gamelib_redraw();
                    iso_redraw();
                    tig_window_display();
                    char path[64];
                    snprintf(path, sizeof(path), "arcmp-progress-campaign-%u.bmp", phase);
                    good &= tig_video_screenshot_save(path) == TIG_OK;
                }
                char line[64];
                snprintf(line, sizeof(line), "progress-done:%u", phase);
                good &= coop_session_chat(held, line);
                done = true;
                completed = phase;
            }
        }
        uint64_t deadline_at = at;
        if (evil_mode && phase == 217) {
            int count = script_global_var_get(1008);
            if (count != (int)motion_loc) {
                motion_loc = count;
                motion_at = now;
                if (host) fprintf(stderr, "ARCMP evil town native deaths=%d population=%d destroyed=%d\n", count,
                    script_global_var_get(1062), script_global_flag_get(2010));
            }
            if (motion_at > deadline_at) deadline_at = motion_at;
        }
        if (phase && (combat_finale_mode || evil_mode)
            && (legacy_stage(phase)->kind == LG_TILE || (evil_mode && (legacy_stage(phase)->kind == LG_USE || legacy_stage(phase)->kind == LG_COLLECT)))
            && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
            int64_t loc = obj_field_int64_get(pc(legacy_actor(phase)), OBJ_F_LOCATION);
            if (loc != motion_loc) {
                motion_loc = loc;
                motion_at = now;
            }
            if (motion_at > deadline_at) deadline_at = motion_at;
        }
        bool bounded_use = evil_mode && phase && legacy_stage(phase)->kind == LG_USE && now - at > (phase == 239 ? 240000u : 120000u);
        if (phase && !done && (fault_wait_at || bounded_use || now - deadline_at > (finale.media_time - phase_media) + (legacy_mode ? 120000u : dark_mode && phase == 119 ? 180000u
                                                                            : dark_mode && (phase == 101 || phase == 103 || phase == 120 || phase == 122)  ? 120000u
                                                                                                                                                           : 55000u))) {
            bool preserve = evil_mode;
            if (preserve && !fault_wait_at) {
                fault_wait_at = now;
                fault_epoch = coop_runtime_epoch();
            }
            if (preserve && host && !fault_requested && coop_runtime_epoch() == fault_epoch
                && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
                const CoopControlView* controls = coop_controls_current(coop_runtime_controls());
                if (controls->phase == COOP_CONTROL_IDLE) fault_requested = coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
                if (fault_requested) fprintf(stderr, "ARCMP preserving blocked combat phase=%u deaths=%d through public Save\n", phase, script_global_var_get(1008));
            }
            bool preserved = preserve && coop_runtime_epoch() > fault_epoch && coop_runtime_phase() == COOP_RUNTIME_PLAYING;
            /* Give the ordinary shared Save coordinator time to retain every
             * completed combat effect before the fixture reports its fault.
             * The archive remains a continuation input, not stage acceptance. */
            if (!preserve || preserved || now - fault_wait_at > 30000) {
                if ((phase == 13 || quest_mode) && game) {
                    const CoopDialogueView* view = coop_dialogue_current(coop_gameplay_dialogue(game));
                    fprintf(stderr, "ARCMP progress quest pending active=%d mode=%u chooser=%u reply=%s\n", view->active, view->mode, view->chooser, view->reply);
                }
                fprintf(stderr, "ARCMP progress timeout phase=%u epoch=%u status=%u counter=%d\n", phase, coop_runtime_epoch(), status, script_global_var_get(1983));
                if (legacy_mode) {
                    fprintf(stderr, "ARCMP legacy pending phase=%u marker=%d sent=%d read=%d step=%u story=%d docs=%u actor=%u\n", phase, script_global_var_get(1889), sent, talked, lock_step, script_story_state_get(), coop_engine_documents()->count, legacy_actor(phase));
                    if ((rescue_mode || combat_finale_mode || evil_mode) && legacy_stage(phase)->kind == LG_FIGHT) {
                        int64_t target = evil_mode ? evil_town_enemy() : rescue_mode ? branch_target(phase)
                                                                                     : combat_finale_target();
                        fprintf(stderr, "ARCMP native fight pending target=%lld name=%d hp=%d dead=%d helm=%d captain=%d turn=%d stance=%d distance=%lld flags=%x\n",
                            (long long)target, target ? obj_field_int32_get(target, OBJ_F_NAME) : -1, target ? object_hp_current(target) : -1,
                            target ? critter_is_dead(target) : -1, script_global_flag_get(2591), script_global_flag_get(2470),
                            combat_turn_based_is_active(), combat_critter_is_combat_mode_active(pc(slot)),
                            target ? (long long)object_dist(pc(slot), target) : -1, target ? obj_field_int32_get(target, OBJ_F_FLAGS) : 0);
                    }
                }
                if (dark_mode) {
                    int64_t item = dark_document(phase), parent = 0;
                    if (item) item_parent(item, &parent);
                    fprintf(stderr, "ARCMP dark pending slot=%u actor=%u sent=%d read=%d step=%u marker=%d docs=%u item=%d parent-member=%d\n", slot, dark_actor(phase), sent, talked, lock_step, script_global_var_get(1889), coop_engine_documents()->count, item ? obj_field_int32_get(item, OBJ_F_NAME) : -1, parent ? coop_campaign_slot_for_object(parent) : -1);
                }
                if (forest_mode && phase == 93) {
                    int64_t actual = obj_field_int64_get(pc(3), OBJ_F_LOCATION), target = location_make(script_global_var_get(1926), script_global_var_get(1927));
                    fprintf(stderr, "ARCMP exploration pending at=%lld,%lld target=%lld,%lld distance=%lld known=%d map=%d chart=%u flags=%x\n",
                        (long long)location_get_x(actual), (long long)location_get_y(actual), (long long)location_get_x(target), (long long)location_get_y(target),
                        (long long)location_dist(actual, target), forest_destination_known(), script_global_var_get(1924), forest_chart_hash(), obj_field_int32_get(pc(3), OBJ_F_FLAGS));
                }
                good = false;
            }
        }
        if (host && done && !finishing && game) {
            bool all = true;
            for (unsigned i = 0; i < 4; i++)
                all &= ack[i] == phase;
            if (all) {
                if (phase < final_phase)
                    good &= announce(phase + 1);
                else {
                    good &= coop_gameplay_finish(game);
                    finishing = true;
                }
            }
        }
        if (game && coop_gameplay_finished(game)) {
            if (!ended) ended = now;
            if (now - ended > 600) break;
        }
        if (host && combat_finale_mode && phase >= 209) {
            int64_t target = combat_finale_target();
            if (target) {
                int prototype = sub_49B290(target), bit = prototype == 27368 ? 2 : prototype == 27369 ? 4
                                                                                                      : 1;
                if (!(script_global_var_get(1843) & bit)) {
                    fprintf(stderr, "ARCMP combat finale native form prototype=%d name=%d hp=%d\n", prototype,
                        obj_field_int32_get(target, OBJ_F_NAME), object_hp_current(target));
                    script_global_var_set(1843, script_global_var_get(1843) | bit);
                }
            }
        }
        if (host && phase) quiet();
        if (finale_mode && phase == final_phase) {
            finale.story = (unsigned)script_story_state_get();
            finale.epoch = coop_runtime_epoch();
            finale.original_device |= combat_finale_mode ? script_global_var_get(1842) == 1 && (script_global_var_get(1843) & 6) == 6 : script_global_flag_get(2674);
            if (evil_ending_mode) finale.original_alliance |= script_global_flag_get(2632) && script_global_flag_get(2048)
                && script_global_flag_get(2043) && quest_state_get(pc(0), 1031) == QUEST_STATE_COMPLETED;
        }
        gamelib_ping();
        iso_redraw();
        tig_window_display();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT)
                good = false;
            else if (message.type == TIG_MESSAGE_REDRAW)
                gamelib_redraw();
            else
                intgame_process_event(&message);
        }
        SDL_Delay(2);
    }
    if (finale_mode) {
        unsigned expected_epoch = 1;
        for (unsigned stage = first_phase; stage <= final_phase; stage++)
            expected_epoch += legacy_epochs(stage);
        if (getenv("ARCMP_PROBE_MEDIA_DROP")) {
            expected_epoch++;
            good &= finale.fault_epoch && finale.epoch == finale.fault_epoch + 1
                && (coop_session_local_slot(held) != 2 || finale.media_drop);
        }
        good &= finale.good && finale.terminal && !finale.terminal_id && (evil_ending_mode || combat_finale_mode || finale.goal_seen)
            && (evil_ending_mode || finale.original_device)
            && (!evil_ending_mode || finale.original_alliance)
            && finale.story == (evil_ending_mode ? 20u : 22u) && finale.epoch == expected_epoch && finale.slides > 0
            && (!combat_finale_mode || finale.combat_movie)
            && phase == final_phase && completed == (first_phase == final_phase ? 0 : final_phase - 1)
            && coop_runtime_phase() == COOP_RUNTIME_OFF && mainmenu_ui_is_active() && !coop_campaign_active();
        FILE* report = fopen("arcmp-finale-result.json", "wb");
        if (!report)
            good = false;
        else {
            fprintf(report, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"story\":%u,\"original_device\":%s,\"original_alliance\":%s,\"original_farewell\":%s,\"shared_slides\":%u,\"all_ready_done\":%s,\"terminal_id\":%u,\"native_menu\":%s,\"party_reset\":%s}\n", good ? "true" : "false", first_phase, phase, finale.epoch, finale.story, finale.original_device ? "true" : "false", finale.original_alliance ? "true" : "false", finale.goal_seen ? "true" : "false", finale.slides, finale.terminal ? "true" : "false", finale.terminal_id, mainmenu_ui_is_active() ? "true" : "false", coop_campaign_active() ? "false" : "true");
            if (fclose(report)) good = false;
        }
        if (combat_finale_mode) {
            FILE* route = fopen("arcmp-combat-finale-result.json", "wb");
            if (!route)
                good = false;
            else {
                fprintf(route, "{\"verified\":%s,\"original_combat_movie\":%s,\"original_device_ending\":%s}\n", good ? "true" : "false", finale.combat_movie ? "true" : "false", finale.original_device ? "true" : "false");
                if (fclose(route)) good = false;
            }
        }
        coop_runtime_present_observer(NULL, NULL);
        fprintf(stderr, "ARCMP original finale probe %s stages=%u-%u slides=%u\n", good ? "PASS" : "FAIL", first_phase, phase, finale.slides);
        return good;
    }
    CoopGameplay* game = coop_runtime_gameplay();
    unsigned entries = 0, locks = 0;
    script_cache_status(&entries, &locks);
    unsigned expected_epoch = 1;
    for (unsigned stage = first_phase; stage <= final_phase; stage++)
        if ((quest_mode && (stage == 48 || stage == 49 || stage == 50 || stage == 51 || stage == 53 || stage == 54 || stage == 55 || stage == 57 || stage == 59)) || stair_script(stage) || stage == 8 || stage == 9 || stage == 18 || stage == 19 || (regional_mode && (stage == 29 || stage == 30 || stage >= 34))) expected_epoch++;
    if (voyage_mode) {
        expected_epoch = 1;
        for (unsigned stage = first_phase; stage <= final_phase; stage++)
            expected_epoch += voyage_epochs(stage);
    }
    if (legacy_mode) {
        expected_epoch = 1;
        for (unsigned stage = first_phase; stage <= final_phase; stage++)
            expected_epoch += legacy_epochs(stage);
    }
    if (dark_mode) {
        expected_epoch = 1;
        for (unsigned stage = first_phase; stage <= final_phase; stage++)
            expected_epoch += dark_epochs(stage);
    }
    if (forest_mode) {
        expected_epoch = 1;
        for (unsigned stage = first_phase; stage <= final_phase; stage++)
            expected_epoch += forest_epochs(stage);
    }
    good &= game && coop_gameplay_finished(game) && completed == final_phase && coop_runtime_epoch() == expected_epoch
        && script_global_flag_get(1022) && (first_phase > 13 || reveal_seen);
    FILE* report = fopen("arcmp-campaign-progress-result.json", "wb");
    if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"first_phase\":%u,\"epoch\":%u,\"script_cache_entries\":%u,\"script_locks\":%u,\"retail_choices\":%u,\"ring_owner_known\":%s,\"ring_reveal_seen\":%s,\"story\":%d,\"world_use_count\":%d}\n", good ? "true" : "false", completed, first_phase, coop_runtime_epoch(), entries, locks, retail_choices, script_global_flag_get(1022) ? "true" : "false", reveal_seen ? "true" : "false", script_story_state_get(), script_global_var_get(1983));
        if (fclose(report)) good = false;
    }
    if (quest_mode) {
        size_t size;
        uint8_t* bytes = coop_journal_encode(coop_engine_journal(), &size);
        FILE* transcript = bytes ? fopen("arcmp-campaign-journal.bin", "wb") : NULL;
        good &= transcript && fwrite(bytes, 1, size, transcript) == size;
        if (transcript && fclose(transcript)) good = false;
        free(bytes);
        FILE* results = fopen("arcmp-quests-result.json", "wb");
        if (!results)
            good = false;
        else {
            fprintf(results, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"queued_owners\":%u,\"bates_seen\":%s,\"pillar_seen\":%s,\"story\":%d,\"journal_pages\":%u}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), queued_owners, bates_seen ? "true" : "false", pillar_seen ? "true" : "false", script_story_state_get(), coop_journal_count(coop_engine_journal()));
            if (fclose(results)) good = false;
        }
    }
    if (voyage_mode) {
        FILE* results = fopen("arcmp-voyage-result.json", "wb");
        if (!results)
            good = false;
        else {
            fprintf(results, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"story\":%d,\"presentations\":%u,\"journal_pages\":%u}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), script_story_state_get(), coop_present_current(coop_runtime_presentation())->sequence, coop_journal_count(coop_engine_journal()));
            if (fclose(results)) good = false;
        }
    }

    if (rescue_mode) good &= script_story_state_get() == 8 && legacy_ready(206)
        && !script_global_flag_get(1031) && script_global_flag_get(2470) && !script_global_flag_get(2591);
    if (evil_mode) good &= legacy_ready(EVIL_PHASES);
    if (traversal_mode) good &= script_story_state_get() == 22 && script_global_flag_get(2095)
        && legacy_ready(192) && (first_phase > 182 || traversal_refused) && script_global_var_get(1850) == 12;
    if (legacy_mode) {
        size_t size;
        uint8_t* bytes = coop_documents_encode(coop_engine_documents(), &size);
        FILE* file = bytes ? fopen("arcmp-legacy-documents.bin", "wb") : NULL;
        good &= file && fwrite(bytes, 1, size, file) == size;
        if (file && fclose(file)) good = false;
        free(bytes);
        file = fopen("arcmp-legacy-result.json", "wb");
        if (!file)
            good = false;
        else {
            fprintf(file, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"dungeon_start\":%u,\"void_start\":%u,\"story\":%d,\"documents\":%u,\"journal_pages\":%u,\"tile_persistence\":%s}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), evil_floor_start, evil_void_start, script_story_state_get(), coop_engine_documents()->count, coop_journal_count(coop_engine_journal()), (evil_mode ? legacy_ready(EVIL_PHASES) : rescue_mode ? legacy_ready(206)
                                                                                                                                                                                                                                                                                                                                                                  : traversal_mode                             ? traversal_tile_persistence()
                                                                                                                                                                                                                                                                                                                                                                                                               : legacy_tile_oracle(false))
                    ? "true"
                    : "false");
            if (fclose(file)) good = false;
        }
    }
    if (dark_mode) {
        size_t size;
        uint8_t* bytes = coop_documents_encode(coop_engine_documents(), &size);
        FILE* file = bytes ? fopen("arcmp-dark-documents.bin", "wb") : NULL;
        good &= file && fwrite(bytes, 1, size, file) == size;
        if (file && fclose(file)) good = false;
        free(bytes);
        file = fopen("arcmp-dark-result.json", "wb");
        if (!file)
            good = false;
        else {
            fprintf(file, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"story\":%d,\"documents\":%u,\"journal_pages\":%u}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), script_story_state_get(), coop_engine_documents()->count, coop_journal_count(coop_engine_journal()));
            if (fclose(file)) good = false;
        }
    }

    if (forest_mode) {
        FILE* results = fopen("arcmp-forest-result.json", "wb");
        if (!results)
            good = false;
        else {
            fprintf(results, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"story\":%d,\"chart_hash\":%u,\"party\":%d,\"team\":%d,\"map_io\":%d,\"journal_pages\":%u}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), script_story_state_get(), forest_chart_hash(), script_global_var_get(1920), script_global_var_get(1921), script_global_var_get(1923), coop_journal_count(coop_engine_journal()));
            if (fclose(results)) good = false;
        }
    }

    if (regional_mode) {
        FILE* results = fopen("arcmp-regions-result.json", "wb");
        if (!results)
            good = false;
        else {
            fprintf(results, "{\"verified\":%s,\"first_phase\":%u,\"last_phase\":%u,\"epoch\":%u,\"pcm_phases\":%u,\"save_faults\":%u,\"retail_exits\":%u}\n", good ? "true" : "false", first_phase, completed, coop_runtime_epoch(), pcm_phases, save_faults, retail_exits);
            if (fclose(results)) good = false;
        }
        tig_sound_probe_enable(false);
    }
    if (evil_mode) coop_runtime_present_observer(NULL, NULL);
    fprintf(stderr, "ARCMP campaign progression probe %s phases=%u\n", good ? "PASS" : "FAIL", completed);
    return good;
}
