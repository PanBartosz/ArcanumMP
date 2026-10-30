#ifndef ARCMP_CONTROLS_H
#define ARCMP_CONTROLS_H
#include "coop/control_state.h"
enum { COOP_CONTROL_REQUEST = 61, COOP_CONTROL_VIEW = 62, COOP_CONTROL_ACK = 63 };
typedef struct CoopControls CoopControls;
bool coop_controls_restore_pauses(CoopControls*, uint8_t pauses);
bool coop_controls_restore_interrupted(CoopControls*, const CoopControlView* previous);
typedef unsigned (*CoopControlValidate)(void* context, uint8_t sender, const CoopControlRequest* request);
typedef unsigned (*CoopControlBegin)(void* context, uint8_t sender, const CoopControlRequest* request);
CoopControls* coop_controls_create(CoopSession* session, uint32_t epoch, uint32_t map, uint64_t now, CoopControlValidate validate, CoopControlBegin begin, void* context);
void coop_controls_destroy(CoopControls* controls);
bool coop_controls_message(CoopControls* controls, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_controls_poll(CoopControls* controls, uint64_t now, bool ready);
bool coop_controls_submit(CoopControls* controls, const CoopControlRequest* request);
const CoopControlView* coop_controls_current(const CoopControls* controls);
bool coop_controls_pending(const CoopControls* controls);
/* Host runtime updates progress and completes only after the shared reload. */
bool coop_controls_progress(CoopControls* controls, CoopControlPhase phase, unsigned hours);
bool coop_controls_complete(CoopControls* controls, unsigned status, uint32_t epoch, uint32_t map);
bool coop_controls_context(CoopControls* controls, uint32_t epoch, uint32_t map);
bool coop_controls_cancelled(const CoopControls* controls);
uint32_t coop_controls_last_result(const CoopControls* controls, unsigned* status);
unsigned coop_controls_executed(const CoopControls* controls);
#endif
