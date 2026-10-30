#include "coop/engine_teleport.h"
#include "coop/authority.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_magic.h"
#include "coop/runtime.h"
#include "game/antiteleport.h"
#include "game/area.h"
#include "game/critter.h"
#include "game/item.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/player.h"
#include "ui/intgame.h"
#include "ui/wmap_ui.h"
#include <stdio.h>
#include <string.h>
static CoopMagicChoice choices[COOP_MAGIC_CHOICES];
static uint64_t serial, shown, submitted;
static uint32_t submitted_sequence;
static const char path[] = "Save\\Current\\arcmp-spell-destinations.bin";
void coop_engine_teleport_reset(void)
{
    wmap_ui_coop_dismiss_spell();
    memset(choices, 0, sizeof(choices)); serial = shown = submitted = 0; submitted_sequence = 0;
}
bool coop_engine_teleport_pending(int64_t actor)
{
    int member = coop_campaign_slot_for_object(actor);
    return member >= 0 && member < COOP_MAGIC_CHOICES && choices[member].token != 0;
}
bool coop_engine_teleport_any(void)
{
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) if (choices[i].token) return true;
    return false;
}
bool coop_engine_teleport_select(int64_t source, int spell)
{
    if (!coop_runtime_gameplay()) return false;
    if (!coop_authority_can_simulate()) return true;
    int64_t actor = source;
    if (obj_type_is_item(obj_field_int32_get(source, OBJ_F_TYPE)) && !item_parent(source, &actor)) return true;
    int member = coop_campaign_slot_for_object(actor);
    if (member < 0 || member >= COOP_MAGIC_CHOICES) return false;
    if (spell < 0 || spell >= MT_SPELL_COUNT || serial == UINT64_MAX) return true;
    if (choices[member].token) return true;
    CoopMagicChoice choice = { .token = ++serial, .map = (uint32_t)map_current_map(), .spell = (uint16_t)spell, .charge = source == actor };
    memcpy(choice.actor, obj_get_id(actor).d.g.data, 16);
    choices[member] = choice;
    fprintf(stderr, "ARCMP native spell destination pending member=%d spell=%d token=%llu deferred-charge=%u\n", member, spell,
        (unsigned long long)choice.token, choice.charge);
    return true;
}
void coop_engine_teleport_capture(CoopMagicChoice result[COOP_MAGIC_CHOICES]) { memcpy(result, choices, sizeof(choices)); }
unsigned coop_engine_teleport_apply(int64_t actor, const CoopMagicRequest* request)
{
    if (!coop_authority_can_simulate() || !actor || !request || request->operation != COOP_MAGIC_DESTINATION) return COOP_MAGIC_UNAVAILABLE;
    int member = coop_campaign_slot_for_object(actor);
    if (member < 0 || member >= COOP_MAGIC_CHOICES) return COOP_MAGIC_OWNERSHIP;
    CoopMagicChoice* choice = &choices[member];
    if (!choice->token || choice->token != request->token || choice->spell != request->spell || choice->map != (uint32_t)map_current_map()
        || memcmp(choice->actor, request->actor, 16) || memcmp(request->source.bytes + 1, request->actor, 16)) return COOP_MAGIC_STALE;
    if (!request->x) { *choice = (CoopMagicChoice) { 0 }; return COOP_MAGIC_OK; }
    if (!critter_is_active(actor) || request->x >= (uint32_t)area_count() || !area_is_known(actor, (int)request->x)) return COOP_MAGIC_TARGET;
    int64_t location = area_get_location((int)request->x);
    if (!location || !antiteleport_check(actor, 0) || !antiteleport_check(actor, location)) return COOP_MAGIC_TARGET;
    if (choice->charge && !magictech_can_charge_spell_fatigue(actor, choice->spell)) return COOP_MAGIC_FATIGUE;
    if (!coop_runtime_commit_spell_destination(actor, location, choice->spell, choice->charge != 0)) return COOP_MAGIC_UNAVAILABLE;
    fprintf(stderr, "ARCMP native spell destination committed member=%d spell=%u token=%llu area=%u charged=%u\n", member,
        choice->spell, (unsigned long long)choice->token, request->x, choice->charge);
    /* The transition is committed; its checkpoint carries the spent cost and
     * no remaining choice. The original cast/item Use is never replayed. */
    if (area_get_last_known_area(actor) == (int)request->x) area_reset_last_known_area(actor);
    char* area_name = area_get_name((int)request->x);
    char line[COOP_CHAT_SIZE];
    snprintf(line, sizeof(line), "%s chose Teleportation to %s.", coop_campaign_member((unsigned)member)->character, area_name ? area_name : "a known location");
    coop_session_chat(coop_runtime_session(), line);
    *choice = (CoopMagicChoice) { 0 };
    return COOP_MAGIC_OK;
}
bool coop_engine_teleport_save(void)
{
    if (!coop_campaign_active()) return !tig_file_exists(path, NULL) || tig_file_remove(path) == 0;
    CoopMagicView view = { 0 }; memcpy(view.choices, choices, sizeof(choices));
    uint8_t bytes[COOP_MAGIC_VIEW_HEADER + 4];
    if (!coop_magic_view_encode(&view, bytes, COOP_MAGIC_VIEW_HEADER)) return false;
    coop_object_write32(bytes + COOP_MAGIC_VIEW_HEADER, coop_snapshot_crc(bytes, COOP_MAGIC_VIEW_HEADER));
    TigFile* file = tig_file_fopen(path, "wb");
    bool good = file && tig_file_fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (file && tig_file_fclose(file)) good = false;
    return good;
}
bool coop_engine_teleport_load(void)
{
    coop_engine_teleport_reset();
    if (!coop_campaign_active() || !tig_file_exists(path, NULL)) return true;
    TigFile* file = tig_file_fopen(path, "rb");
    if (!file) return false;
    uint8_t bytes[COOP_MAGIC_VIEW_HEADER + 4]; CoopMagicView view;
    bool good = tig_file_fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && tig_file_fgetc(file) == EOF && !tig_file_ferror(file);
    if (tig_file_fclose(file)) good = false;
    if (!good || coop_object_read32(bytes + COOP_MAGIC_VIEW_HEADER) != coop_snapshot_crc(bytes, COOP_MAGIC_VIEW_HEADER)
        || !coop_magic_view_decode(bytes, COOP_MAGIC_VIEW_HEADER, &view) || view.count) return false;
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) if (view.choices[i].token) {
        const CoopCampaignMember* member = coop_campaign_member(i);
        if (!member || memcmp(member->object_id.d.g.data, view.choices[i].actor, 16) || view.choices[i].map != (uint32_t)map_current_map()
            || !magictech_is_enabled(view.choices[i].spell)) return false;
        if (view.choices[i].token > serial) serial = view.choices[i].token;
    }
    memcpy(choices, view.choices, sizeof(choices));
    return true;
}
static const CoopMagicChoice* local_choice(void)
{
    int64_t pc = player_get_local_pc_obj();
    const CoopMagicView* view = coop_engine_magic_view();
    if (!pc || !view) return NULL;
    for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) if (view->choices[i].token && !memcmp(view->choices[i].actor, obj_get_id(pc).d.g.data, 16)) return &view->choices[i];
    return NULL;
}
void coop_engine_teleport_ui_closed(uint64_t token)
{
    if (shown == token) shown = 0;
}
bool coop_engine_teleport_submit(uint64_t token, unsigned area)
{
    const CoopMagicChoice* choice = local_choice();
    if (!choice || choice->token != token || area >= 4096 || coop_runtime_phase() != COOP_RUNTIME_PLAYING) return false;
    CoopMagic* magic = coop_gameplay_magic(coop_runtime_gameplay());
    if (coop_magic_busy(magic)) return false;
    CoopMagicRequest request = { .operation = COOP_MAGIC_DESTINATION, .token = token, .spell = choice->spell, .x = area };
    request.source.bytes[0] = 2; memcpy(request.source.bytes + 1, choice->actor, 16);
    unsigned status;
    submitted_sequence = coop_magic_last_result(magic, &status);
    if (!coop_runtime_magic_choice(&request)) return false;
    submitted = token;
    return true;
}
void coop_engine_teleport_ui_tick(void)
{
    if (coop_runtime_phase() != COOP_RUNTIME_PLAYING || !coop_runtime_gameplay()) return;
    if (coop_authority_can_simulate()) for (unsigned i = 0; i < COOP_MAGIC_CHOICES; i++) if (choices[i].token) {
        int64_t actor = coop_campaign_object(i);
        if (!actor || !critter_is_active(actor) || choices[i].map != (uint32_t)map_current_map()
            || (obj_field_int32_get(actor, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) choices[i] = (CoopMagicChoice){0};
    }
    const CoopMagicChoice* choice = local_choice();
    if (!choice) { wmap_ui_coop_dismiss_spell(); shown = submitted = 0; return; }
    CoopMagic* magic = coop_gameplay_magic(coop_runtime_gameplay());
    if (submitted == choice->token) {
        unsigned status;
        if (coop_magic_busy(magic) || coop_magic_last_result(magic, &status) == submitted_sequence) return;
        if (!status) return;
        submitted = shown = 0;
    }
    if (coop_magic_busy(magic)) return;
    CoopDialogue* dialogue = coop_gameplay_dialogue(coop_runtime_gameplay());
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    if (choice->token == shown || coop_engine_dialogue_pending() || coop_dialogue_current(dialogue)->active || control->paused || control->phase != COOP_CONTROL_IDLE) return;
    if (wmap_ui_coop_select(choice->token, player_get_local_pc_obj(), choice->spell)) shown = choice->token;
}
