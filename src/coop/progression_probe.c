#include "coop/progression_probe.h"
#include "coop/campaign.h"
#include "coop/engine_progression.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/level.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/player.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/skill_ui.h"
#include "ui/spell_ui.h"
#include "ui/tech_ui.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 41 };
struct CoopProgressionProbe {
    CoopSession* session; CoopGameplay* game;
    int member; bool host, framebuffer, started, sent, done, replayed, complete, closed, replica_guard;
    unsigned phase, completed, ack[4], replays, denials, earned[4], panel_step;
    uint64_t chats, at, quiet;
    uint32_t expected, old_lease;
    int before, points_before, damage_before;
    unsigned level_notice_base;
    CoopProgressionRequest request;
};
static void click(int x, int y)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE };
    m.data.mouse.x = x; m.data.mouse.y = y + 41; m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
static bool announce(CoopProgressionProbe* p, unsigned phase)
{
    if (phase == 1) for (unsigned i = 0; i < 4; i++) {
        int64_t actor = coop_campaign_object(i);
        int level = stat_base_get(actor, STAT_LEVEL), points = stat_base_get(actor, STAT_UNSPENT_POINTS);
        auto_level_scheme_set(actor, 0);
        int xp = stat_base_get(actor, STAT_EXPERIENCE_POINTS) + level_experience_points_to_next_level(actor);
        stat_base_set(actor, STAT_EXPERIENCE_POINTS, xp);
        if (stat_base_get(actor, STAT_LEVEL) != level + 1 || stat_base_get(actor, STAT_UNSPENT_POINTS) <= points) return false;
        p->earned[i] = (unsigned)(stat_base_get(actor, STAT_UNSPENT_POINTS) - points);
        stat_base_set(actor, STAT_INTELLIGENCE, 20); stat_base_set(actor, STAT_WILLPOWER, 20);
        basic_skill_points_set(actor, BASIC_SKILL_PICK_POCKET, 0); tech_skill_points_set(actor, TECH_SKILL_REPAIR, 0);
        while (spell_college_level_get(actor, 14) > 0) if (!spell_remove(actor, 14 * 5 + spell_college_level_get(actor, 14) - 1)) return false;
        obj_field_int32_set(actor, OBJ_F_HP_DAMAGE, 3);
    }
    if (phase == 17 || phase == 18 || phase == 20) for (unsigned i = 0; i < 4; i++)
        stat_base_set(coop_campaign_object(i), STAT_UNSPENT_POINTS, phase == 17 ? 0 : 5);
    if (phase == 29) for (unsigned i = 0; i < 4; i++)
        if (spell_min_level(14 * 5 + 2) <= stat_level_get(coop_campaign_object(i), STAT_LEVEL)) return false;
    char message[64]; snprintf(message, sizeof(message), "progress-phase:%u", phase);
    return coop_session_chat(p->session, message);
}
CoopProgressionProbe* coop_progression_probe_create(CoopSession* session, CoopGameplay* game, int member, bool framebuffer)
{
    CoopProgressionProbe* p = calloc(1, sizeof(*p));
    if (p) { p->session = session; p->game = game; p->member = member; p->host = coop_session_is_host(session); p->framebuffer = framebuffer; p->level_notice_base = charedit_coop_level_notifications(); }
    return p;
}
static void selection(unsigned phase, CoopProgressionKind* kind, unsigned* index, bool* increase)
{
    *kind = COOP_PROGRESS_STAT; *index = STAT_BEAUTY; *increase = true;
    if (phase == 3 || phase == 16 || phase == 22) *increase = false;
    if (phase == 40) *increase = false;
    if (phase == 4 || phase == 5) { *kind = COOP_PROGRESS_HP; *index = 0; *increase = phase == 4; }
    if (phase == 6 || phase == 7) { *kind = COOP_PROGRESS_FATIGUE; *index = 0; *increase = phase == 6; }
    if (phase == 8 || phase == 9 || phase == 10 || phase == 11) { *kind = COOP_PROGRESS_SKILL; *index = phase <= 9 ? BASIC_SKILL_PICK_POCKET : BASIC_SKILL_COUNT + TECH_SKILL_REPAIR; *increase = !(phase & 1); }
    if (phase == 12 || phase == 13) { *kind = COOP_PROGRESS_COLLEGE; *index = 14; *increase = phase == 12; }
    if (phase == 14 || phase == 15) { *kind = COOP_PROGRESS_DISCIPLINE; *index = 0; *increase = phase == 14; }
    if (phase == 24) *index = STAT_DEXTERITY;
    if (phase >= 27 && phase <= 31) { *kind = COOP_PROGRESS_COLLEGE; *index = 14; *increase = phase <= 29; }
    if (phase >= 34 && phase <= 36) {
        *kind = COOP_PROGRESS_SCHEME; *index = 0;
        if (phase != 36) {
            for (unsigned scheme = 1; scheme < 200; scheme++) {
                bool valid = auto_level_scheme_name((int)scheme) && auto_level_scheme_rule((int)scheme);
                if (valid == (phase == 34)) { *index = scheme; break; }
            }
        }
    }
}
int coop_progression_probe_poll(CoopProgressionProbe* p, uint64_t now, bool start)
{
    CoopProgression* service = coop_gameplay_progression(p->game);
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* chat = coop_session_chat_at(p->session, p->chats); unsigned phase;
        if (!chat->slot && sscanf(chat->text, "progress-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > PHASES) return -1;
            p->phase = phase; p->at = now; p->quiet = 0; p->sent = p->done = p->replayed = p->closed = false;
            p->panel_step = 0;
            unsigned status; p->expected = coop_progression_last_result(service, &status) + 1;
            fprintf(stderr, "ARCMP progress phase=%u member=%d expected=%u\n", phase, p->member, p->expected);
        }
        if (chat->slot < 4 && sscanf(chat->text, "progress-done:%u", &phase) == 1) p->ack[chat->slot] = phase;
    }
    if (p->host && start && !p->started) { p->started = true; if (!announce(p, 1)) return -1; }
    if (!p->phase || p->complete) return p->complete;
    if (now - p->at > 20000) { fprintf(stderr, "ARCMP progress timeout phase=%u member=%d sent=%d done=%d lease=%u busy=%d\n", p->phase, p->member, p->sent, p->done, coop_progression_lease(service), coop_progression_busy(service)); return -1; }
    int64_t actor = player_get_local_pc_obj();
    CoopProgressionKind kind; unsigned index; bool increase; selection(p->phase, &kind, &index, &increase);
    if (!p->done && now - p->at > 700) {
        if (!p->sent) {
            if (p->phase == 12 || p->phase == 27) {
                if (!p->panel_step) { click(683, 31); p->panel_step = 1; return 0; }
                if (p->panel_step == 1) { click(689, 115); p->panel_step = 2; return 0; }
            }
            p->before = coop_engine_progression_value(actor, kind, index); p->points_before = stat_level_get(actor, STAT_UNSPENT_POINTS); p->damage_before = object_hp_damage_get(actor);
            CoopProgressionRequest r = { .operation = COOP_PROGRESS_EDIT, .kind = kind, .index = (uint8_t)index, .direction = increase ? 1 : 2,
                .lease = coop_progression_lease(service), .expected = (uint32_t)p->before, .points = (uint32_t)p->points_before,
                .epoch = 1, .sequence = p->expected, .map = (uint32_t)map_current_map() };
            memcpy(r.actor, obj_get_id(actor).d.g.data, 16);
            if (!p->host && !p->replica_guard) {
                CoopProgressionBaseline baseline;
                if (coop_engine_progression_open(actor, &baseline)
                    || coop_engine_progression_edit(actor, &baseline, &r) != COOP_PROGRESS_UNAVAILABLE) return -1;
                p->replica_guard = true;
            }
            if (p->phase == 34) click(766, 33);
            if (p->phase == 1 || p->phase == 21 || p->phase == 26 || p->phase == 33 || p->phase == 38) {
                if (!charedit_open(actor, CHAREDIT_MODE_ACTIVE)) return -1;
            } else if (p->phase == 25 || p->phase == 32 || p->phase == 37 || p->phase == 41) charedit_close();
            else if (p->phase == 18 || p->phase == 19 || p->phase == 23) {
                if (p->phase == 18) r.expected++;
                if (p->phase == 19) memcpy(r.actor, obj_get_id(coop_campaign_object(((unsigned)p->member + 1) % 4)).d.g.data, 16);
                if (p->phase == 23) r.lease = p->old_lease;
                if (!coop_progression_submit(service, &r)) return -1;
            } else if (p->phase == 2 || p->phase == 3 || p->phase == 16 || p->phase == 17 || p->phase == 22) click(increase ? 228 : 174, 241);
            else if (p->phase >= 4 && p->phase <= 7) click(increase ? 469 : 413, p->phase <= 5 ? 148 : 218);
            else if (kind == COOP_PROGRESS_SKILL) { if (increase) skill_ui_inc_skill(actor, (int)index); else skill_ui_dec_skill(actor, (int)index); }
            else if (kind == COOP_PROGRESS_COLLEGE) { if (increase) spell_ui_add(actor, (int)index * 5 + p->before); else spell_ui_remove(actor, (int)index * 5 + p->before - 1); }
            else if (kind == COOP_PROGRESS_DISCIPLINE) { if (increase) tech_ui_inc_degree(actor, (int)index); else tech_ui_dec_degree(actor, (int)index); }
            else if (!coop_progression_change(service, kind, index, increase)) return -1;
            if (coop_engine_progression_value(actor, kind, index) != p->before || stat_level_get(actor, STAT_UNSPENT_POINTS) != p->points_before) return -1;
            p->request = r; p->sent = true;
        }
        if (p->phase == 20 && !p->closed && coop_progression_busy(service)) { p->old_lease = coop_progression_lease(service); charedit_close(); p->closed = true; }
        if (p->phase == 39 && !p->closed && coop_progression_busy(service)) {
            p->old_lease = coop_progression_lease(service); charedit_close();
            if (!charedit_open(actor, CHAREDIT_MODE_ACTIVE)) return -1;
            p->closed = true;
        }
        unsigned status; uint32_t result = coop_progression_last_result(service, &status);
        bool ready = result == p->expected;
        if (p->phase == 20) ready = p->closed && !coop_progression_busy(service) && !coop_progression_lease(service);
        if (p->phase == 39) ready = p->closed && result == p->expected + 2 && !coop_progression_busy(service) && coop_progression_lease(service) > p->old_lease;
        unsigned wanted = p->phase == 16 || p->phase == 22 || p->phase == 40 ? COOP_PROGRESS_BASELINE : p->phase == 17 ? COOP_PROGRESS_POINTS : p->phase == 18 ? COOP_PROGRESS_CHANGED : p->phase == 19 ? COOP_PROGRESS_OWNERSHIP : p->phase == 23 ? COOP_PROGRESS_LEASE : p->phase == 24 ? COOP_PROGRESS_LIMIT : p->phase == 29 || p->phase == 35 ? COOP_PROGRESS_REQUIREMENTS : 0;
        if (ready && status != wanted) { fprintf(stderr, "ARCMP progress bad status phase=%u status=%u wanted=%u\n", p->phase, status, wanted); return -1; }
        if (ready && (p->phase == 1 || p->phase == 21 || p->phase == 26 || p->phase == 33 || p->phase == 38)) ready = coop_progression_lease(service) && charedit_is_created();
        if (ready && (p->phase == 25 || p->phase == 32 || p->phase == 37 || p->phase == 41)) ready = !coop_progression_lease(service) && !charedit_is_created();
        if (ready && p->phase != 1 && p->phase != 21 && p->phase != 25 && p->phase != 26 && p->phase != 32 && p->phase != 33 && p->phase != 37 && p->phase != 38 && p->phase != 41) {
            int after = coop_engine_progression_value(actor, kind, index), points = stat_level_get(actor, STAT_UNSPENT_POINTS);
            ready = object_hp_damage_get(actor) == p->damage_before;
            if (wanted) ready &= after == p->before && points == p->points_before;
            else if (kind == COOP_PROGRESS_SCHEME) ready &= after == (int)index && points == p->points_before;
            else ready &= increase ? after > p->before && points < p->points_before : after < p->before && points > p->points_before;
        }
        if (ready && (p->phase == 8 || p->phase == 16) && !p->replayed) {
            uint8_t bytes[COOP_PROGRESSION_REQUEST_BYTES];
            if (!coop_progression_request_encode(&p->request, bytes)) return -1;
            if (!(p->host ? coop_progression_message(service, 0, COOP_PROGRESSION_REQUEST, bytes, sizeof(bytes)) : coop_session_send(p->session, 0, COOP_PROGRESSION_REQUEST, bytes, sizeof(bytes)))) return -1;
            p->replayed = true; p->replays++; p->quiet = now; ready = false;
        }
        if (p->replayed && now - p->quiet < 600) ready = false;
        if (ready) {
            if (wanted) p->denials++;
            if (p->framebuffer && (p->phase == 1 || p->phase == 4 || p->phase == 12 || p->phase == 16 || p->phase == 20 || p->phase == 22 || p->phase == 29 || p->phase == 34 || p->phase == 35 || p->phase == 39 || p->phase == 40)) {
                gamelib_redraw(); iso_redraw(); tig_window_display();
                char path[80]; snprintf(path, sizeof(path), "arcmp-progress-%u.bmp", p->phase); if (tig_video_screenshot_save(path) != TIG_OK) return -1;
            }
            char message[64]; snprintf(message, sizeof(message), "progress-done:%u", p->phase);
            if (!coop_session_chat(p->session, message)) return -1;
            p->done = true; p->completed = p->phase;
        }
    }
    if (p->host) {
        bool all = true; for (unsigned i = 0; i < 4; i++) all &= p->ack[i] == p->phase;
        if (all) {
            if (!p->quiet) p->quiet = now;
            if (now - p->quiet < 600) return 0;
            if (p->phase == PHASES) { p->complete = true; return 1; }
            if (!announce(p, p->phase + 1)) return -1;
            p->ack[0] = 0;
        }
    }
    return 0;
}
bool coop_progression_probe_report(CoopProgressionProbe* p)
{
    if (!p) return false;
    unsigned notices = charedit_coop_level_notifications() - p->level_notice_base;
    bool good = p->completed == PHASES && p->replays == 2 && p->denials == 10
        && p->replica_guard == !p->host && notices == (p->host ? 0u : 1u);
    FILE* file = fopen("arcmp-progress-result.json", "wb"); if (!file) return false;
    CoopProgression* s = coop_gameplay_progression(p->game);
    fprintf(file, "{\"verified\":%s,\"phases\":%u,\"member\":%d,\"replays\":%u,\"denials\":%u,\"executed\":%u,\"rejected\":%u,\"replica_guard\":%s,\"replica_level_notices\":%u,\"earned_points\":[%u,%u,%u,%u]}\n",
        good ? "true" : "false", p->completed, p->member, p->replays, p->denials, coop_progression_executed(s), coop_progression_rejected(s), p->replica_guard ? "true" : "false", notices, p->earned[0], p->earned[1], p->earned[2], p->earned[3]);
    return fclose(file) == 0 && good;
}
void coop_progression_probe_destroy(CoopProgressionProbe* p) { free(p); }
