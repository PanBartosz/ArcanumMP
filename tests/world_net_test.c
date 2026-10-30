#include "coop/object_state.h"
#include "coop/world.h"
#include "coop/world_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)

/* Engine-independent transport fixture. Actual object application is covered
 * separately by the whole-engine probes; these bytes exercise queue budgets,
 * ordering and acknowledgment without requiring retail assets in CTest. */
struct CoopWorldTracker {
    unsigned captured;
};
static unsigned generation = 1, active, applied[4];
static const size_t batch_size = 600123;
CoopWorldTracker* coop_world_tracker_create(bool separate)
{
    CHECK(separate);
    return calloc(1, sizeof(CoopWorldTracker));
}
void coop_world_tracker_destroy(CoopWorldTracker* tracker) { free(tracker); }
static uint8_t fixture_byte(size_t offset, unsigned version)
{
    if (version == 4) {
        uint32_t n = (uint32_t)offset + 0x9e3779b9u;
        n ^= n >> 16; n *= 0x85ebca6bu; n ^= n >> 13; n *= 0xc2b2ae35u; n ^= n >> 16;
        return (uint8_t)n;
    }
    return (uint8_t)(offset * 37 + (offset >> 8) + version);
}
bool coop_world_capture(CoopWorldTracker* tracker, uint8_t** bytes, size_t* size)
{
    *bytes = NULL;
    *size = 0;
    if (tracker->captured == generation) return true;
    *bytes = malloc(batch_size);
    CHECK(*bytes);
    *size = batch_size;
    for (size_t i = 0; i < batch_size; i++)
        (*bytes)[i] = fixture_byte(i, generation);
    coop_object_write32(*bytes, generation);
    tracker->captured = generation;
    return true;
}
bool coop_world_apply(const void* bytes, size_t size)
{
    const uint8_t* p = bytes;
    CHECK(active > 0 && active < 4 && size == batch_size);
    unsigned version = coop_object_read32(p);
    CHECK(version == applied[active] + 1);
    for (size_t i = 4; i < size; i++)
        CHECK(p[i] == fixture_byte(i, version));
    applied[active] = version;
    return true;
}
unsigned coop_world_deletions(const void* bytes, size_t size)
{
    (void)bytes;
    (void)size;
    return 1;
}
static CoopSession* sessions[4];
static CoopWorldNetwork* worlds[4];
static uint64_t now = 100;
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size)
{
    return coop_world_net_message(context, sender, type, payload, size);
}
static void pump(unsigned rounds, unsigned participants)
{
    while (rounds--) {
        for (unsigned i = 0; i < participants; i++) {
            active = i;
            coop_session_poll(sessions[i], now);
            if (worlds[i]) CHECK(coop_world_net_poll(worlds[i], now));
        }
        now++;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 };
        nanosleep(&delay, NULL);
#endif
    }
}
int main(void)
{
    for (unsigned i = 0; i < 4; i++) {
        CoopConfig config = { 0 };
        snprintf(config.name, sizeof(config.name), "World %u", i);
        sessions[i] = coop_session_create(&config);
        CHECK(sessions[i]);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    for (unsigned i = 1; i < 4; i++)
        CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(50, 4);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_session_phase(sessions[i]) == COOP_LOBBY);
        worlds[i] = coop_world_net_create(sessions[i], 42);
        CHECK(worlds[i]);
        coop_session_set_handler(sessions[i], message, worlds[i]);
    }
    /* Saturate the host's bounded queues before any socket is flushed. */
    for (unsigned i = 0; i < 200; i++)
        CHECK(coop_world_net_poll(worlds[0], now));
    pump(150, 3); // The last client has not received/applied/acknowledged.
    CHECK(applied[1] == 1 && applied[2] == 1 && applied[3] == 0);
    CHECK(!coop_world_net_initialized(worlds[0]) && !coop_world_net_initialized(worlds[3]));
    CHECK(coop_world_net_initialized(worlds[1]) && coop_world_net_initialized(worlds[2]));
    CHECK(coop_world_net_revision(worlds[0]) == 1 && coop_world_net_bytes(worlds[0]) == 0);
    generation = 2;
    pump(50, 3);
    CHECK(coop_world_net_revision(worlds[0]) == 1); // Never skip an unacknowledged batch.
    for (unsigned i = 0; i < 1000 && applied[3] != 2; i++)
        pump(1, 4);
    CHECK(applied[1] == 2 && applied[2] == 2 && applied[3] == 2);
    for (unsigned i=0;i<4;i++) CHECK(coop_world_net_initialized(worlds[i]));
    uint64_t next_capture = coop_world_net_request_capture(worlds[0]);
    for (unsigned i = 0; i < 1000 && coop_world_net_captures(worlds[0]) < next_capture; i++) pump(1, 4);
    CHECK(coop_world_net_captures(worlds[0]) >= next_capture);
    for (unsigned i = 1; i < 4; i++) CHECK(coop_world_net_applied_revision(worlds[i]) == 2);
    coop_world_net_settle(worlds[0]);
    for (unsigned i = 0; i < 1000 && !coop_world_net_settled(worlds[0]); i++)
        pump(1, 4);
    CHECK(coop_world_net_settled(worlds[0]));
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_world_net_revision(worlds[i]) == 2);
        CHECK(coop_world_net_bytes(worlds[i]) == 2 * batch_size);
        CHECK(coop_world_net_deletions(worlds[i]) == 2);
        CHECK(coop_world_net_wire_bytes(worlds[i]) < coop_world_net_bytes(worlds[i])/10);
    }
    /* A native/action completion racing an earlier settle still gets its
     * promised capture. Coalesced requests wait for that same capture, and
     * all peers must ACK its changed world before settling again. */
    generation = 3;
    next_capture = coop_world_net_request_capture(worlds[0]);
    CHECK(!coop_world_net_settled(worlds[0]));
    CHECK(coop_world_net_request_capture(worlds[0]) == next_capture);
    for (unsigned i = 0; i < 1000 && !coop_world_net_settled(worlds[0]); i++) pump(1, 4);
    CHECK(coop_world_net_settled(worlds[0]) && coop_world_net_captures(worlds[0]) >= next_capture);
    for (unsigned i = 1; i < 4; i++) CHECK(applied[i] == 3 && coop_world_net_applied_revision(worlds[i]) == 3);
    uint64_t compressed_bytes = coop_world_net_wire_bytes(worlds[0]);
    generation = 4; // High-entropy state must use the unchanged raw fallback.
    coop_world_net_request_capture(worlds[0]);
    for (unsigned i = 0; i < 2000 && (!coop_world_net_settled(worlds[0]) || applied[3]!=4); i++) pump(1,4);
    CHECK(coop_world_net_settled(worlds[0]));
    for (unsigned i=0;i<4;i++) {
        CHECK(applied[i] == 4 || i == 0);
        CHECK(coop_world_net_wire_bytes(worlds[i]) == compressed_bytes+batch_size);
    }
    uint8_t begin[20] = { 0 }, chunk[32] = { 0 };
    coop_object_write32(begin, 99);
    coop_object_write32(begin + 4, 1);
    coop_object_write32(begin + 8, 20);
    coop_object_write32(begin + 16, 20);
    CoopWorldNetwork* invalid = coop_world_net_create(sessions[1], 99);
    CHECK(invalid);
    CHECK(!coop_world_net_message(invalid, 2, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    CHECK(!coop_world_net_message(worlds[0], 1, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    coop_object_write32(begin + 8, COOP_WORLD_LIMIT + 1);
    CHECK(!coop_world_net_message(invalid, 0, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    coop_object_write32(begin + 8, 20);
    coop_object_write32(begin + 16, COOP_WORLD_LIMIT+1);
    CHECK(!coop_world_net_message(invalid,0,COOP_WORLD_BEGIN,begin,sizeof(begin)));
    coop_object_write32(begin + 16,20);
    CHECK(coop_world_net_message(invalid, 0, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    CHECK(!coop_world_net_message(invalid, 0, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    coop_object_write32(chunk, 99);
    coop_object_write32(chunk + 4, 1);
    coop_object_write32(chunk + 8, 1);
    CHECK(!coop_world_net_message(invalid, 0, COOP_WORLD_CHUNK, chunk, sizeof(chunk)));
    coop_object_write32(chunk + 8, 0);
    CHECK(coop_world_net_message(invalid, 0, COOP_WORLD_CHUNK, chunk, sizeof(chunk)));
    CHECK(!coop_world_net_poll(invalid, 1)); // CRC failure never reaches engine application.
    coop_world_net_destroy(invalid);
    invalid = coop_world_net_create(sessions[1], 99);
    CHECK(invalid && coop_world_net_message(invalid, 0, COOP_WORLD_BEGIN, begin, sizeof(begin)));
    CHECK(!coop_world_net_poll(invalid, 30001));
    coop_world_net_destroy(invalid);
    invalid = coop_world_net_create(sessions[1],99);
    coop_object_write32(begin+8,20); coop_object_write32(begin+16,200);
    CHECK(invalid && coop_world_net_message(invalid,0,COOP_WORLD_BEGIN,begin,sizeof(begin)));
    CHECK(coop_world_net_message(invalid,0,COOP_WORLD_CHUNK,chunk,sizeof(chunk)));
    CHECK(!coop_world_net_poll(invalid,1)); // Invalid deflate stream never reaches apply.
    coop_world_net_destroy(invalid);
    for (unsigned i = 0; i < 4; i++) {
        coop_session_set_handler(sessions[i], NULL, NULL);
        coop_world_net_destroy(worlds[i]);
        coop_session_destroy(sessions[i]);
    }
    puts("PASS: ordered world batches, queue backpressure, delayed acknowledgments, settle barrier and malformed/timeout rejection");
    return 0;
}
