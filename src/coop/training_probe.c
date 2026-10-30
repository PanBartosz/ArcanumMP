#include "coop/training_probe.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/reaction.h"
#include "game/script.h"
#include "game/skill.h"
#include "game/stat.h"
#include "ui/dialog_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CASES = 24, ROUNDS = 6, TRAINER_TAG = 185307 };
typedef struct TrainingCase {
    unsigned phase, member, round;
    int gold, basic, tech, npc_gold, price;
} TrainingCase;
typedef struct TrainingValues { int gold, basic, tech, basic_points, tech_points; } TrainingValues;
static TrainingValues values(int64_t actor)
{
    return (TrainingValues) { item_gold_get(actor), basic_skill_training_get(actor, BASIC_SKILL_HEAL),
        tech_skill_training_get(actor, TECH_SKILL_FIREARMS), basic_skill_points_get(actor, BASIC_SKILL_HEAL),
        tech_skill_points_get(actor, TECH_SKILL_FIREARMS) };
}
static bool equal(TrainingValues a, TrainingValues b)
{
    return a.gold == b.gold && a.basic == b.basic && a.tech == b.tech
        && a.basic_points == b.basic_points && a.tech_points == b.tech_points;
}
static bool contains(const char* text, const char* part)
{
    size_t n = strlen(part);
    for (; *text; text++) if (!SDL_strncasecmp(text, part, n)) return true;
    return false;
}
static int64_t trainer(void)
{
    int64_t actor; int iterator;
    if (obj_inst_first(&actor, &iterator)) do {
        if (obj_field_int32_get(actor, OBJ_F_TYPE) == OBJ_TYPE_NPC
            && obj_field_int32_get(actor, OBJ_F_NPC_PAD_I_1) == TRAINER_TAG) return actor;
    } while (obj_inst_next(&actor, &iterator));
    return OBJ_HANDLE_NULL;
}
static bool prepare(void)
{
    /* Reuse the crash-site speaker's art, but run the unmodified Doc Roberts
     * dialogue at line 46. Heartbeats/quest scripts are outside this fixture. */
    int64_t actor, speaker = 0; int iterator;
    if (obj_inst_first(&actor, &iterator)) do {
        if (obj_field_int32_get(actor, OBJ_F_TYPE) != OBJ_TYPE_NPC) continue;
        Script script = { 0 };
        obj_arrayfield_script_get(actor, OBJ_F_SCRIPTS_IDX, SAP_DIALOG, &script);
        if (script.num == 1324) speaker = actor;
        object_flags_set(actor, OF_OFF);
    } while (obj_inst_next(&actor, &iterator));
    if (!speaker) return false;
    Script none = { 0 };
    for (int sap = 0; sap <= SAP_CRITICAL_MISS; sap++) obj_arrayfield_script_set(speaker, OBJ_F_SCRIPTS_IDX, sap, &none);
    critter_leader_set(speaker, OBJ_HANDLE_NULL);
    object_flags_unset(speaker, OF_OFF);
    obj_field_int32_set(speaker, OBJ_F_NPC_PAD_I_1, TRAINER_TAG);
    for (unsigned i = 0; i < 4; i++) {
        int64_t pc = coop_campaign_object(i);
        stat_base_set(pc, STAT_INTELLIGENCE, 20); stat_base_set(pc, STAT_PERCEPTION, 20);
        basic_skill_training_set(pc, BASIC_SKILL_HEAL, TRAINING_NONE);
        tech_skill_training_set(pc, TECH_SKILL_FIREARMS, TRAINING_NONE);
        basic_skill_points_set(pc, BASIC_SKILL_HEAL, 0);
        tech_skill_points_set(pc, TECH_SKILL_FIREARMS, 0);
        reaction_adj(speaker, pc, 60 - reaction_get(speaker, pc));
    }
    return true;
}
static bool start_case(CoopSession* session, unsigned phase, TrainingValues baseline[4], int* npc_before)
{
    unsigned member = (phase - 1) / ROUNDS, round = (phase - 1) % ROUNDS;
    int64_t pc = coop_campaign_object(member), npc = trainer();
    if (!npc) return false;
    if (round <= 2) {
        int gold = item_gold_get(pc), wanted = round == 1 ? 0 : 500;
        if (gold && !item_gold_transfer(pc, OBJ_HANDLE_NULL, gold, OBJ_HANDLE_NULL)) return false;
        if (wanted && !item_gold_transfer(OBJ_HANDLE_NULL, pc, wanted, OBJ_HANDLE_NULL)) return false;
        int wanted_points = round == 0 ? 0 : 2;
        if (basic_skill_points_set(pc, BASIC_SKILL_HEAL, wanted_points) != wanted_points) return false;
    }
    if (round == 5 && tech_skill_points_set(pc, TECH_SKILL_FIREARMS, 2) != 2) return false;
    int price = sub_4C1150(npc, pc, 100); if (price == 1) price = 2;
    int paid = round == 3 || round == 5 ? price : 0;
    if (price <= 0 || (paid && item_gold_get(pc) < paid)) return false;
    for (unsigned i = 0; i < 4; i++) baseline[i] = values(coop_campaign_object(i));
    *npc_before = item_gold_get(npc);
    char text[160];
    snprintf(text, sizeof(text), "training-case:%u:%u:%u:%d:%d:%d:%d:%d", phase, member, round,
        baseline[member].gold - paid, round >= 3 ? TRAINING_APPRENTICE : TRAINING_NONE,
        round == 5 ? TRAINING_APPRENTICE : TRAINING_NONE, *npc_before + paid, price);
    if (!coop_session_chat(session, text)) return false;
    dialog_ui_start_dialog(pc, npc, 1280, 0, 46);
    return true;
}
static bool capture(unsigned phase, bool quote, bool framebuffer)
{
    if (!framebuffer) return true;
    gamelib_redraw(); iso_redraw(); tig_window_display();
    char path[80]; snprintf(path, sizeof(path), "arcmp-training-%s-%u.bmp", quote ? "quote" : "outcome", phase);
    return tig_video_screenshot_save(path) == TIG_OK;
}
static void key(unsigned option)
{
    TigMessage input = { .type = TIG_MESSAGE_KEYBOARD };
    input.data.keyboard.scancode = SDL_SCANCODE_1 + (int)option;
    input.data.keyboard.key = SDLK_1 + (int)option;
    /* Two identical clicks in one UI poll must still mean one native action. */
    for (unsigned i = 0; i < 2; i++) {
        input.data.keyboard.pressed = true; tig_message_enqueue(&input);
        input.data.keyboard.pressed = false; tig_message_enqueue(&input);
    }
}
bool coop_training_probe(CoopSession* session, bool framebuffer, CoopGameplay** held_gameplay)
{
    dialog_ui_reset();
    CoopGameplay* game = *held_gameplay = coop_gameplay_create(session, 1);
    if (!game) return false;
    CoopDialogue* dialogue = coop_gameplay_dialogue(game);
    bool host = coop_session_is_host(session), initialized = false, finishing = false, guard = false, sent = false, done = false;
    uint64_t start = SDL_GetTicks(), running_at = 0, page_at = 0, finished_at = 0, chats = 0, quiet = 0;
    unsigned completed = 0, step = 0, inputs = 0, observer_denials = 0, stale_denials = 0, ack[4] = { 0 }, page_ack[4] = { 0 };
    unsigned action = 0; uint32_t action_revision = 0; bool action_pending = false;
    uint32_t recorded = 0;
    TrainingCase test = { 0 };
    TrainingValues baseline[4]; int npc_before = 0;
    bool good = true;
    FILE* transcript = fopen("arcmp-training-transcript.bin", "wb");
    if (!transcript) return false;
    while (good && SDL_GetTicks() - start < 180000) {
        uint64_t now = SDL_GetTicks();
        tig_ping(); coop_session_poll(session, now); coop_gameplay_poll(game, now);
        if (coop_gameplay_failed(game)) { fprintf(stderr, "ARCMP training network failed: %s\n", coop_gameplay_error(game)); good = false; break; }
        if (!coop_gameplay_running(game)) { SDL_Delay(2); continue; }
        if (!running_at) running_at = now;
        if (host && !initialized && now - running_at > 1200) {
            initialized = true; good = prepare() && start_case(session, 1, baseline, &npc_before);
        }
        for (; chats < coop_session_chat_count(session); chats++) {
            const CoopChat* chat = coop_session_chat_at(session, chats); TrainingCase next; unsigned phase;
            if (!chat->slot && sscanf(chat->text, "training-case:%u:%u:%u:%d:%d:%d:%d:%d", &next.phase, &next.member, &next.round,
                    &next.gold, &next.basic, &next.tech, &next.npc_gold, &next.price) == 8) {
                if (next.phase != test.phase + 1 || next.phase > CASES || next.member >= 4 || next.round >= ROUNDS) { good = false; break; }
                test = next; step = 0; sent = done = false; quiet = 0;
                fprintf(stderr, "ARCMP training case=%u member=%u round=%u expected-gold=%d price=%d\n", test.phase, test.member, test.round, test.gold, test.price);
            }
            if (chat->slot < 4 && sscanf(chat->text, "training-done:%u", &phase) == 1) ack[chat->slot] = phase;
            if (chat->slot < 4 && sscanf(chat->text, "training-page:%u", &phase) == 1) page_ack[chat->slot] = phase;
        }
        const CoopDialogueView* view = coop_dialogue_current(dialogue);
        if (view->revision > recorded) {
            size_t size = 0; uint8_t* bytes = coop_dialogue_view_encode(view, &size), length[4];
            coop_object_write32(length, (uint32_t)size);
            if (!bytes || fwrite(length, 1, 4, transcript) != 4 || fwrite(bytes, 1, size, transcript) != size) good = false;
            free(bytes); recorded = view->revision; page_at = now; sent = false;
            fprintf(stderr, "ARCMP training page=%u conversation=%u step=%u active=%u options=%u reply=%.120s\n", recorded, view->conversation, step, view->active, view->option_count, view->reply);
        }
        if (test.phase && !done && view->conversation == test.phase && view->active && !sent && now - page_at > 400) {
            int64_t pc = coop_campaign_object(test.member), npc = trainer();
            if (!npc) { SDL_Delay(2); continue; }
            bool terminal = step == (test.round == 0 || test.round == 4 ? 2u : 3u);
            if (terminal) {
                TrainingValues actual = values(pc);
                if (actual.gold != test.gold || actual.basic != test.basic || actual.tech != test.tech || item_gold_get(npc) != test.npc_gold) { SDL_Delay(2); continue; }
            }
            if (!host && !guard) {
                TrainingValues before = values(pc);
                if (coop_engine_dialogue_choose(NULL, 0) || !equal(before, values(pc))) { good = false; break; }
                guard = true;
            }
            if (coop_dialogue_choose(dialogue, view->conversation, view->revision - 1, 0)) { good = false; break; }
            stale_denials++;
            bool chooser = view->chooser == coop_session_local_slot(session);
            if (!chooser) {
                if (coop_dialogue_choose(dialogue, view->conversation, view->revision, 0)) { good = false; break; }
                observer_denials++;
            }
            unsigned option = 0;
            if (step == 0 || step == 1) {
                const char* needle = step == 0 ? "train" : test.round == 5 ? tech_skill_name(TECH_SKILL_FIREARMS) : basic_skill_name(BASIC_SKILL_HEAL);
                bool found = false;
                for (unsigned i = 0; i < view->option_count; i++) if (contains(view->options[i], needle)) { option = i; found = true; break; }
                if (!found) { fprintf(stderr, "ARCMP training missing option '%s'\n", needle); good = false; break; }
            } else if (terminal) {
                if (host) for (unsigned i = 0; i < 4; i++) {
                    TrainingValues expected = baseline[i];
                    if (i == test.member) { expected.gold = test.gold; expected.basic = test.basic; expected.tech = test.tech; }
                    if (!equal(expected, values(coop_campaign_object(i)))) good = false;
                }
                if (test.member == 1 || (!test.member && (test.round == 3 || test.round == 5)))
                    good = good && capture(test.phase, false, framebuffer);
            } else {
                if (view->option_count != 2 || step != 2) { good = false; break; }
                char price[32]; snprintf(price, sizeof(price), "%d", test.price);
                if (!strstr(view->reply, price)) { good = false; break; }
                if (test.member == 1 && (test.round == 3 || test.round == 5)) good = good && capture(test.phase, true, framebuffer);
                option = test.round == 2 ? 1 : 0;
            }
            if (chooser) { action = terminal ? COOP_DIALOGUE_CANCEL : option; action_pending = true; action_revision = view->revision; }
            if (!chooser) key(0);
            char text[64]; snprintf(text, sizeof(text), "training-page:%u", view->revision);
            good = good && coop_session_chat(session, text);
            sent = true; step++;
        }
        if (action_pending) {
            bool all = true; for (unsigned i = 0; i < 4; i++) all &= page_ack[i] == action_revision;
            if (all) {
                if (action == COOP_DIALOGUE_CANCEL) good = good && coop_dialogue_choose(dialogue, view->conversation, action_revision, action);
                else key(action);
                action_pending = false; inputs++;
            }
        }
        if (test.phase && !done && view->conversation == test.phase && !view->active && step == (test.round == 0 || test.round == 4 ? 3u : 4u)) {
            int64_t npc = trainer(), pc = coop_campaign_object(test.member);
            TrainingValues actual = values(pc);
            if (npc && actual.gold == test.gold && actual.basic == test.basic && actual.tech == test.tech && item_gold_get(npc) == test.npc_gold) {
                char text[64]; snprintf(text, sizeof(text), "training-done:%u", test.phase);
                good = good && coop_session_chat(session, text); done = true; completed = test.phase;
            }
        }
        if (host && done && !finishing && coop_dialogue_settled(dialogue)) {
            bool all = true; for (unsigned i = 0; i < 4; i++) all &= ack[i] == test.phase;
            if (all) {
                if (!quiet) quiet = now;
                if (now - quiet > 250) {
                    if (test.phase < CASES) good = good && start_case(session, test.phase + 1, baseline, &npc_before);
                    else { good = good && coop_gameplay_finish(game); finishing = true; }
                }
            }
        }
        TigMessage input; while (tig_message_dequeue(&input) == TIG_OK) if (input.type == TIG_MESSAGE_QUIT) good = false;
        if (coop_gameplay_finished(game)) {
            if (!finished_at) finished_at = now;
            if (now - finished_at > 600) break;
        }
        SDL_Delay(2);
    }
    if (fclose(transcript)) good = false;
    good = good && completed == CASES && inputs == 22 && observer_denials == 66 && stale_denials == 88
        && guard == !host && coop_gameplay_finished(game) && coop_dialogue_accepted(dialogue) == (host ? 88u : 0u);
    FILE* report = fopen("arcmp-training-result.json", "wb");
    if (!report) return false;
    fprintf(report, "{\"verified\":%s,\"cases\":%u,\"pages\":%u,\"inputs\":%u,\"observer_denials\":%u,\"stale_denials\":%u,\"replica_native_guard\":%s,\"accepted\":%u,\"players\":[",
        good ? "true" : "false", completed, recorded, inputs, observer_denials, stale_denials, guard ? "true" : "false", coop_dialogue_accepted(dialogue));
    for (unsigned i = 0; i < 4; i++) {
        TrainingValues v = values(coop_campaign_object(i));
        fprintf(report, "%s{\"gold\":%d,\"heal_training\":%d,\"firearms_training\":%d,\"heal_points\":%d,\"firearms_points\":%d}", i ? "," : "", v.gold, v.basic, v.tech, v.basic_points, v.tech_points);
    }
    fprintf(report, "]}\n"); if (fclose(report)) good = false;
    fprintf(stderr, "ARCMP retail training probe %s cases=%u pages=%u inputs=%u observers=%u stale=%u accepted=%u\n", good ? "PASS" : "FAIL", completed, recorded, inputs, observer_denials, stale_denials, coop_dialogue_accepted(dialogue));
    return good;
}
