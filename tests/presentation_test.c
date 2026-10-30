#include "coop/presentation.h"
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
static CoopSession* sessions[4]; static CoopPresentation* presentations[4];
static uint64_t now = 100; static bool hold_last;
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* data, size_t size) { return coop_present_message(context, sender, type, data, size); }
static void pump(unsigned rounds)
{
    while (rounds--) {
        for (unsigned i = 0; i < 4; i++) {
            coop_session_poll(sessions[i], now);
            if (presentations[i] && !(hold_last && i == 3)) CHECK(coop_present_poll(presentations[i], now));
        }
        now += 10;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 }; nanosleep(&delay, NULL);
#endif
    }
}
static void phase(CoopPresentPhase expected) { pump(80); for (unsigned i = 0; i < 4; i++) CHECK(coop_present_current(presentations[i])->phase == expected); }
int main(void)
{
    CoopPresentView v = { .sequence = 1, .revision = 1, .participants = 15, .path = "movies\\00069.bik" }, decoded;
    uint8_t bytes[COOP_PRESENT_BYTES]; CHECK(coop_present_encode(&v, bytes) && coop_present_decode(bytes, sizeof(bytes), &decoded));
    uint32_t random = 42;
    for (unsigned i = 0; i < 20000; i++) {
        random = random * 1664525u + 1013904223u; unsigned at = random % sizeof(bytes); uint8_t original = bytes[at]; bytes[at] ^= (uint8_t)(random >> 24);
        if (coop_present_decode(bytes, sizeof(bytes), &decoded)) { uint8_t roundtrip[sizeof(bytes)]; CHECK(coop_present_encode(&decoded, roundtrip) && !memcmp(bytes, roundtrip, sizeof(bytes))); }
        bytes[at] = original;
    }
    strcpy(v.path, "movies\\..\\evil.bik"); CHECK(!coop_present_encode(&v, bytes));
    v = (CoopPresentView) { .sequence = 1, .revision = 1, .participants = 15, .kind = COOP_PRESENT_SLIDE, .id = 12 };
    CHECK(coop_present_encode(&v, bytes)); v.phase = COOP_PRESENT_COMPLETE; CHECK(!coop_present_encode(&v, bytes));
    v = (CoopPresentView) { .sequence = 1, .revision = 1, .participants = 15, .kind = COOP_PRESENT_FADE, .id = 0xffffff, .flags = (48u << 8) | 1, .track = 2000 };
    CHECK(coop_present_encode(&v, bytes) && coop_present_decode(bytes, sizeof(bytes), &decoded));
    v.track = 60001;
    CHECK(!coop_present_encode(&v, bytes));
    v.track = 2000;
    v.id = 0x1000000;
    CHECK(!coop_present_encode(&v, bytes));
    v.id = 0;
    v.flags = 2;
    CHECK(!coop_present_encode(&v, bytes));
    v.flags = 1025u << 8;
    CHECK(!coop_present_encode(&v, bytes));
    for (unsigned i = 0; i < 4; i++) { CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Viewer %u", i); sessions[i] = coop_session_create(&config); CHECK(sessions[i]); }
    CHECK(coop_session_host(sessions[0], 0, now)); for (unsigned i = 1; i < 4; i++) CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(100);
    for (unsigned i = 0; i < 4; i++) { CHECK(coop_session_local_slot(sessions[i]) == i); presentations[i] = coop_present_create(sessions[i]); CHECK(presentations[i]); coop_session_set_handler(sessions[i], message, presentations[i]); }
    CHECK(coop_present_begin(presentations[0], COOP_PRESENT_MOVIE, 0, "movies\\00069.bik", 0, 0, now)); phase(COOP_PRESENT_PREPARING);
    for (unsigned i = 0; i < 3; i++) coop_present_ready(presentations[i], true);
    phase(COOP_PRESENT_PREPARING);
    uint8_t ack[10] = { 0 }; coop_object_write32(ack, 1); coop_object_write32(ack + 4, coop_present_current(presentations[0])->revision); ack[8] = 2;
    CHECK(!coop_present_message(presentations[0], 3, COOP_PRESENT_ACK, ack, sizeof(ack)));
    coop_present_ready(presentations[3], true); phase(COOP_PRESENT_PLAYING);
    coop_present_skip(presentations[1]); coop_present_skip(presentations[2]); phase(COOP_PRESENT_PLAYING);
    CHECK(coop_present_current(presentations[0])->skip == 6);
    coop_present_done(presentations[0], true); coop_present_done(presentations[3], true); phase(COOP_PRESENT_PLAYING);
    CHECK((coop_present_current(presentations[0])->skip | coop_present_current(presentations[0])->done) == 15);
    hold_last = true; coop_present_done(presentations[1], true); coop_present_done(presentations[2], true); phase(COOP_PRESENT_COMPLETE);
    CHECK(!coop_present_settled(presentations[0])); hold_last = false; pump(100); CHECK(coop_present_settled(presentations[0]));
    CHECK(coop_present_encode(coop_present_current(presentations[0]), bytes));
    CHECK(coop_present_message(presentations[1], 0, COOP_PRESENT_VIEW, bytes, sizeof(bytes)));
    bytes[28] ^= 1; CHECK(!coop_present_message(presentations[1], 0, COOP_PRESENT_VIEW, bytes, sizeof(bytes)));
    CHECK(coop_present_begin(presentations[0], COOP_PRESENT_FADE, 0, NULL, (48u << 8) | 1, 2000, now));
    phase(COOP_PRESENT_PREPARING);
    for (unsigned i = 0; i < 4; i++)
        coop_present_ready(presentations[i], true);
    phase(COOP_PRESENT_PLAYING);
    for (unsigned i = 0; i < 3; i++)
        coop_present_done(presentations[i], true);
    phase(COOP_PRESENT_PLAYING);
    coop_present_done(presentations[3], true);
    phase(COOP_PRESENT_COMPLETE);
    CHECK(coop_present_settled(presentations[0]));
    CHECK(coop_present_begin(presentations[0], COOP_PRESENT_SLIDE, 12, NULL, 0, 0, now)); phase(COOP_PRESENT_PREPARING);
    coop_object_write32(ack, 1); CHECK(coop_present_message(presentations[0], 3, COOP_PRESENT_ACK, ack, sizeof(ack)));
    coop_object_write32(ack, 4);
    CHECK(!coop_present_message(presentations[0], 3, COOP_PRESENT_ACK, ack, sizeof(ack)));
    for (unsigned i = 0; i < 4; i++) coop_present_ready(presentations[i], i != 2);
    phase(COOP_PRESENT_FAILED); CHECK(coop_present_current(presentations[0])->failed == 4);
    CHECK(!coop_present_begin(presentations[0], COOP_PRESENT_END, 0, NULL, 0, 0, now));
    for (unsigned i = 0; i < 4; i++) { coop_session_set_handler(sessions[i], NULL, NULL); coop_present_destroy(presentations[i]); coop_session_destroy(sessions[i]); }
    puts("PASS: canonical presentation corpus, four viewers, delayed readiness/completion, collective skip, exact replay and asset failure"); return 0;
}
