#include "coop/audio.h"
#include "coop/object_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static CoopSession* sessions[4];
static CoopAudio* audio[4];
static uint64_t now = 100, applied[4];
static bool apply(void* context, const CoopAudioEvent* events, unsigned count)
{
    unsigned peer = *(unsigned*)context;
    CHECK(peer > 0 && peer < 4 && count && events[0].token);
    applied[peer] += count;
    return true;
}
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    return coop_audio_message(context, sender, type, p, size);
}
static void pump(unsigned rounds, unsigned peers)
{
    while (rounds--) {
        for (unsigned i = 0; i < peers; i++) {
            coop_session_poll(sessions[i], now);
            if (audio[i]) CHECK(coop_audio_poll(audio[i], now));
        }
        now++;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 }; nanosleep(&delay, NULL);
#endif
    }
}
int main(void)
{
    CoopAudioEvent event = { .operation = COOP_AUDIO_START, .token = UINT64_C(0xfedcba9876543210),
        .sound = 1234, .loops = 65535, .size = 3, .x = 123, .y = 456 }, decoded;
    uint8_t bytes[COOP_AUDIO_EVENT_BYTES], canonical[COOP_AUDIO_EVENT_BYTES];
    CHECK(coop_audio_event_encode(&event, bytes) && coop_audio_event_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.token == event.token && decoded.loops == 65535 && decoded.size == 3 && decoded.x == 123 && decoded.y == 456);
    CHECK(bytes[8] == 0xfe && bytes[15] == 0x10 && bytes[2] == 255 && bytes[3] == 255);
    for (unsigned n = 0; n < sizeof(bytes); n++) CHECK(!coop_audio_event_decode(bytes, n, &decoded));
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        uint8_t saved = bytes[i]; bytes[i] ^= 255;
        if (coop_audio_event_decode(bytes, sizeof(bytes), &decoded))
            CHECK(coop_audio_event_encode(&decoded, canonical) && !memcmp(bytes, canonical, sizeof(bytes)));
        bytes[i] = saved;
    }
    event.operation = COOP_AUDIO_STOP;
    CHECK(!coop_audio_event_encode(&event, bytes));
    event = (CoopAudioEvent) { .operation = COOP_AUDIO_STOP, .token = 1, .fade = 60000 };
    CHECK(coop_audio_event_encode(&event, bytes));
    event.fade++; CHECK(!coop_audio_event_encode(&event, bytes));
    unsigned indices[4] = { 0, 1, 2, 3 };
    for (unsigned i = 0; i < 4; i++) {
        CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Audio %u", i);
        sessions[i] = coop_session_create(&config); CHECK(sessions[i]);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    for (unsigned i = 1; i < 4; i++) CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(50, 4);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_session_phase(sessions[i]) == COOP_LOBBY);
        audio[i] = coop_audio_create(sessions[i], 42, 7, apply, &indices[i]); CHECK(audio[i]);
        coop_session_set_handler(sessions[i], message, audio[i]);
    }
    for (unsigned i = 1; i <= 300; i++) {
        event = (CoopAudioEvent) { .operation = COOP_AUDIO_START, .token = i, .sound = i, .loops = 1, .x = i, .y = 5 };
        CHECK(coop_audio_emit(audio[0], &event));
    }
    pump(50, 3); /* No delivery/ACK from the fourth participant. */
    CHECK(applied[1] == 36 && applied[2] == 36 && !applied[3] && !coop_audio_events(audio[0]));
    for (unsigned i = 0; i < 1000 && coop_audio_events(audio[0]) != 300; i++) pump(1, 4);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_audio_events(audio[i]) == 300);
        CHECK(coop_audio_digest(audio[i]) == coop_audio_digest(audio[0]));
    }
    /* Coalescing preserves the final move and never discards start/stop. */
    for (unsigned i = 0; i < 100; i++) {
        event = (CoopAudioEvent) { .operation = COOP_AUDIO_MOVE, .token = 300, .x = i, .y = 8 };
        CHECK(coop_audio_emit(audio[0], &event));
    }
    event = (CoopAudioEvent) { .operation = COOP_AUDIO_STOP, .token = 300 };
    CHECK(coop_audio_emit(audio[0], &event));
    coop_audio_seal(audio[0]); CHECK(!coop_audio_emit(audio[0], &event));
    for (unsigned i = 0; i < 1000 && !coop_audio_settled(audio[0]); i++) pump(1, 4);
    CHECK(coop_audio_settled(audio[0]) && coop_audio_events(audio[0]) == 302);
    for (unsigned i = 1; i < 4; i++) CHECK(applied[i] == 302 && coop_audio_digest(audio[i]) == coop_audio_digest(audio[0]));
    /* Manual replay of an applied packet cannot invoke presentation twice. */
    CoopAudio* replica = coop_audio_create(sessions[1], 99, 7, apply, &indices[1]); CHECK(replica);
    uint8_t packet[COOP_AUDIO_HEADER_BYTES + COOP_AUDIO_EVENT_BYTES] = { 0 };
    coop_object_write32(packet, 99); coop_object_write32(packet + 4, 1); coop_object_write32(packet + 8, 7); packet[13] = 1;
    event = (CoopAudioEvent) { .operation = COOP_AUDIO_START, .token = 1, .sound = 1, .loops = 1 };
    CHECK(coop_audio_event_encode(&event, packet + COOP_AUDIO_HEADER_BYTES));
    CHECK(!coop_audio_message(replica, 2, COOP_AUDIO_BATCH, packet, sizeof(packet)));
    packet[14] = 1; CHECK(!coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet))); packet[14] = 0;
    packet[11] = 8; CHECK(!coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet))); packet[11] = 7;
    CHECK(!coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet) - 1));
    CHECK(coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet)));
    CHECK(coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet)));
    uint64_t before = applied[1]; CHECK(coop_audio_poll(replica, now)); CHECK(applied[1] == before + 1);
    CHECK(coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet)));
    CHECK(coop_audio_poll(replica, now)); CHECK(applied[1] == before + 1);
    packet[23] ^= 1; CHECK(!coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet))); packet[23] ^= 1;
    coop_audio_destroy(replica);
    replica = coop_audio_create(sessions[1], 99, 7, apply, &indices[1]); CHECK(replica);
    CHECK(coop_audio_message(replica, 0, COOP_AUDIO_BATCH, packet, sizeof(packet)));
    CHECK(!coop_audio_poll(replica, 30001)); coop_audio_destroy(replica);
    CoopAudio* full = coop_audio_create(sessions[0], 100, 7, apply, &indices[0]); CHECK(full);
    for (unsigned i = 0; i < COOP_AUDIO_QUEUE_EVENTS; i++) CHECK(coop_audio_emit(full, &event));
    CHECK(!coop_audio_emit(full, &event) && !coop_audio_poll(full, now)); coop_audio_destroy(full);
    for (unsigned i = 0; i < 4; i++) {
        coop_session_set_handler(sessions[i], NULL, NULL); coop_audio_destroy(audio[i]); coop_session_destroy(sessions[i]);
    }
    puts("PASS: portable sound events, four-peer ordered delivery, delayed ACKs, move coalescing, exact replay, malformed packets, queue/timeout bounds and seal barrier");
    return 0;
}
