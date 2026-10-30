#ifndef ARCMP_CONTROL_STATE_H
#define ARCMP_CONTROL_STATE_H
#include "coop/object_state.h"
#include "coop/session.h"
#include "coop/save_slots.h"
enum { COOP_JOURNEY_WAYPOINTS = 30, COOP_CONTROL_REQUEST_BYTES = 244, COOP_CONTROL_VIEW_BYTES = 254 };
typedef enum CoopControlAction {
    COOP_CONTROL_PAUSE, COOP_CONTROL_RESUME_ALL, COOP_CONTROL_REST,
    COOP_CONTROL_VOTE, COOP_CONTROL_CANCEL, COOP_CONTROL_SAVE, COOP_CONTROL_LOAD, COOP_CONTROL_TRAVEL,
} CoopControlAction;
typedef enum CoopControlPhase {
    COOP_CONTROL_IDLE, COOP_CONTROL_VOTING, COOP_CONTROL_DRAINING,
    COOP_CONTROL_RESTING, COOP_CONTROL_SAVING, COOP_CONTROL_LOADING, COOP_CONTROL_JOURNEY,
} CoopControlPhase;
typedef enum CoopControlStatus {
    COOP_CONTROL_OK, COOP_CONTROL_UNAVAILABLE, COOP_CONTROL_OWNER,
    COOP_CONTROL_BUSY, COOP_CONTROL_DECLINED, COOP_CONTROL_EXPIRED,
    COOP_CONTROL_ENEMIES, COOP_CONTROL_DISTANCE, COOP_CONTROL_LOCATION,
    COOP_CONTROL_INCAPACITATED, COOP_CONTROL_IO, COOP_CONTROL_NO_CHECKPOINT,
    COOP_CONTROL_CONTENT, COOP_CONTROL_OWNERS, COOP_CONTROL_INTERRUPTED, COOP_CONTROL_CHANGED,
} CoopControlStatus;
typedef struct CoopControlRequest {
    uint32_t epoch, sequence, map;
    uint8_t actor[COOP_IDENTITY_SIZE];
    CoopControlAction action;
    uint32_t value, proposal;
    CoopObjectId bed;
    uint32_t checkpoint_fingerprint;
    uint8_t checkpoint_present;
    char description[COOP_CHECKPOINT_DESCRIPTION];
    uint8_t route_count;
    uint32_t route[COOP_JOURNEY_WAYPOINTS];
} CoopControlRequest;
typedef struct CoopControlView {
    uint32_t epoch, revision, proposal, value, completed;
    uint32_t sequences[COOP_MAX_PLAYERS], remaining_ms, world_revision, map;
    uint8_t statuses[COOP_MAX_PLAYERS];
    CoopControlPhase phase;
    CoopControlAction action;
    uint8_t paused, participants, agreed, refused, proposer, notice;
    uint32_t checkpoint_fingerprint;
    uint8_t checkpoint_present;
    char description[COOP_CHECKPOINT_DESCRIPTION];
    uint8_t route_count;
    uint32_t route[COOP_JOURNEY_WAYPOINTS];
} CoopControlView;
bool coop_control_request_encode(const CoopControlRequest* request, uint8_t bytes[COOP_CONTROL_REQUEST_BYTES]);
bool coop_control_request_decode(const void* bytes, size_t size, CoopControlRequest* request);
bool coop_control_view_encode(const CoopControlView* view, uint8_t bytes[COOP_CONTROL_VIEW_BYTES]);
bool coop_control_view_decode(const void* bytes, size_t size, CoopControlView* view);
#endif
