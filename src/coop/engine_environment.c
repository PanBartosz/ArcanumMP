#include "coop/engine_environment.h"
#include "coop/engine_options.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/object_state.h"
#include "game/gsound.h"
#include "game/light.h"
#include "game/light_scheme.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/sector.h"
#include "tig/color.h"
#include <stdio.h>
#include <string.h>
static const char save_path[] = "Save\\Current\\arcmp-environment.bin";
static uint32_t rgb(tig_color_t color)
{
    return (uint32_t)tig_color_get_red(color) << 16 | (uint32_t)tig_color_get_green(color) << 8 | tig_color_get_blue(color);
}
static tig_color_t native_color(uint32_t color)
{
    return tig_color_make((color >> 16) & 255, (color >> 8) & 255, color & 255);
}
bool coop_engine_environment_capture(CoopEnvironmentState* s)
{
    memset(s, 0, sizeof(*s));
    s->light_scheme = light_scheme_get();
    s->light_default = light_scheme_get_map_default();
    s->hour = light_scheme_get_hour();
    s->indoor = rgb(light_get_indoor_color());
    s->outdoor = rgb(light_get_outdoor_color());
    gsound_campaign_state_get(s);
    coop_engine_options_capture(s);
    return coop_environment_valid(s);
}
bool coop_engine_environment_matches_content(const CoopEnvironmentState* s)
{
    return coop_environment_valid(s) && light_scheme_campaign_valid(s->light_scheme, s->light_default)
        && gsound_campaign_state_valid(s);
}
bool coop_engine_environment_install(const CoopEnvironmentState* s)
{
    if (!coop_engine_environment_matches_content(s)) return false;
    if (!light_scheme_campaign_restore(s->light_scheme, s->light_default, s->hour)) return false;
    tig_color_t indoor = native_color(s->indoor), outdoor = native_color(s->outdoor);
    if (light_get_indoor_color() != indoor || light_get_outdoor_color() != outdoor) light_set_colors(indoor, outdoor);
    if (!gsound_campaign_state_restore(s)) return false;
    coop_engine_options_install(s);
    return true;
}

bool coop_engine_environment_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(save_path, NULL) || tig_file_remove(save_path) == 0;
    CoopEnvironmentState state;
    uint8_t bytes[COOP_ENVIRONMENT_BYTES + 4];
    if (!coop_engine_environment_capture(&state) || !coop_environment_encode(&state, bytes)) return false;
    coop_object_write32(bytes + COOP_ENVIRONMENT_BYTES, coop_snapshot_crc(bytes, COOP_ENVIRONMENT_BYTES));
    TigFile* file = tig_file_fopen(save_path, "wb");
    if (!file) return false;
    bool good = tig_file_fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (tig_file_fclose(file) != 0) good = false;
    return good;
}

bool coop_engine_environment_load(void)
{
    /* Older cooperative and ordinary native saves have no sidecar. Their
     * original lighting/sound modules provide the compatibility fallback. */
    if (!coop_campaign_active() || !tig_file_exists(save_path, NULL)) return true;
    uint8_t bytes[COOP_ENVIRONMENT_BYTES + 4];
    CoopEnvironmentState state;
    TigFile* file = tig_file_fopen(save_path, "rb");
    if (!file) return false;
    int length = tig_file_filelength(file);
    bool good = (length == COOP_ENVIRONMENT_BYTES + 4 || length == COOP_ENVIRONMENT_V2_BYTES + 4 || length == COOP_ENVIRONMENT_LEGACY_BYTES + 4)
        && tig_file_fread(bytes, 1, (size_t)length, file) == (size_t)length
        && tig_file_fgetc(file) == EOF && !tig_file_ferror(file);
    if (tig_file_fclose(file) != 0) good = false;
    return good && coop_object_read32(bytes + length - 4) == coop_snapshot_crc(bytes, (size_t)length - 4)
        && coop_environment_decode(bytes, (size_t)length - 4, &state) && coop_engine_environment_install(&state);
}

void coop_engine_environment_view_begin(void)
{
    if (!coop_campaign_active()) return;
    int64_t pc = player_get_local_pc_obj();
    if (!pc || coop_campaign_slot_for_object(pc) < 0) return;
    int64_t id = sector_id_from_loc(obj_field_int64_get(pc, OBJ_F_LOCATION));
    Sector* sector;
    if (!sector_lock(id, &sector)) return;
    int scheme = sector->light_scheme;
    sector_unlock(id);
    if (!scheme) scheme = light_scheme_get_map_default();
    tig_color_t indoor = light_get_indoor_color(), outdoor = light_get_outdoor_color();
    /* Preserve script-specified custom colors when the owned region uses the
     * campaign scheme. A different region derives its own colors at that hour. */
    if (scheme != light_scheme_get() && !light_scheme_view_colors(scheme, light_scheme_get_hour(), &indoor, &outdoor)) return;
    light_view_begin(indoor, outdoor);
}
void coop_engine_environment_view_end(void)
{
    light_view_end();
}
