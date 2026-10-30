#ifndef ARCMP_WORLD_NET_H
#define ARCMP_WORLD_NET_H
#include "coop/session.h"
typedef struct CoopWorldNetwork CoopWorldNetwork;
enum { COOP_WORLD_BEGIN = 32,
    COOP_WORLD_CHUNK,
    COOP_WORLD_ACK };
CoopWorldNetwork* coop_world_net_create(CoopSession* session, uint32_t epoch);
void coop_world_net_destroy(CoopWorldNetwork* network);
void coop_world_net_retire(CoopWorldNetwork* network);
bool coop_world_net_retired(const CoopWorldNetwork* network);
bool coop_world_net_message(CoopWorldNetwork* network, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size);
bool coop_world_net_poll(CoopWorldNetwork* network, uint64_t now_ms);
void coop_world_net_settle(CoopWorldNetwork* network);
bool coop_world_net_settled(const CoopWorldNetwork* network);
/* First complete world state applied locally, or acknowledged by every peer. */
bool coop_world_net_initialized(const CoopWorldNetwork* network);
uint32_t coop_world_net_revision(const CoopWorldNetwork* network);
/* Item/action receipts wait for a capture after execution, and replicas wait
 * for application rather than merely receiving the batch's BEGIN frame. */
uint32_t coop_world_net_applied_revision(const CoopWorldNetwork* network);
uint64_t coop_world_net_captures(const CoopWorldNetwork* network);
uint64_t coop_world_net_request_capture(CoopWorldNetwork* network);
uint64_t coop_world_net_bytes(const CoopWorldNetwork* network);
/* Payload bytes after optional transport compression, excluding frame headers.
 * bytes() retains its original canonical, uncompressed-state meaning. */
uint64_t coop_world_net_wire_bytes(const CoopWorldNetwork* network);
uint64_t coop_world_net_deletions(const CoopWorldNetwork* network);
#endif
