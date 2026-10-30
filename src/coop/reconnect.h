#ifndef ARCMP_RECONNECT_H
#define ARCMP_RECONNECT_H
#include "coop/session.h"
enum { COOP_RECONNECT_VIEW = 68, COOP_RECONNECT_ACK = 69, COOP_RECONNECT_BYTES = 16, COOP_RECONNECT_GRACE = 120000 };
enum { COOP_RECONNECT_WAIT = 1, COOP_RECONNECT_SYNC = 2 };
typedef struct CoopReconnectView {
    uint32_t epoch, revision, remaining_ms;
    uint8_t participants, connected, phase;
} CoopReconnectView;
typedef struct CoopReconnect CoopReconnect;
bool coop_reconnect_encode(const CoopReconnectView*, uint8_t out[COOP_RECONNECT_BYTES]);
bool coop_reconnect_decode(const void*, size_t, CoopReconnectView*);
CoopReconnect* coop_reconnect_create(CoopSession*);
void coop_reconnect_destroy(CoopReconnect*);
bool coop_reconnect_begin(CoopReconnect*, uint32_t epoch, uint8_t participants, uint64_t now);
bool coop_reconnect_message(CoopReconnect*, uint8_t sender, uint8_t type, const uint8_t*, size_t);
/* prepared confirms the native adapter has retired old services and installed
 * a fresh snapshot receiver. No native work occurs inside message(). */
bool coop_reconnect_poll(CoopReconnect*, uint64_t now, bool prepared);
const CoopReconnectView* coop_reconnect_current(const CoopReconnect*);
bool coop_reconnect_active(const CoopReconnect*);
bool coop_reconnect_ready(const CoopReconnect*);
void coop_reconnect_end(CoopReconnect*);
#endif
