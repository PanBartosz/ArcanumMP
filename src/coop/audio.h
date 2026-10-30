#ifndef ARCMP_AUDIO_H
#define ARCMP_AUDIO_H
#include "coop/session.h"

enum { COOP_AUDIO_BATCH = 49, COOP_AUDIO_ACK = 50,
    COOP_AUDIO_EVENT_BYTES = 28, COOP_AUDIO_HEADER_BYTES = 16,
    COOP_AUDIO_BATCH_EVENTS = 36, COOP_AUDIO_QUEUE_EVENTS = 1024 };
typedef enum CoopAudioOperation { COOP_AUDIO_START, COOP_AUDIO_MOVE, COOP_AUDIO_STOP } CoopAudioOperation;
typedef struct CoopAudioEvent {
    uint64_t token;
    uint32_t sound, x, y, fade;
    uint16_t loops; /* Native convention: zero repeats indefinitely. */
    uint8_t operation, size;
} CoopAudioEvent;
typedef struct CoopAudio CoopAudio;
/* Validate the complete batch before changing local presentation. Called only
 * from poll(), never a socket callback. Native handles never cross the wire. */
typedef bool (*CoopAudioApply)(void* context, const CoopAudioEvent* events, unsigned count);
bool coop_audio_event_encode(const CoopAudioEvent* event, uint8_t out[COOP_AUDIO_EVENT_BYTES]);
bool coop_audio_event_decode(const void* bytes, size_t size, CoopAudioEvent* out);
CoopAudio* coop_audio_create(CoopSession* session, uint32_t epoch, uint32_t map, CoopAudioApply apply, void* context);
void coop_audio_destroy(CoopAudio* audio);
bool coop_audio_emit(CoopAudio* audio, const CoopAudioEvent* event);
bool coop_audio_message(CoopAudio* audio, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_audio_poll(CoopAudio* audio, uint64_t now);
void coop_audio_seal(CoopAudio* audio);
bool coop_audio_settled(const CoopAudio* audio);
uint64_t coop_audio_events(const CoopAudio* audio);
uint32_t coop_audio_digest(const CoopAudio* audio);
#endif
