#ifndef ARCMP_SESSION_H
#define ARCMP_SESSION_H

#include "coop/identity.h"
#include "coop/character.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COOP_MAX_PLAYERS 4
#define COOP_NAME_SIZE 32
#define COOP_CHAT_SIZE 241
#define COOP_CHAT_HISTORY 64
#define COOP_DEFAULT_PORT 35741
#define COOP_NO_SLOT 255
#define COOP_PROTOCOL_VERSION 64
#define COOP_MESSAGE_CAPACITY 1024

typedef enum CoopPhase {
    COOP_OFFLINE,
    COOP_CONNECTING,
    COOP_LOBBY,
} CoopPhase;

typedef struct CoopPlayer {
    bool connected;
    bool ready;
    uint32_t prototype;
    char name[COOP_NAME_SIZE];
    char character[COOP_NAME_SIZE];
    bool custom;
    CoopCharacter build;
    /* Host-side ownership key. Other clients' keys are not in lobby rosters. */
    uint8_t identity[COOP_IDENTITY_SIZE];
} CoopPlayer;

typedef struct CoopChat {
    uint64_t ordinal;
    uint8_t slot;
    char name[COOP_NAME_SIZE];
    char text[COOP_CHAT_SIZE];
} CoopChat;

typedef struct CoopConfig {
    char name[COOP_NAME_SIZE];
    /* SHA-256 of the canonical content manifest; peers must match exactly. */
    uint8_t content_id[32];
    /* Zero requests a transient identity. The game loads a persistent profile. */
    uint8_t identity[COOP_IDENTITY_SIZE];
} CoopConfig;

typedef struct CoopSession CoopSession;
/* Engine messages use types 16..95. The established connection supplies the
 * sender slot; payloads cannot choose it. Payload storage is valid for the whole
 * callback, including nested native UI polls. Handlers may queue replies, poll,
 * leave or replace the connection; an older poll cannot modify its replacement.
 * Destroy retires immediately and frees storage after outstanding polls return.
 * Returning false disconnects the peer only if its connection is still current. */
typedef bool (*CoopMessageHandler)(void* context, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size);
void coop_session_set_handler(CoopSession* session, CoopMessageHandler handler, void* context);
/* Returns false on backpressure without disconnecting or consuming a sequence.
 * Clients may send only to host slot 0; hosts address remote connection slots. */
bool coop_session_send(CoopSession* session, uint8_t recipient, uint8_t type, const void* payload, size_t size);

/* The caller owns one session on one thread. poll() never waits for the network.
 * now_ms is a monotonic millisecond clock (also permits deterministic timeouts).
 * Socket initialization and cleanup are owned by each session on Windows. */
CoopSession* coop_session_create(const CoopConfig* config);
void coop_session_destroy(CoopSession* session);
void coop_session_leave(CoopSession* session);
/* Freeze application traffic immediately. A launched client's failure is
 * relayed by the host; terminal notices are acknowledged before sockets close.
 * poll() finishes this bounded exchange even though phase is already OFFLINE. */
void coop_session_abort(CoopSession* session, const char* reason);
bool coop_session_failed(const CoopSession* session);
bool coop_session_abort_pending(const CoopSession* session);
bool coop_session_host(CoopSession* session, uint16_t port, uint64_t now_ms);
bool coop_session_join(CoopSession* session, const char* ipv4, uint16_t port, uint64_t now_ms);
void coop_session_poll(CoopSession* session, uint64_t now_ms);
bool coop_session_character(CoopSession* session, uint32_t prototype, const char* name);
/* Shape validation happens here; native creation rules are validated by the
 * host engine before campaign creation. Changing any choice clears readiness. */
bool coop_session_custom_character(CoopSession* session, const CoopCharacter* character);
bool coop_session_ready(CoopSession* session, bool ready);
/* Reserve launched slots/characters/readiness for the original profiles.
 * Returning owners reuse their slot; unknown owners cannot join a campaign.
 * Host changes publish one new roster revision. */
void coop_session_lock(CoopSession* session, bool locked);
bool coop_session_locked(const CoopSession* session);
/* Lobby host removal binds the selected profile, so a replaced connection
 * cannot inherit a stale UI confirmation. Launched owner slots stay reserved. */
bool coop_session_remove(CoopSession* session, uint8_t slot, const uint8_t identity[COOP_IDENTITY_SIZE]);
uint8_t coop_session_reserved(const CoopSession* session);
/* Retry the last client endpoint after a transport loss. Explicit leave and
 * non-transient server rejections revoke this permission. */
bool coop_session_can_retry(const CoopSession* session);
bool coop_session_retry(CoopSession* session, uint64_t now_ms);
/* Explicit diagnostic fault: close only the client's TCP transport, retaining
 * retry eligibility. This sends no leave/roster/engine message. */
bool coop_session_drop_transport(CoopSession* session);
bool coop_session_chat(CoopSession* session, const char* text);
/* Readiness is a prerequisite only; this API does not claim world bootstrap. */
bool coop_session_all_ready(const CoopSession* session);
CoopPhase coop_session_phase(const CoopSession* session);
bool coop_session_is_host(const CoopSession* session);
uint8_t coop_session_local_slot(const CoopSession* session);
uint16_t coop_session_port(const CoopSession* session);
uint32_t coop_session_revision(const CoopSession* session);
uint64_t coop_session_now(const CoopSession* session);
const CoopPlayer* coop_session_players(const CoopSession* session);
const char* coop_session_status(const CoopSession* session);
uint64_t coop_session_chat_count(const CoopSession* session);
const CoopChat* coop_session_chat_at(const CoopSession* session, uint64_t ordinal);

#endif
