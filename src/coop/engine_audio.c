#include "coop/engine_audio.h"
#include "coop/authority.h"
#include "game/gsound.h"
#include "game/location.h"
#include "game/map.h"
#include <stdlib.h>
#include <string.h>

enum { LISTENER_SOUNDS = 128 };
typedef struct NativeSound {
    uint64_t instance, token;
    int64_t location;
    int id, loops, size, map;
} NativeSound;
typedef struct ListenerSound {
    uint64_t token, instance;
    tig_sound_handle_t handle;
    int64_t location;
    bool loop, ending;
} ListenerSound;
struct CoopEngineAudio {
    CoopAudio* network;
    bool host, enabled, sealed, failed;
    uint32_t map;
    uint64_t last_start;
    CoopAudioStats stats;
    ListenerSound sounds[LISTENER_SOUNDS];
};
static NativeSound native_sounds[TIG_SOUND_MAX_HANDLES];
static CoopEngineAudio* bound;
static uint64_t next_token;

static bool publishing(void)
{
    return bound && bound->host && bound->enabled && !bound->sealed
        && coop_authority_get() == COOP_AUTHORITY_HOST && map_current_map() == (int)bound->map;
}
static bool emit(CoopEngineAudio* a, const CoopAudioEvent* event)
{
    bool good = coop_audio_emit(a->network, event);
    if (!good) a->failed = true;
    else if (event->operation == COOP_AUDIO_START) { a->stats.starts++; a->stats.last_sound = event->sound; }
    else if (event->operation == COOP_AUDIO_MOVE) a->stats.moves++;
    else a->stats.stops++;
    return good;
}
static uint64_t start(CoopEngineAudio* a, int id, int loops, int64_t loc, int size)
{
    if (next_token == UINT64_MAX) { a->failed = true; return 0; }
    CoopAudioEvent e = { .operation = COOP_AUDIO_START, .token = ++next_token, .sound = (uint32_t)id,
        .loops = (uint16_t)loops, .size = (uint8_t)size, .x = (uint32_t)location_get_x(loc), .y = (uint32_t)location_get_y(loc) };
    return emit(a, &e) ? e.token : 0;
}
void coop_engine_audio_native_start(tig_sound_handle_t handle, int id, int loops, int64_t loc, int size)
{
    if (id < 0 || loops < 0 || loops > UINT16_MAX || size < 0 || size >= TIG_SOUND_SIZE_COUNT) return;
    uint64_t instance = tig_sound_instance(handle);
    NativeSound* n = handle >= 0 && handle < TIG_SOUND_MAX_HANDLES ? &native_sounds[handle] : NULL;
    if (n) *n = (NativeSound) { .instance = instance, .location = loc, .id = id, .loops = loops, .size = size, .map = map_current_map() };
    if (!publishing()) return;
    char path[TIG_MAX_PATH];
    if (gsound_resolve_path(id, path, sizeof(path)) != TIG_OK) return;
    /* One-shots are independent of the host device/volume. A native infinite
     * loop needs a real lifetime handle so its later stop can be observed. */
    if (!loops && !instance) return;
    uint64_t token = start(bound, id, loops, loc, size);
    if (n && instance) n->token = token;
}
void coop_engine_audio_native_move(tig_sound_handle_t handle, int64_t location)
{
    if (handle < 0 || handle >= TIG_SOUND_MAX_HANDLES) return;
    NativeSound* n = &native_sounds[handle];
    if (!n->instance || n->instance != tig_sound_instance(handle) || n->location == location) return;
    n->location = location;
    if (publishing() && n->token) {
        CoopAudioEvent e = { .operation = COOP_AUDIO_MOVE, .token = n->token,
            .x = (uint32_t)location_get_x(location), .y = (uint32_t)location_get_y(location) };
        emit(bound, &e);
    }
}
void coop_engine_audio_native_stop(tig_sound_handle_t handle, int fade, bool completed)
{
    if (handle < 0 || handle >= TIG_SOUND_MAX_HANDLES) return;
    NativeSound* n = &native_sounds[handle];
    if (n->instance && n->instance == tig_sound_instance(handle) && publishing() && n->token && (!completed || !n->loops)) {
        CoopAudioEvent e = { .operation = COOP_AUDIO_STOP, .token = n->token, .fade = (uint32_t)(fade > 60000 ? 60000 : fade) };
        emit(bound, &e);
    }
    memset(n, 0, sizeof(*n));
}
static void cleanup(CoopEngineAudio* a)
{
    for (unsigned i = 0; i < LISTENER_SOUNDS; i++) {
        ListenerSound* s = &a->sounds[i];
        if (s->token && s->instance && tig_sound_instance(s->handle) != s->instance) memset(s, 0, sizeof(*s));
    }
}
static bool apply(void* context, const CoopAudioEvent* events, unsigned count)
{
    CoopEngineAudio* a = context;
    int64_t max_x, max_y; location_limits_get(&max_x, &max_y);
    uint64_t last = a->last_start, loops[LISTENER_SOUNDS] = { 0 };
    for (unsigned i = 0; i < LISTENER_SOUNDS; i++)
        if (a->sounds[i].token && a->sounds[i].loop && !a->sounds[i].ending) loops[i] = a->sounds[i].token;
    /* All native lookups and coordinates are validated before any sound plays.
     * Old finished one-shots may receive harmless moves/stops; future tokens
     * and reused start identities are rejected. */
    for (unsigned i = 0; i < count; i++) {
        const CoopAudioEvent* e = &events[i];
        if (e->operation != COOP_AUDIO_STOP && ((uint64_t)e->x >= (uint64_t)max_x || (uint64_t)e->y >= (uint64_t)max_y)) return false;
        if (e->operation == COOP_AUDIO_START) {
            char path[TIG_MAX_PATH];
            if (e->token <= last || gsound_resolve_path((int)e->sound, path, sizeof(path)) != TIG_OK) return false;
            last = e->token;
            if (!e->loops) {
                unsigned slot = 0; while (slot < LISTENER_SOUNDS && loops[slot]) slot++;
                if (slot == LISTENER_SOUNDS) return false;
                loops[slot] = e->token;
            }
        } else {
            if (e->token > last) return false;
            if (e->operation == COOP_AUDIO_STOP)
                for (unsigned j = 0; j < LISTENER_SOUNDS; j++) if (loops[j] == e->token) loops[j] = 0;
        }
    }
    cleanup(a);
    for (unsigned i = 0; i < count; i++) {
        const CoopAudioEvent* e = &events[i];
        if (e->operation == COOP_AUDIO_START) {
            a->last_start = e->token; a->stats.starts++; a->stats.last_sound = e->sound;
            tig_sound_handle_t handle = gsound_play_sfx_at_loc_sized((int)e->sound, e->loops, location_make(e->x, e->y), e->size);
            uint64_t instance = tig_sound_instance(handle);
            if (!instance) a->stats.unavailable++;
            if (instance || !e->loops) {
                unsigned slot = 0; while (slot < LISTENER_SOUNDS && a->sounds[slot].token) slot++;
                if (slot == LISTENER_SOUNDS) {
                    if (instance) tig_sound_destroy(handle);
                    a->stats.unavailable++;
                } else a->sounds[slot] = (ListenerSound) { .token = e->token, .instance = instance, .handle = handle,
                    .location = location_make(e->x, e->y), .loop = e->loops == 0 };
            }
            continue;
        }
        if (e->operation == COOP_AUDIO_MOVE) a->stats.moves++; else a->stats.stops++;
        for (unsigned j = 0; j < LISTENER_SOUNDS; j++) {
            ListenerSound* s = &a->sounds[j];
            if (s->token != e->token) continue;
            bool live = s->instance && s->instance == tig_sound_instance(s->handle);
            if (e->operation == COOP_AUDIO_MOVE) {
                s->location = location_make(e->x, e->y);
                if (live && !s->ending) gsound_move(s->handle, location_make(e->x, e->y));
            } else {
                if (live && e->fade) { tig_sound_stop(s->handle, (int)e->fade); s->ending = true; }
                else { if (live) tig_sound_destroy(s->handle); memset(s, 0, sizeof(*s)); }
            }
        }
    }
    return true;
}
CoopEngineAudio* coop_engine_audio_create(CoopSession* session, uint32_t epoch, uint32_t map)
{
    if (bound) return NULL;
    CoopEngineAudio* a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->host = coop_session_is_host(session); a->map = map;
    a->network = coop_audio_create(session, epoch, map, apply, a);
    if (!a->network) { free(a); return NULL; }
    for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) native_sounds[i].token = 0;
    bound = a;
    return a;
}
void coop_engine_audio_destroy(CoopEngineAudio* a)
{
    if (!a) return;
    if (bound == a) bound = NULL;
    for (unsigned i = 0; i < LISTENER_SOUNDS; i++) {
        ListenerSound* s = &a->sounds[i];
        if (s->instance && s->instance == tig_sound_instance(s->handle)) tig_sound_destroy(s->handle);
    }
    coop_audio_destroy(a->network); free(a);
}
bool coop_engine_audio_message(CoopEngineAudio* a, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    return a && coop_audio_message(a->network, sender, type, bytes, size);
}
bool coop_engine_audio_poll(CoopEngineAudio* a, uint64_t now, bool running)
{
    if (!a || a->failed) return false;
    cleanup(a);
    if (running && !a->enabled && !a->sealed) {
        a->enabled = true;
        if (a->host) {
            for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) {
                NativeSound* n = &native_sounds[i];
                if (n->instance && !n->loops && n->map == (int)a->map && n->instance == tig_sound_instance((int)i))
                    n->token = start(a, n->id, 0, n->location, n->size);
            }
        }
    }
    return !a->failed && coop_audio_poll(a->network, now);
}
void coop_engine_audio_seal(CoopEngineAudio* a)
{
    if (!a || a->sealed) return;
    if (a->host) {
        for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) {
            NativeSound* n = &native_sounds[i];
            if (n->token && !n->loops) {
                CoopAudioEvent e = { .operation = COOP_AUDIO_STOP, .token = n->token };
                emit(a, &e); n->token = 0;
            }
        }
    }
    a->sealed = true; coop_audio_seal(a->network);
}
bool coop_engine_audio_settled(const CoopEngineAudio* a) { return a && !a->failed && coop_audio_settled(a->network); }
uint64_t coop_engine_audio_native_token(tig_sound_handle_t handle)
{
    return handle >= 0 && handle < TIG_SOUND_MAX_HANDLES && native_sounds[handle].instance == tig_sound_instance(handle)
        ? native_sounds[handle].token : 0;
}
bool coop_engine_audio_instance(const CoopEngineAudio* a, uint64_t token, tig_sound_handle_t* handle, uint64_t* instance, int64_t* location)
{
    if (!a || !token) return false;
    if (a->host) {
        for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) {
            const NativeSound* n = &native_sounds[i];
            if (n->token != token || !n->instance || n->instance != tig_sound_instance((int)i)) continue;
            if (handle) *handle = (int)i;
            if (instance) *instance = n->instance;
            if (location) *location = n->location;
            return true;
        }
    } else {
        for (unsigned i = 0; i < LISTENER_SOUNDS; i++) {
            const ListenerSound* s = &a->sounds[i];
            if (s->token != token || s->ending || !s->instance || s->instance != tig_sound_instance(s->handle)) continue;
            if (handle) *handle = s->handle;
            if (instance) *instance = s->instance;
            if (location) *location = s->location;
            return true;
        }
    }
    return false;
}
void coop_engine_audio_quiet(CoopEngineAudio* a)
{
    if (!a) return;
    a->sealed = true;
    for (unsigned i = 0; i < LISTENER_SOUNDS; i++) {
        ListenerSound* s = &a->sounds[i];
        if (s->instance && s->instance == tig_sound_instance(s->handle)) tig_sound_destroy(s->handle);
        memset(s, 0, sizeof(*s));
    }
    if (a->host) {
        for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) {
            NativeSound* n = &native_sounds[i];
            if (n->token && !n->loops && n->instance == tig_sound_instance((int)i)) tig_sound_destroy((int)i);
        }
    }
}
void coop_engine_audio_stats(const CoopEngineAudio* a, CoopAudioStats* out)
{
    *out = a->stats; out->loops = 0;
    out->events = coop_audio_events(a->network); out->digest = coop_audio_digest(a->network);
    if (a->host) {
        for (unsigned i = 0; i < TIG_SOUND_MAX_HANDLES; i++) out->loops += native_sounds[i].token && !native_sounds[i].loops;
    } else {
        for (unsigned i = 0; i < LISTENER_SOUNDS; i++) out->loops += a->sounds[i].token && a->sounds[i].loop && !a->sounds[i].ending;
    }
}
