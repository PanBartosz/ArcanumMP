#ifndef ARCMP_ENGINE_AUDIO_H
#define ARCMP_ENGINE_AUDIO_H
#include "coop/audio.h"
#include "tig/sound.h"
typedef struct CoopEngineAudio CoopEngineAudio;
typedef struct CoopAudioStats {
    uint64_t starts, moves, stops, unavailable;
    uint64_t events;
    uint32_t digest;
    uint32_t last_sound;
    unsigned loops;
} CoopAudioStats;
CoopEngineAudio* coop_engine_audio_create(CoopSession* session, uint32_t epoch, uint32_t map);
void coop_engine_audio_destroy(CoopEngineAudio* audio);
bool coop_engine_audio_message(CoopEngineAudio* audio, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_engine_audio_poll(CoopEngineAudio* audio, uint64_t now, bool running);
void coop_engine_audio_seal(CoopEngineAudio* audio);
void coop_engine_audio_quiet(CoopEngineAudio* audio);
bool coop_engine_audio_settled(const CoopEngineAudio* audio);
void coop_engine_audio_stats(const CoopEngineAudio* audio, CoopAudioStats* out);
/* Read-only inspection of a live local instance, for diagnostics. */
bool coop_engine_audio_instance(const CoopEngineAudio* audio, uint64_t token, tig_sound_handle_t* handle, uint64_t* instance, int64_t* location);
uint64_t coop_engine_audio_native_token(tig_sound_handle_t handle);
/* Native world-sound boundaries. These also remember already-running loops for
 * a new gameplay epoch. UI/music/voice paths are independent local channels. */
void coop_engine_audio_native_start(tig_sound_handle_t handle, int id, int loops, int64_t location, int size);
void coop_engine_audio_native_move(tig_sound_handle_t handle, int64_t location);
void coop_engine_audio_native_stop(tig_sound_handle_t handle, int fade, bool completed);
#endif
