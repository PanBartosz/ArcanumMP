#include "coop/audio_probe.h"
#include "coop/campaign.h"
#include "game/gamelib.h"
#include "game/gsound.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/player.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include <stdio.h>
#include <stdlib.h>

struct CoopAudioProbe {
    CoopSession* session;
    CoopGameplay* game;
    CoopEngineAudio* audio;
    bool host, framebuffer, started, sent, done, complete, stopped;
    int member, volume, sound;
    unsigned phase, ack[4], state;
    uint64_t chats, phase_at, sample_at, quiet_at, token, old_token;
    tig_sound_handle_t loop, ui;
    uint64_t loop_instance, ui_instance;
    TigSoundSignal before, samples[3];
    int pan[3], gain[3];
    int64_t origin;
    bool native_reuse, listener_reuse;
};
static bool announce(CoopAudioProbe* p, unsigned phase)
{
    char text[64]; snprintf(text, sizeof(text), "audio-phase:%u", phase);
    return coop_session_chat(p->session, text);
}
CoopAudioProbe* coop_audio_probe_create(CoopSession* s, CoopGameplay* g, int member, bool framebuffer)
{
    CoopAudioProbe* p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->session = s; p->game = g; p->audio = coop_gameplay_audio(g); p->member = member;
    p->host = coop_session_is_host(s); p->framebuffer = framebuffer;
    p->loop = p->ui = TIG_SOUND_HANDLE_INVALID;
    p->volume = settings_get_value(&settings, EFFECTS_VOLUME_KEY);
    tig_sound_probe_enable(true);
    return p;
}
static bool capture(void)
{
    location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    iso_interface_refresh(); intgame_show(); gamelib_redraw(); iso_redraw(); tig_window_display();
    return tig_video_screenshot_save("arcmp-audio-loop.bmp") == TIG_OK;
}
static int64_t source(CoopAudioProbe* p)
{
    return p->phase == 2 ? location_make(location_get_x(p->origin) + 4, location_get_y(p->origin) - 4) : p->origin;
}
static bool measured(CoopAudioProbe* p, uint64_t now, unsigned index, bool muted)
{
    tig_sound_handle_t handle; uint64_t instance; int64_t loc;
    if (!coop_engine_audio_instance(p->audio, p->token, &handle, &instance, &loc) || loc != source(p)) return false;
    int side = ((p->member + (p->phase == 2)) % 2) ? 6 : -6;
    if (index == 2) side = 0;
    gsound_listener_set(location_make(location_get_x(loc) + side, location_get_y(loc) - side));
    if (!p->sample_at) {
        if (!tig_sound_probe_signal(handle, &p->before)) return false;
        p->sample_at = now;
        return false;
    }
    if (now - p->sample_at < 700) return false;
    TigSoundSignal after;
    if (!tig_sound_probe_signal(handle, &after) || !tig_sound_is_playing(handle)) return false;
    p->samples[index] = (TigSoundSignal) { after.frames - p->before.frames, after.left - p->before.left, after.right - p->before.right };
    p->pan[index] = tig_sound_get_extra_volume(handle); p->gain[index] = tig_sound_get_volume(handle);
    TigSoundSignal* signal = &p->samples[index];
    if (muted) return p->gain[index] == 0 && signal->left == 0 && signal->right == 0;
    if (p->gain[index] <= 0 || signal->frames < 5000 || signal->left + signal->right < 1e-12) return false;
    if (index < 2) {
        if (p->pan[index] < 32) return signal->left > 4 * signal->right;
        if (p->pan[index] > 96) return signal->right > 4 * signal->left;
        return false;
    }
    return true;
}
int coop_audio_probe_poll(CoopAudioProbe* p, uint64_t now, bool start)
{
    for (; p->chats < coop_session_chat_count(p->session); p->chats++) {
        const CoopChat* c = coop_session_chat_at(p->session, p->chats);
        unsigned phase; unsigned long long token;
        if (!c->slot && sscanf(c->text, "audio-phase:%u", &phase) == 1) {
            if (phase != p->phase + 1 || phase > 5) return -1;
            p->phase = phase; p->phase_at = now; p->sample_at = p->quiet_at = 0;
            p->sent = p->done = false; p->state = 0;
            if (phase == 1) {
                p->origin = obj_field_int64_get(coop_campaign_object(0), OBJ_F_LOCATION);
                settings_set_value(&settings, EFFECTS_VOLUME_KEY, 10);
            }
            if (phase == 4) settings_set_value(&settings, EFFECTS_VOLUME_KEY, p->host ? 0 : 10);
            fprintf(stderr, "ARCMP audio phase=%u member=%d\n", phase, p->member);
        }
        if (!c->slot && sscanf(c->text, "audio-token:%llu", &token) == 1) p->token = (uint64_t)token;
        if (sscanf(c->text, "audio-done:%u", &phase) == 1 && c->slot < 4) p->ack[c->slot] = phase;
    }
    if (p->host && start && !p->started) { p->started = true; if (!announce(p, 1)) return -1; }
    if (!p->phase) return 0;
    if (now - p->phase_at > 20000) {
        unsigned index = p->phase == 2 ? 1 : p->phase == 4 ? 2 : 0;
        fprintf(stderr, "ARCMP audio timeout phase=%u member=%d token=%llu state=%u pan=%d gain=%d frames=%llu left=%.12g right=%.12g\n",
            p->phase, p->member, (unsigned long long)p->token, p->state, p->pan[index], p->gain[index],
            (unsigned long long)p->samples[index].frames, p->samples[index].left, p->samples[index].right);
        return -1;
    }
    if (!p->done) {
        bool ready = false;
        if (p->phase == 1 || p->phase == 4) {
            if (p->host && !p->sent) {
                CoopAudioStats stats; coop_engine_audio_stats(p->audio, &stats);
                if (p->phase == 1) p->sound = (int)stats.last_sound;
                p->loop = gsound_play_sfx_at_loc_sized(p->sound, 0, p->origin, TIG_SOUND_SIZE_LARGE);
                p->loop_instance = tig_sound_instance(p->loop);
                if (!p->loop_instance) return -1;
                if (p->phase == 4) {
                    tig_sound_handle_t old = p->loop; uint64_t old_instance = p->loop_instance;
                    uint64_t old_token = coop_engine_audio_native_token(old);
                    tig_sound_destroy(old);
                    p->loop = gsound_play_sfx_at_loc_sized(p->sound, 0, p->origin, TIG_SOUND_SIZE_LARGE);
                    p->loop_instance = tig_sound_instance(p->loop);
                    p->native_reuse = p->loop == old && p->loop_instance != old_instance && old_token != coop_engine_audio_native_token(p->loop);
                    if (!p->native_reuse) return -1;
                }
                uint64_t published = coop_engine_audio_native_token(p->loop);
                char text[64]; snprintf(text, sizeof(text), "audio-token:%llu", (unsigned long long)published);
                if (!published || !coop_session_chat(p->session, text)) return -1;
                p->sent = true;
            }
            ready = p->token && measured(p, now, p->phase == 4 ? 2 : 0, p->host && p->phase == 4);
        } else if (p->phase == 2) {
            if (p->host && !p->sent) { gsound_move(p->loop, source(p)); p->sent = true; }
            ready = measured(p, now, 1, false);
            if (ready && ((p->pan[0] < 64) == (p->pan[1] < 64))) return -1;
        } else if (p->phase == 3) {
            if (!p->state) {
                p->old_token = p->token;
                if (!p->host) {
                    tig_sound_handle_t old; uint64_t old_instance;
                    if (!coop_engine_audio_instance(p->audio, p->token, &old, &old_instance, NULL)) return 0;
                    tig_sound_destroy(old);
                    CoopAudioStats stats; coop_engine_audio_stats(p->audio, &stats); p->sound = (int)stats.last_sound;
                    tig_sound_handle_t fillers[TIG_SOUND_MAX_HANDLES]; unsigned count = 0;
                    /* Occupy intervening free slots so reuse of the released
                     * remote slot is deterministic even with ambient sounds. */
                    do {
                        p->ui = gsound_play_sfx(p->sound, 0);
                        if (p->ui == old || p->ui == TIG_SOUND_HANDLE_INVALID) break;
                        fillers[count++] = p->ui;
                    } while (count < TIG_SOUND_MAX_HANDLES);
                    for (unsigned i = 0; i < count; i++) tig_sound_destroy(fillers[i]);
                    p->ui_instance = tig_sound_instance(p->ui);
                    p->listener_reuse = p->ui == old && p->ui_instance && p->ui_instance != old_instance;
                    if (!p->listener_reuse) return -1;
                }
                p->state = 1;
            }
            ready = true;
        } else if (p->phase == 5) {
            ready = coop_engine_audio_instance(p->audio, p->token, NULL, NULL, NULL);
            if (ready && p->framebuffer && !capture()) return -1;
        }
        if (ready) {
            char text[64]; snprintf(text, sizeof(text), "audio-done:%u", p->phase);
            if (!coop_session_chat(p->session, text)) return -1;
            p->done = true;
        }
    }
    if (p->host) {
        bool all = true; for (unsigned i = 0; i < 4; i++) all &= p->ack[i] == p->phase;
        if (!all) return 0;
        if (!p->quiet_at) p->quiet_at = now;
        if (now - p->quiet_at < 300) return 0;
        if (p->phase == 3) tig_sound_destroy(p->loop);
        if (p->phase == 5) { p->complete = true; return 1; }
        if (!announce(p, p->phase + 1)) return -1;
        p->ack[0] = 0;
    }
    return p->complete ? 1 : 0;
}
bool coop_audio_probe_report(CoopAudioProbe* p)
{
    if (!p || p->phase != 5 || !p->done || (p->host ? !p->native_reuse : !p->listener_reuse)) return false;
    CoopAudioStats s; coop_engine_audio_stats(p->audio, &s);
    bool ui_alive = p->host || (p->ui_instance == tig_sound_instance(p->ui) && tig_sound_is_playing(p->ui));
    bool good = !s.loops && !coop_engine_audio_instance(p->audio, p->token, NULL, NULL, NULL) && s.events && s.starts && s.moves && s.stops && ui_alive;
    FILE* f = fopen("arcmp-audio-result.json", "wb"); if (!f) return false;
    fprintf(f, "{\"verified\":%s,\"phases\":5,\"host\":%s,\"member\":%d,\"starts\":%llu,\"moves\":%llu,\"stops\":%llu,\"unavailable\":%llu,\"wire_events\":%llu,\"wire_digest\":%u,\"active_loops\":%u,\"native_handle_reuse\":%s,\"listener_handle_reuse\":%s,\"local_ui_survives_old_stop\":%s,\"host_muted\":%s,\"pcm\":[",
        good ? "true" : "false", p->host ? "true" : "false", p->member, (unsigned long long)s.starts, (unsigned long long)s.moves,
        (unsigned long long)s.stops, (unsigned long long)s.unavailable, (unsigned long long)s.events, s.digest, s.loops,
        p->native_reuse ? "true" : "false", p->listener_reuse ? "true" : "false", ui_alive ? "true" : "false", p->host ? "true" : "false");
    for (unsigned i = 0; i < 3; i++) fprintf(f, "%s{\"frames\":%llu,\"left\":%.12g,\"right\":%.12g,\"pan\":%d,\"gain\":%d}", i ? "," : "", (unsigned long long)p->samples[i].frames, p->samples[i].left, p->samples[i].right, p->pan[i], p->gain[i]);
    fprintf(f, "]}\n");
    return fclose(f) == 0 && good;
}
void coop_audio_probe_stop(CoopAudioProbe* p)
{
    if (!p || p->stopped) return;
    if (p->ui_instance && p->ui_instance == tig_sound_instance(p->ui)) tig_sound_destroy(p->ui);
    if (p->loop_instance && p->loop_instance == tig_sound_instance(p->loop)) tig_sound_destroy(p->loop);
    settings_set_value(&settings, EFFECTS_VOLUME_KEY, p->volume);
    tig_sound_probe_enable(false); p->stopped = true;
}
void coop_audio_probe_destroy(CoopAudioProbe* p) { free(p); }
