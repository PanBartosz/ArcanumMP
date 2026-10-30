#include "coop/engine_character.h"
#include "coop/campaign.h"
#include "coop/draft.h"
#include "game/background.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/level.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/proto.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/tech.h"
#include "ui/skill_ui.h"
#include "ui/spell_ui.h"
#include "ui/tech_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/textedit_ui.h"
#include <stdio.h>
#include <string.h>

static bool verify(int64_t pc, unsigned* tested, unsigned* denied)
{
    CoopCharacter c, decoded;
    uint8_t first[COOP_CHARACTER_BYTES], second[COOP_CHARACTER_BYTES];
    if (!coop_engine_character_capture(pc, &c) || !coop_character_encode(&c, first)
        || !coop_character_decode(&decoded, first, sizeof(first)) || !coop_character_encode(&decoded, second)
        || memcmp(first, second, sizeof(first)) || !coop_engine_character_validate(&decoded)) {
        fprintf(stderr, "ARCMP character replay mismatch race=%d gender=%d background=%d points=%d\n",
            stat_base_get(pc, STAT_RACE), stat_base_get(pc, STAT_GENDER), background_get(pc), stat_base_get(pc, STAT_UNSPENT_POINTS));
        return false;
    }
    (*tested)++;
    c.unspent++;
    if (coop_engine_character_validate(&c)) return false;
    (*denied)++;
    return true;
}
bool coop_character_probe(void)
{
    static const int bodies[] = { TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_DWARF,
        TIG_ART_CRITTER_BODY_TYPE_ELF, TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_HALFLING,
        TIG_ART_CRITTER_BODY_TYPE_HALFLING, TIG_ART_CRITTER_BODY_TYPE_HUMAN, TIG_ART_CRITTER_BODY_TYPE_HALF_OGRE };
    int64_t local = player_get_local_pc_obj(), prototype = sub_4685A0(BP_GENERIC_PC);
    if (!local || !prototype) return false;
    unsigned tested = 0, denied = 0, backgrounds = 0, combinations = 0;
    bool good = true;
    for (int race = 0; good && race < 8; race++) for (int gender = 0; good && gender < 2; gender++) {
        if (gender == GENDER_FEMALE && race != RACE_HUMAN && race != RACE_ELF && race != RACE_HALF_ELF && race != RACE_HALF_ORC) continue;
        combinations++;
        int background = 0; bool more = true;
        for (unsigned b = 0; good && more && b < 256; b++) {
            int64_t pc = 0; obj_create_inst(prototype, 0, &pc); level_set(pc, 1);
            object_set_gender_and_race(pc, bodies[race], gender, race);
            if (!b) more = background_find_first(pc, &background);
            if (!more) { obj_deallocate(pc); break; }
            int portrait = 0;
            good = portrait_find_first(pc, &portrait);
            obj_field_int32_set(pc, OBJ_F_CRITTER_PORTRAIT, portrait);
            obj_field_string_set(pc, OBJ_F_PC_PLAYER_NAME, "Created traveller");
            if (background) background_set(pc, background, background_get_description(background));
            backgrounds++;
            good = good && verify(pc, &tested, &denied);
            /* Native UI operations supply the independent expected allocations.
             * Each background is checked both before and after spending points. */
            if (good) {
                unsigned choice = (unsigned)(race + gender + background) % 6;
                int points = stat_level_get(pc, STAT_UNSPENT_POINTS);
                if (choice == 0) skill_ui_inc_skill(pc, SKILL_MELEE);
                else if (choice == 1) skill_ui_inc_skill(pc, SKILL_PICK_LOCKS);
                else if (choice == 2) spell_ui_add(pc, 0);
                else if (choice == 3) tech_ui_inc_degree(pc, TECH_HERBOLOGY);
                else if (choice == 4 && points > 0) { object_hp_pts_set(pc, 1); stat_base_set(pc, STAT_UNSPENT_POINTS, points - 1); }
                else if (choice == 5 && points > 0) { critter_fatigue_pts_set(pc, 1); stat_base_set(pc, STAT_UNSPENT_POINTS, points - 1); }
                good = verify(pc, &tested, &denied);
                int value = stat_base_get(pc, STAT_INTELLIGENCE);
                int cost = stat_cost(value + 1);
                points = stat_level_get(pc, STAT_UNSPENT_POINTS);
                if (cost <= points && stat_level_get(pc, STAT_INTELLIGENCE) < stat_level_max(pc, STAT_INTELLIGENCE)
                    && stat_base_set(pc, STAT_INTELLIGENCE, value + 1) == value + 1)
                    stat_base_set(pc, STAT_UNSPENT_POINTS, points - cost);
                good = good && verify(pc, &tested, &denied);
            }
            background_clear(pc);
            more = background_find_next(pc, &background);
            obj_deallocate(pc);
        }
    }
    bool preflight = false;
    if (good) {
        int64_t pc = 0; obj_create_inst(prototype, 0, &pc); level_set(pc, 1);
        object_set_gender_and_race(pc, bodies[0], GENDER_MALE, RACE_HUMAN);
        int portrait = 0; portrait_find_first(pc, &portrait); obj_field_int32_set(pc, OBJ_F_CRITTER_PORTRAIT, portrait);
        obj_field_string_set(pc, OBJ_F_PC_PLAYER_NAME, "Preflight traveller");
        CoopCharacter c; good = coop_engine_character_capture(pc, &c);
        obj_deallocate(pc);
        CoopPlayer players[COOP_MAX_PLAYERS] = { 0 };
        for (unsigned i = 0; good && i < COOP_MAX_PLAYERS; i++) {
            players[i].connected = players[i].ready = players[i].custom = true;
            players[i].identity[0] = (uint8_t)(i + 1); players[i].prototype = BP_GENERIC_PC;
            players[i].build = c; strcpy(players[i].character, c.name);
        }
        good = good && coop_campaign_selections_valid(players);
        players[2].build.unspent++;
        preflight = good && !coop_campaign_selections_valid(players) && player_get_local_pc_obj() == local;
        good = good && preflight;
        c.portrait = INT32_MAX; good = good && !coop_engine_character_validate(&c);
        c.portrait = (uint32_t)portrait; c.background = 255; good = good && !coop_engine_character_validate(&c);
        c.background = 0; c.race = RACE_HALF_OGRE; c.gender = GENDER_FEMALE; good = good && !coop_engine_character_validate(&c);
    }
    good = good && player_get_local_pc_obj() == local && tested >= 100 && combinations == 12;
    FILE* file = fopen("arcmp-character-result.json", "wb");
    if (!file) return false;
    fprintf(file, "{\"verified\":%s,\"race_gender_combinations\":%u,\"legal_backgrounds\":%u,\"native_recipes_replayed\":%u,\"forged_point_balances_denied\":%u,\"invalid_party_preflight_rejected\":%s,\"local_pc_preserved\":%s}\n",
        good ? "true" : "false", combinations, backgrounds, tested, denied, preflight ? "true" : "false", player_get_local_pc_obj() == local ? "true" : "false");
    if (fclose(file)) good = false;
    fprintf(stderr, "ARCMP character replay %s recipes=%u backgrounds=%u denied=%u\n", good ? "PASS" : "FAIL", tested, backgrounds, denied);
    return good;
}

static CoopCharacter ui_character;
static bool ui_accepted;
static unsigned ui_cancelled;
static bool accept_ui(int64_t pc)
{
    if (!pc) { ui_cancelled++; return true; }
    ui_accepted = coop_engine_character_capture(pc, &ui_character) && coop_engine_character_validate(&ui_character);
    return ui_accepted;
}
static void pump_ui(void)
{
    uint64_t until = SDL_GetTicks() + 40;
    do {
        tig_ping();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
        }
        tig_window_display();
        SDL_Delay(2);
    } while (SDL_GetTicks() < until);
}
static void click_ui(int x, int y)
{
    TigMessage message = { .type = TIG_MESSAGE_MOUSE };
    message.data.mouse.x = x; message.data.mouse.y = y;
    message.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&message);
    pump_ui();
    message.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&message);
    pump_ui();
}
bool coop_character_ui_probe(unsigned player, bool framebuffer, CoopCharacter* character)
{
    ui_accepted = false; ui_cancelled = 0;
    if (!mainmenu_ui_is_active()) { mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU); mainmenu_ui_open(); }
    if (!mainmenu_ui_create_party_character(accept_ui)) return false;
    pump_ui();
    if (framebuffer) tig_video_screenshot_save("arcmp-character-initial.bmp");
    click_ui(95, 523);
    fprintf(stderr, "ARCMP character cancelled=%u active=%d type=%d\n", ui_cancelled, mainmenu_ui_party_creation_active(), mainmenu_ui_window_type_get());
    if (mainmenu_ui_party_creation_active() || ui_cancelled != 1 || !mainmenu_ui_create_party_character(accept_ui)) return false;
    pump_ui();
    click_ui(110, 232);
    if (!textedit_ui_is_focused()) { fprintf(stderr, "ARCMP character name field did not focus\n"); return false; }
    TigMessage text = { .type = TIG_MESSAGE_TEXT_INPUT };
    char name[32];
    snprintf(name, sizeof(name), "Created hero %u", player);
    text.data.text.text = name;
    tig_message_enqueue(&text); pump_ui();
    TigMessage key = { .type = TIG_MESSAGE_KEYBOARD };
    key.data.keyboard.scancode = SDL_SCANCODE_RETURN; key.data.keyboard.key = SDLK_RETURN; key.data.keyboard.pressed = true;
    tig_message_enqueue(&key); pump_ui();
    for (unsigned i = 0; i < player; i++) click_ui(263, 340);
    if (player & 1) click_ui(263, 390);
    if (framebuffer && tig_video_screenshot_save("arcmp-character-identity.bmp") != TIG_OK) return false;
    click_ui(704, 523);
    if (!charedit_is_created() || mainmenu_ui_window_type_get() != MM_WINDOW_CHAREDIT) {
        fprintf(stderr, "ARCMP character allocation screen did not open type=%d\n", mainmenu_ui_window_type_get()); return false;
    }
    int64_t pc = player_get_local_pc_obj();
    int before = object_hp_pts_get(pc), unspent = stat_base_get(pc, STAT_UNSPENT_POINTS);
    if (framebuffer) tig_video_screenshot_save("arcmp-character-allocation-before.bmp");
    click_ui(475, 194);
    if (object_hp_pts_get(pc) != before + 1 || stat_base_get(pc, STAT_UNSPENT_POINTS) != unspent - 1) {
        fprintf(stderr, "ARCMP character native HP allocation did not apply hp=%d points=%d\n", object_hp_pts_get(pc), stat_base_get(pc, STAT_UNSPENT_POINTS)); return false;
    }
    before = stat_base_get(pc, STAT_DEXTERITY);
    click_ui(234, 247);
    if (stat_base_get(pc, STAT_DEXTERITY) != before + 1) return false;
    if (framebuffer && tig_video_screenshot_save("arcmp-character-allocation.bmp") != TIG_OK) return false;
    click_ui(704, 523);
    bool good = ui_accepted && !mainmenu_ui_party_creation_active() && ui_cancelled == 1;
    uint8_t bytes[COOP_CHARACTER_BYTES];
    good = good && coop_character_encode(&ui_character, bytes);
    FILE* file = good ? fopen("arcmp-created-character.bin", "wb") : NULL;
    if (!file) good = false;
    if (file) { good = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && good; if (fclose(file)) good = false; }
    if (good && character) *character = ui_character;
    fprintf(stderr, "ARCMP character native UI %s player=%u race=%u background=%u hp=%u points=%u cancelled=%u\n",
        good ? "PASS" : "FAIL", player, ui_character.race, ui_character.background, ui_character.hp, ui_character.unspent, ui_cancelled);
    return good;
}

static uint8_t edit_content[32] = { 42 }, edit_owner[COOP_IDENTITY_SIZE];
static const char* edit_path = "arcmp-character-edit-fixture.draft";
static bool accept_edit(int64_t pc)
{
    if (!pc) return accept_ui(pc);
    CoopCharacter c;
    ui_accepted = coop_engine_character_capture(pc, &c) && coop_engine_character_validate(&c)
        && coop_draft_save(edit_path, edit_content, edit_owner, &c);
    if (ui_accepted) ui_character = c;
    return ui_accepted;
}
static bool same_character(const CoopCharacter* a, const CoopCharacter* b)
{
    uint8_t first[COOP_CHARACTER_BYTES], second[COOP_CHARACTER_BYTES];
    return coop_character_encode(a, first) && coop_character_encode(b, second) && !memcmp(first, second, sizeof(first));
}
static bool edit_failure(const char* stage)
{
    fprintf(stderr, "ARCMP character edit FAIL stage=%s\n", stage);
    tig_window_display(); tig_video_screenshot_save("arcmp-character-edit-failure.bmp");
    return false;
}
static bool edit_refund_and_reallocate(const CoopCharacter* source, CoopCharacter* result)
{
    click_ui(178, 247); click_ui(418, 194);
    CoopCharacter refunded, floor;
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &refunded)
        || refunded.stats[STAT_DEXTERITY] + 1 != source->stats[STAT_DEXTERITY]
        || refunded.hp + 1 != source->hp || refunded.unspent != source->unspent + 2) return false;
    click_ui(178, 247); click_ui(418, 194);
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &floor) || !same_character(&refunded, &floor)) return false;
    click_ui(234, 167);
    return coop_engine_character_capture(player_get_local_pc_obj(), result) && result->stats[STAT_STRENGTH] == refunded.stats[STAT_STRENGTH] + 1
        && result->unspent == refunded.unspent - 1 && coop_engine_character_validate(result);
}
bool coop_character_edit_probe(unsigned player, bool framebuffer, CoopCharacter* character)
{
    CoopCharacter original, editing, loaded;
    if (!coop_character_ui_probe(player, framebuffer, &original)) return false;
    memset(edit_owner, 0, sizeof(edit_owner)); edit_owner[0] = (uint8_t)(player + 1);
    if (!coop_draft_save(edit_path, edit_content, edit_owner, &original)) return edit_failure("initial-save");
    ui_accepted = false;
    if (!mainmenu_ui_edit_party_character(accept_edit, &original)) return edit_failure("open");
    pump_ui(); click_ui(704, 523);
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &editing) || !same_character(&original, &editing)) return edit_failure("restore");
    if (!edit_refund_and_reallocate(&original, &editing)) return edit_failure("refund-floor");
    mainmenu_ui_cancel_party_character();
    if (ui_accepted || coop_draft_load(edit_path, edit_content, edit_owner, &loaded) != COOP_DRAFT_OK || !same_character(&loaded, &original)) return edit_failure("cancel-preserves-file");

    if (!mainmenu_ui_edit_party_character(accept_edit, &original)) return edit_failure("reopen");
    pump_ui();
    click_ui(110, 232);
    char name[32]; snprintf(name, sizeof(name), "Edited hero %u", player);
    TigMessage text = { .type = TIG_MESSAGE_TEXT_INPUT }; text.data.text.text = name; tig_message_enqueue(&text); pump_ui();
    TigMessage key = { .type = TIG_MESSAGE_KEYBOARD };
    key.data.keyboard.scancode = SDL_SCANCODE_RETURN; key.data.keyboard.key = SDLK_RETURN; key.data.keyboard.pressed = true;
    tig_message_enqueue(&key); pump_ui();
    click_ui(704, 523);
    if (!edit_refund_and_reallocate(&original, &editing)) return edit_failure("second-refund");
    click_ui(95, 523); // Return to identity without discarding allocations.
    if (mainmenu_ui_window_type_get() != MM_WINDOW_NEW_CHAR) return edit_failure("back-to-identity");
    click_ui(259, 132); // Portrait-only changes preserve point allocations.
    int portrait = portrait_get(player_get_local_pc_obj());
    click_ui(704, 523);
    editing.portrait = (uint32_t)portrait;
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &loaded) || !same_character(&editing, &loaded)) return edit_failure("back-preserves-points");
    if (framebuffer && tig_video_screenshot_save("arcmp-character-edit.bmp") != TIG_OK) return edit_failure("capture");
    click_ui(704, 523);
    if (!ui_accepted || mainmenu_ui_party_creation_active() || coop_draft_load(edit_path, edit_content, edit_owner, &loaded) != COOP_DRAFT_OK
        || !same_character(&editing, &loaded)) return edit_failure("accept-replaces-file");
    CoopCharacter accepted = loaded;
    ui_accepted = false;
    if (!mainmenu_ui_edit_party_character(accept_edit, &accepted)) return edit_failure("identity-reset-open");
    pump_ui(); click_ui(263, 390);
    CoopCharacter reset;
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &reset) || reset.background == accepted.background) return edit_failure("identity-change");
    click_ui(704, 523);
    if (!coop_engine_character_capture(player_get_local_pc_obj(), &loaded) || !same_character(&reset, &loaded)) return edit_failure("identity-reset-points");
    if (framebuffer) tig_video_screenshot_save("arcmp-character-identity-reset.bmp");
    mainmenu_ui_cancel_party_character();
    if (ui_accepted || coop_draft_load(edit_path, edit_content, edit_owner, &loaded) != COOP_DRAFT_OK || !same_character(&loaded, &accepted)) return edit_failure("reset-cancel-preserves-file");
    uint8_t bytes[COOP_CHARACTER_BYTES];
    if (!coop_character_encode(&accepted, bytes)) return false;
    FILE* file = fopen("arcmp-edited-character.bin", "wb");
    if (!file) return false;
    bool good = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes); if (fclose(file)) good = false;
    file = fopen("arcmp-character-edit-result.json", "wb");
    if (!file) return false;
    fprintf(file, "{\"verified\":%s,\"refunded_existing_points\":true,\"baseline_floor_preserved\":true,\"back_preserves_allocations\":true,\"portrait_and_name_edits_preserve_allocations\":true,\"identity_change_resets_points\":true,\"cancel_preserves_saved_draft\":true,\"accept_replaces_saved_draft\":true}\n", good ? "true" : "false");
    if (fclose(file)) good = false;
    if (good && character) *character = accepted;
    fprintf(stderr, "ARCMP character edit %s player=%u name=%s points=%u\n", good ? "PASS" : "FAIL", player, accepted.name, accepted.unspent);
    return good;
}
