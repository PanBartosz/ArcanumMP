#ifndef ARCMP_PRESENTATION_H
#define ARCMP_PRESENTATION_H
#include "coop/session.h"
enum { COOP_PRESENT_VIEW = 64, COOP_PRESENT_ACK = 65, COOP_PRESENT_BYTES = 288 };
typedef enum CoopPresentKind { COOP_PRESENT_MOVIE,
    COOP_PRESENT_SLIDE,
    COOP_PRESENT_END,
    COOP_PRESENT_FADE } CoopPresentKind;
typedef enum CoopPresentPhase { COOP_PRESENT_PREPARING, COOP_PRESENT_PLAYING, COOP_PRESENT_COMPLETE, COOP_PRESENT_FAILED } CoopPresentPhase;
typedef struct CoopPresentView {
    uint32_t sequence, revision, flags, id, track;
    CoopPresentKind kind; CoopPresentPhase phase;
    uint8_t participants, ready, done, skip, failed;
    char path[260];
} CoopPresentView;
typedef struct CoopPresentation CoopPresentation;
bool coop_present_encode(const CoopPresentView* view, uint8_t bytes[COOP_PRESENT_BYTES]);
bool coop_present_decode(const void* bytes, size_t size, CoopPresentView* view);
CoopPresentation* coop_present_create(CoopSession* session);
void coop_present_destroy(CoopPresentation* presentation);
bool coop_present_begin(CoopPresentation* presentation, CoopPresentKind kind, unsigned id, const char* path, unsigned flags, unsigned track, uint64_t now);
bool coop_present_message(CoopPresentation* presentation, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_present_poll(CoopPresentation* presentation, uint64_t now);
const CoopPresentView* coop_present_current(const CoopPresentation* presentation);
void coop_present_ready(CoopPresentation* presentation, bool success);
void coop_present_done(CoopPresentation* presentation, bool success);
void coop_present_skip(CoopPresentation* presentation);
bool coop_present_active(const CoopPresentation* presentation);
bool coop_present_settled(const CoopPresentation* presentation);
#endif
