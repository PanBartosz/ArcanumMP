#include "coop/controls.h"
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
static CoopControls* controls[4];
static uint64_t now = 100;
static unsigned begins, validations;
static bool hold_ack, ready = true;
static unsigned validate(void* context, uint8_t sender, const CoopControlRequest* r)
{
    CHECK(context == &begins); validations++;
    return r->actor[0] != sender + 1 ? COOP_CONTROL_OWNER : r->map != 1 ? COOP_CONTROL_LOCATION : COOP_CONTROL_OK;
}
static unsigned begin(void* context, uint8_t sender, const CoopControlRequest* r)
{
    CHECK(context == &begins && sender < 4); begins++;
    CHECK(coop_controls_progress(controls[0], r->action == COOP_CONTROL_REST ? COOP_CONTROL_RESTING
        : r->action == COOP_CONTROL_TRAVEL ? COOP_CONTROL_JOURNEY : r->action == COOP_CONTROL_SAVE ? COOP_CONTROL_SAVING : COOP_CONTROL_LOADING, 0));
    if (r->action == COOP_CONTROL_TRAVEL) CHECK(r->route_count == 3
        && r->route[0] == 0x01000200 && r->route[1] == 0x01000210 && r->route[2] == r->value);
    return COOP_CONTROL_OK;
}
static bool message(void* context, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size)
{
    return coop_controls_message(context, sender, type, bytes, size);
}
static void pump(unsigned rounds, unsigned elapsed)
{
    while (rounds--) {
        for (unsigned i = 0; i < 4; i++) {
            coop_session_poll(sessions[i], now);
            CHECK(coop_session_phase(sessions[i]) == COOP_LOBBY || !controls[i]);
            if (controls[i] && !(hold_ack && i == 3)) CHECK(coop_controls_poll(controls[i], now, ready));
        }
        now += elapsed;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay = { 0, 1000000 }; nanosleep(&delay, NULL);
#endif
    }
}
static CoopControlRequest submit(unsigned owner, CoopControlAction action, unsigned value, uint32_t proposal)
{
    CoopControlRequest r = { .map = 1, .action = action, .value = value, .proposal = proposal };
    r.actor[0] = (uint8_t)(owner + 1);
    r.epoch = coop_controls_current(controls[owner])->epoch;
    r.sequence = coop_controls_current(controls[owner])->sequences[owner] + 1;
    CHECK(coop_controls_submit(controls[owner], &r));
    CHECK(!coop_controls_submit(controls[owner], &r));
    pump(80, 10);
    CHECK(!coop_controls_pending(controls[owner]));
    return r;
}
static void result(unsigned owner, unsigned status)
{
    unsigned received = 255;
    CHECK(coop_controls_last_result(controls[owner], &received) && received == status);
}
static void all_phase(CoopControlPhase phase)
{
    pump(80, 10);
    for (unsigned i = 0; i < 4; i++) CHECK(coop_controls_current(controls[i])->phase == phase);
}
static void complete(unsigned status)
{
    CHECK(coop_controls_complete(controls[0], status, coop_controls_current(controls[0])->epoch + 1, 1));
    all_phase(COOP_CONTROL_IDLE);
    for (unsigned i = 0; i < 4; i++) CHECK(coop_controls_current(controls[i])->notice == status);
}
int main(void)
{
    for (unsigned i = 0; i < 4; i++) {
        CoopConfig config = { 0 }; snprintf(config.name, sizeof(config.name), "Control player %u", i);
        sessions[i] = coop_session_create(&config); CHECK(sessions[i]);
    }
    CHECK(coop_session_host(sessions[0], 0, now));
    for (unsigned i = 1; i < 4; i++) CHECK(coop_session_join(sessions[i], "127.0.0.1", coop_session_port(sessions[0]), now));
    pump(100, 1);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_session_local_slot(sessions[i]) == i);
        controls[i] = coop_controls_create(sessions[i], 7, 1, now, validate, begin, &begins); CHECK(controls[i]);
        coop_session_set_handler(sessions[i], message, controls[i]);
    }
    all_phase(COOP_CONTROL_IDLE);
    submit(1, COOP_CONTROL_PAUSE, 1, 0); submit(2, COOP_CONTROL_PAUSE, 1, 0);
    CHECK(coop_controls_current(controls[3])->paused == 6);
    submit(1, COOP_CONTROL_PAUSE, 0, 0); CHECK(coop_controls_current(controls[3])->paused == 4);
    submit(2, COOP_CONTROL_RESUME_ALL, 0, 0); result(2, COOP_CONTROL_OWNER);
    CHECK(coop_controls_current(controls[0])->paused == 4);
    CoopControlRequest replay = submit(0, COOP_CONTROL_RESUME_ALL, 0, 0);
    CHECK(!coop_controls_current(controls[3])->paused);
    uint8_t bytes[COOP_CONTROL_REQUEST_BYTES]; CHECK(coop_control_request_encode(&replay, bytes));
    unsigned before = validations; CHECK(coop_controls_message(controls[0], 0, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)));
    pump(80, 10); CHECK(validations == before && !begins);
    bytes[12] ^= 16; CHECK(!coop_controls_message(controls[0], 0, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)));
    submit(1, COOP_CONTROL_REST, 3, 0); all_phase(COOP_CONTROL_VOTING);
    uint32_t proposal = coop_controls_current(controls[0])->proposal;
    submit(2, COOP_CONTROL_VOTE, 1, proposal + 1); result(2, COOP_CONTROL_UNAVAILABLE);
    submit(2, COOP_CONTROL_VOTE, 0, proposal); all_phase(COOP_CONTROL_IDLE);
    CHECK(coop_controls_current(controls[3])->notice == COOP_CONTROL_DECLINED && !begins);
    submit(3, COOP_CONTROL_REST, 0, 0); proposal = coop_controls_current(controls[0])->proposal;
    hold_ack = true;
    for (unsigned i = 0; i < 3; i++) submit(i, COOP_CONTROL_VOTE, 1, proposal);
    all_phase(COOP_CONTROL_DRAINING); CHECK(!begins);
    hold_ack = false; all_phase(COOP_CONTROL_RESTING); CHECK(begins == 1);
    CHECK(coop_controls_progress(controls[0], COOP_CONTROL_RESTING, 2));
    submit(2, COOP_CONTROL_CANCEL, 0, proposal); CHECK(coop_controls_cancelled(controls[0]));
    complete(COOP_CONTROL_INTERRUPTED);
    submit(0, COOP_CONTROL_LOAD, 0, 0); all_phase(COOP_CONTROL_VOTING);
    pump(310, 100); all_phase(COOP_CONTROL_IDLE);
    CHECK(coop_controls_current(controls[0])->notice == COOP_CONTROL_EXPIRED && begins == 1);
    hold_ack = true; replay = submit(1, COOP_CONTROL_SAVE, 0, 0);
    all_phase(COOP_CONTROL_DRAINING); CHECK(begins == 1);
    hold_ack = false; all_phase(COOP_CONTROL_SAVING); CHECK(begins == 2);
    complete(COOP_CONTROL_OK);
    CHECK(coop_control_request_encode(&replay, bytes)); before = validations;
    CHECK(coop_controls_message(controls[0], 1, COOP_CONTROL_REQUEST, bytes, sizeof(bytes)));
    uint8_t ack[8]; coop_object_write32(ack, replay.epoch); coop_object_write32(ack + 4, 1);
    CHECK(coop_controls_message(controls[0], 1, COOP_CONTROL_ACK, ack, sizeof(ack)));
    pump(80, 10); CHECK(begins == 2 && validations == before);
    submit(2, COOP_CONTROL_LOAD, 0, 0); proposal = coop_controls_current(controls[0])->proposal;
    for (unsigned i = 0; i < 4; i++) if (i != 2) submit(i, COOP_CONTROL_VOTE, 1, proposal);
    all_phase(COOP_CONTROL_LOADING); CHECK(begins == 3); complete(COOP_CONTROL_OK);
    coop_object_write32(ack, 7); CHECK(coop_controls_message(controls[0], 1, COOP_CONTROL_ACK, ack, sizeof(ack)));
    coop_object_write32(ack, 11); CHECK(!coop_controls_message(controls[0], 1, COOP_CONTROL_ACK, ack, sizeof(ack)));
    ready = false; submit(3, COOP_CONTROL_PAUSE, 1, 0); result(3, COOP_CONTROL_UNAVAILABLE);
    CHECK(!coop_controls_current(controls[0])->paused && coop_controls_executed(controls[0]) == 3);
    CoopControlView interrupted={.action=COOP_CONTROL_REST,.value=4,.completed=2};
    CHECK(coop_controls_restore_interrupted(controls[0],&interrupted)); pump(40,1);
    for (unsigned i=0;i<4;i++) CHECK(coop_controls_current(controls[i])->notice==COOP_CONTROL_INTERRUPTED
        && coop_controls_current(controls[i])->action==COOP_CONTROL_REST && coop_controls_current(controls[i])->completed==2);
    interrupted.completed=169; CHECK(!coop_controls_restore_interrupted(controls[0],&interrupted));
    interrupted=(CoopControlView){.action=COOP_CONTROL_TRAVEL,.completed=9};
    CHECK(!coop_controls_restore_interrupted(controls[1],&interrupted));
    CHECK(coop_controls_restore_interrupted(controls[0],&interrupted)); pump(40,1);
    CHECK(coop_controls_current(controls[3])->action==COOP_CONTROL_TRAVEL && coop_controls_current(controls[3])->completed==9);
    interrupted.action=COOP_CONTROL_VOTE; CHECK(!coop_controls_restore_interrupted(controls[0],&interrupted));
    ready = true;
    CoopControlRequest journey = { .map = 1, .action = COOP_CONTROL_TRAVEL, .value = 0x01200210,
        .actor = { 3 }, .route_count = 3, .route = { 0x01000200, 0x01000210, 0x01200210 } };
    CHECK(coop_controls_submit(controls[2], &journey)); all_phase(COOP_CONTROL_VOTING);
    proposal = coop_controls_current(controls[0])->proposal;
    for (unsigned i = 0; i < 4; i++) {
        const CoopControlView* route = coop_controls_current(controls[i]);
        CHECK(route->route_count == 3 && !memcmp(route->route, journey.route, sizeof(route->route)));
        if (i != 2) submit(i, COOP_CONTROL_VOTE, 1, proposal);
    }
    all_phase(COOP_CONTROL_JOURNEY); CHECK(begins == 4);
    CoopControlView retained = *coop_controls_current(controls[0]);
    submit(1, COOP_CONTROL_CANCEL, 0, proposal); CHECK(coop_controls_cancelled(controls[0]));
    complete(COOP_CONTROL_INTERRUPTED);
    CHECK(coop_controls_restore_interrupted(controls[0], &retained)); pump(40, 1);
    CHECK(coop_controls_current(controls[3])->route_count == 3
        && !memcmp(coop_controls_current(controls[3])->route, journey.route, sizeof(journey.route)));
    for (unsigned i = 0; i < 4; i++) {
        CHECK(coop_controls_current(controls[i])->epoch == 11);
        coop_session_set_handler(sessions[i], NULL, NULL); coop_controls_destroy(controls[i]); coop_session_destroy(sessions[i]);
    }
    puts("PASS: four-owner pause holds, host resume, unanimous rest/load, decline/expiry/cancel, delayed ACK save barrier and retired-epoch replay");
    return 0;
}
