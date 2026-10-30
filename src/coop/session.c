#include "coop/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NetSocket;
#define BAD_SOCKET INVALID_SOCKET
#define socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetSocket;
#define BAD_SOCKET (-1)
#define socket_close close
#endif

enum { HEADER_SIZE = 16,
    MAX_PAYLOAD = COOP_MESSAGE_CAPACITY,
    BUFFER_SIZE = 32768,
    PLAYER_WIRE_SIZE = 71 + COOP_CHARACTER_BYTES,
    ROSTER_SIZE = 7 + COOP_MAX_PLAYERS * PLAYER_WIRE_SIZE };
enum MessageType { HELLO = 1,
    ROSTER,
    CHAT_REQUEST,
    CHAT,
    READY,
    CHARACTER,
    PING,
    PONG,
    ERROR_TEXT,
    FAILURE_REQUEST,
    FAILURE_NOTICE,
    FAILURE_ACK };

typedef struct Peer {
    NetSocket socket;
    uint64_t generation;
    bool connecting;
    bool closing;
    bool terminal_sent, terminal_ack;
    uint8_t slot;
    uint32_t send_sequence;
    uint32_t receive_sequence;
    uint64_t opened_at;
    uint64_t received_at;
    uint64_t ping_at;
    uint64_t chat_window;
    unsigned chat_count;
    size_t input_size;
    size_t output_size;
    uint8_t input[BUFFER_SIZE];
    uint8_t output[BUFFER_SIZE];
} Peer;

struct CoopSession {
    CoopConfig config;
    CoopPhase phase;
    bool host;
    bool locked;
    uint8_t reserved, reserved_ready;
    bool retry_allowed;
    char retry_address[16];
    uint16_t retry_port;
    NetSocket listener;
    Peer peers[COOP_MAX_PLAYERS];
    CoopPlayer players[COOP_MAX_PLAYERS];
    uint8_t local_slot;
    uint16_t port;
    uint32_t revision;
    uint64_t now;
    uint64_t chat_count;
    CoopChat chat[COOP_CHAT_HISTORY];
    char status[160];
    CoopMessageHandler handler;
    void* handler_context;
    uint64_t generation;
    unsigned polling;
    bool destroying;
    bool failed, terminating, terminal_received;
    uint64_t terminal_at;
};
static bool queue_frame(Peer* peer, unsigned type, const void* payload, size_t size);
static bool flush(Peer* peer);

static uint32_t read_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void write_u32(uint8_t* p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

/* The retail UI font is byte-oriented. Reject control bytes and ambiguous text
 * rather than permitting framing/log injection or silently truncating names. */
static bool valid_text(const char* text, size_t capacity)
{
    bool non_space = false;
    if (text == NULL) return false;
    for (size_t i = 0; i < capacity; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0) return non_space;
        if (c < 32 || c > 126) return false;
        if (c != ' ') non_space = true;
    }
    return false;
}

static void copy_text(char* destination, size_t capacity, const char* source)
{
    size_t length = 0;
    while (length + 1 < capacity && source[length]) length++;
    memmove(destination,source,length);
    destination[length] = 0;
}
static void status_set(CoopSession* s, const char* text)
{
    copy_text(s->status,sizeof(s->status),text);
}

static bool would_block(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
#endif
}

static bool socket_setup(NetSocket socket)
{
    int one = 1;
#ifndef _WIN32
    if (socket >= FD_SETSIZE) return false;
#endif
#ifdef _WIN32
    unsigned long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) return false;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) return false;
#endif
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return true;
}

static void peer_clear(Peer* peer)
{
    uint64_t generation = peer->generation + 1;
    if (peer->socket != BAD_SOCKET) socket_close(peer->socket);
    memset(peer, 0, sizeof(*peer));
    peer->generation = generation;
    peer->socket = BAD_SOCKET;
    peer->slot = COOP_NO_SLOT;
}

static void connections_clear(CoopSession* s)
{
    s->generation++;
    if (s->listener != BAD_SOCKET) socket_close(s->listener);
    s->listener = BAD_SOCKET;
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++)
        peer_clear(&s->peers[i]);
    memset(s->players, 0, sizeof(s->players));
    s->phase = COOP_OFFLINE;
    s->host = false;
    s->local_slot = COOP_NO_SLOT;
    s->port = 0;
    s->terminating = s->terminal_received = false;
}

CoopSession* coop_session_create(const CoopConfig* config)
{
    if (config == NULL || !valid_text(config->name, sizeof(config->name))) return NULL;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return NULL;
#endif
    CoopSession* s = calloc(1, sizeof(*s));
    if (s == NULL) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }
    s->config = *config;
    if (!coop_identity_valid(s->config.identity) && !coop_identity_generate(s->config.identity)) {
        free(s);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }
    s->listener = BAD_SOCKET;
    s->local_slot = COOP_NO_SLOT;
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        s->peers[i].socket = BAD_SOCKET;
        s->peers[i].slot = COOP_NO_SLOT;
    }
    status_set(s, "Choose Host or Join to assemble your party.");
    return s;
}

static void session_free(CoopSession* s)
{
    free(s);
#ifdef _WIN32
    WSACleanup();
#endif
}

void coop_session_destroy(CoopSession* s)
{
    if (!s || s->destroying) return;
    s->destroying = true;
    connections_clear(s);
    /* Native message handlers may pump UI and close the party. Keep their
     * borrowed session storage alive until every nested poll has returned. */
    if (!s->polling) session_free(s);
}

void coop_session_leave(CoopSession* s)
{
    /* Tell connected clients that this was intentional. A dead process or
     * broken transport cannot send this terminal reason. */
    if (s->host) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        Peer* peer = &s->peers[i];
        if (peer->slot != COOP_NO_SLOT && !peer->closing) {
            static const char reason[] = "The host ended this party. Recover the saved party to continue.";
            queue_frame(peer, ERROR_TEXT, reason, sizeof(reason));
            flush(peer);
        }
    }
    s->retry_allowed = false;
    connections_clear(s);
    status_set(s, "Disconnected.");
}

static void abort_session(CoopSession* s, const char* reason, bool host_notice)
{
    if (!s || s->destroying || s->failed) return;
    char message[sizeof(s->status)] = { 0 };
    snprintf(message, sizeof(message), "%s", reason ? reason : "The campaign could not continue. Recover the saved party.");
    if (!valid_text(message, sizeof(message)))
        snprintf(message, sizeof(message), "The campaign could not continue. Recover the saved party.");
    bool connected = s->phase == COOP_LOBBY;
    s->failed = true;
    s->retry_allowed = false;
    status_set(s, message);
    if (!connected || (!s->host && !s->locked && !host_notice)) { connections_clear(s); return; }
    s->terminating = true;
    /* Native loading can run between polls. Start the deadline when transport
     * resumes, using its fresh monotonic clock rather than the last game tick. */
    s->terminal_at = UINT64_MAX;
    s->phase = COOP_OFFLINE;
    if (s->listener != BAD_SOCKET) socket_close(s->listener);
    s->listener = BAD_SOCKET;
}

void coop_session_abort(CoopSession* s, const char* reason) { abort_session(s, reason, false); }
bool coop_session_failed(const CoopSession* s) { return s && s->failed; }
bool coop_session_abort_pending(const CoopSession* s) { return s && s->terminating; }

static void terminal_output(CoopSession* s, Peer* peer)
{
    unsigned type;
    size_t size;
    if (!s->host && s->terminal_received) {
        if (peer->terminal_ack) return;
        type = FAILURE_ACK; size = 0;
    } else {
        if (peer->terminal_sent) return;
        type = s->host ? FAILURE_NOTICE : FAILURE_REQUEST;
        size = strlen(s->status) + 1;
    }
    /* Preserve bytes already queued, including partially sent frames. Retry
     * after they drain instead of treating output backpressure as a rejection. */
    if (peer->output_size + HEADER_SIZE + size > BUFFER_SIZE) return;
    if (queue_frame(peer, type, s->status, size)) {
        if (type == FAILURE_ACK) peer->terminal_ack = true;
        else peer->terminal_sent = true;
    }
}

static bool queue_frame(Peer* peer, unsigned type, const void* payload, size_t size)
{
    if (peer->socket == BAD_SOCKET || peer->closing) return false;
    if (size > MAX_PAYLOAD || peer->output_size + HEADER_SIZE + size > BUFFER_SIZE) {
        peer->closing = true;
        return false;
    }
    uint8_t* out = peer->output + peer->output_size;
    memcpy(out, "ACMP", 4);
    out[4] = 0;
    out[5] = COOP_PROTOCOL_VERSION;
    out[6] = 0;
    out[7] = (uint8_t)type;
    write_u32(out + 8, (uint32_t)size);
    write_u32(out + 12, ++peer->send_sequence);
    if (size != 0) memcpy(out + HEADER_SIZE, payload, size);
    peer->output_size += HEADER_SIZE + size;
    return true;
}

void coop_session_set_handler(CoopSession* s, CoopMessageHandler handler, void* context)
{
    s->handler = handler;
    s->handler_context = context;
}

bool coop_session_send(CoopSession* s, uint8_t recipient, uint8_t type, const void* payload, size_t size)
{
    if (s->phase != COOP_LOBBY || type < 16 || type > 95 || size > MAX_PAYLOAD
        || (size && !payload) || recipient >= COOP_MAX_PLAYERS || recipient == s->local_slot
        || !s->players[recipient].connected || (!s->host && recipient != 0)) return false;
    Peer* peer = NULL;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (s->peers[i].socket != BAD_SOCKET && s->peers[i].slot == recipient) {
            peer = &s->peers[i];
            break;
        }
    }
    if (!peer || peer->connecting || peer->closing || peer->output_size + HEADER_SIZE + size > BUFFER_SIZE - 2048) return false;
    return queue_frame(peer, type, payload, size);
}

static void reject_peer(Peer* peer, const char* reason)
{
    queue_frame(peer, ERROR_TEXT, reason, strlen(reason) + 1);
    peer->closing = true;
}

static void roster_send(CoopSession* s)
{
    uint8_t payload[ROSTER_SIZE] = { 0 };
    write_u32(payload + 1, s->revision);
    payload[5] = s->locked; payload[6] = s->reserved;
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        const CoopPlayer* p = &s->players[i];
        uint8_t* out = payload + 7 + i * PLAYER_WIRE_SIZE;
        out[0] = p->connected;
        out[1] = p->ready;
        write_u32(out + 2, p->prototype);
        memcpy(out + 6, p->name, COOP_NAME_SIZE);
        memcpy(out + 38, p->character, COOP_NAME_SIZE);
        out[70] = p->custom;
        if (p->custom) coop_character_encode(&p->build, out + 71);
    }
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        Peer* peer = &s->peers[i];
        if (peer->slot != COOP_NO_SLOT && !peer->closing) {
            payload[0] = peer->slot;
            queue_frame(peer, ROSTER, payload, sizeof(payload));
        }
    }
}

static void roster_changed(CoopSession* s, bool invalidate_ready)
{
    if (invalidate_ready) {
        for (size_t i = 0; i < COOP_MAX_PLAYERS; i++)
            s->players[i].ready = false;
    }
    s->revision++;
    roster_send(s);
}

static void chat_add(CoopSession* s, uint8_t slot, const char* name, const char* text)
{
    CoopChat* chat = &s->chat[s->chat_count % COOP_CHAT_HISTORY];
    chat->ordinal = s->chat_count++;
    chat->slot = slot;
    copy_text(chat->name,sizeof(chat->name),name);
    copy_text(chat->text,sizeof(chat->text),text);
}

static void chat_broadcast(CoopSession* s, uint8_t slot, const char* text)
{
    uint8_t payload[1 + COOP_NAME_SIZE + COOP_CHAT_SIZE] = { 0 };
    payload[0] = slot;
    memcpy(payload + 1, s->players[slot].name, COOP_NAME_SIZE);
    size_t size = strlen(text) + 1;
    memcpy(payload + 1 + COOP_NAME_SIZE, text, size);
    chat_add(s, slot, s->players[slot].name, text);
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (s->peers[i].slot != COOP_NO_SLOT) {
            queue_frame(&s->peers[i], CHAT, payload, 1 + COOP_NAME_SIZE + size);
        }
    }
}

static void chat_replay(CoopSession* s, Peer* peer)
{
    uint64_t start = s->chat_count > COOP_CHAT_HISTORY ? s->chat_count - COOP_CHAT_HISTORY : 0;
    for (uint64_t i = start; i < s->chat_count; i++) {
        const CoopChat* entry = &s->chat[i % COOP_CHAT_HISTORY];
        uint8_t payload[1 + COOP_NAME_SIZE + COOP_CHAT_SIZE] = { 0 };
        payload[0] = entry->slot;
        memcpy(payload + 1, entry->name, COOP_NAME_SIZE);
        size_t size = strlen(entry->text) + 1;
        memcpy(payload + 1 + COOP_NAME_SIZE, entry->text, size);
        queue_frame(peer, CHAT, payload, 1 + COOP_NAME_SIZE + size);
    }
}

static void begin_session(CoopSession* s, uint64_t now)
{
    s->failed = false;
    s->locked = false;
    s->reserved = s->reserved_ready = 0;
    connections_clear(s);
    s->now = now;
    s->revision = 0;
    s->chat_count = 0;
}

bool coop_session_host(CoopSession* s, uint16_t port, uint64_t now)
{
    if (!s || s->destroying) return false;
    s->retry_allowed = false;
    begin_session(s, now);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    NetSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == BAD_SOCKET) {
        status_set(s, "Cannot create the LAN listener.");
        return false;
    }
#ifndef _WIN32
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    if (!socket_setup(listener) || bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0
        || listen(listener, COOP_MAX_PLAYERS) != 0) {
        socket_close(listener);
        status_set(s, "Cannot host on that port. Another session may already use it.");
        return false;
    }
#ifdef _WIN32
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    if (getsockname(listener, (struct sockaddr*)&address, &address_size) != 0) {
        socket_close(listener);
        status_set(s, "Cannot read the listening port.");
        return false;
    }
    s->listener = listener;
    s->port = ntohs(address.sin_port);
    s->phase = COOP_LOBBY;
    s->host = true;
    s->local_slot = 0;
    s->players[0].connected = true;
    memcpy(s->players[0].name, s->config.name, COOP_NAME_SIZE);
    memcpy(s->players[0].identity, s->config.identity, COOP_IDENTITY_SIZE);
    s->revision = 1;
    status_set(s, "Hosting. Friends can join using your LAN IPv4 address.");
    return true;
}

bool coop_session_join(CoopSession* s, const char* ipv4, uint16_t port, uint64_t now)
{
    if (!s || s->destroying) return false;
    begin_session(s, now);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (port == 0 || ipv4 == NULL || inet_pton(AF_INET, ipv4, &address.sin_addr) != 1) {
        status_set(s, "Enter an IPv4 address and a port from 1 to 65535.");
        return false;
    }
    snprintf(s->retry_address, sizeof(s->retry_address), "%s", ipv4);
    s->retry_port = port; s->retry_allowed = true;
    Peer* peer = &s->peers[0];
    peer->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (peer->socket == BAD_SOCKET || !socket_setup(peer->socket)) {
        peer_clear(peer);
        status_set(s, "Cannot create the LAN connection.");
        return false;
    }
    if (connect(peer->socket, (struct sockaddr*)&address, sizeof(address)) != 0 && !would_block()) {
        peer_clear(peer);
        status_set(s, "Cannot connect to the host.");
        return false;
    }
    peer->connecting = true;
    peer->slot = 0;
    peer->opened_at = peer->received_at = peer->ping_at = now;
    s->port = port;
    s->phase = COOP_CONNECTING;
    status_set(s, "Connecting to the host...");
    return true;
}

static bool prototype_valid(uint32_t prototype)
{
    /* Retail PC prototype range. Host engine validation will also be required
     * for custom, serialized character builds; this is only a lobby selection. */
    return prototype >= 16000 && prototype <= 16999;
}

static bool dispatch(CoopSession* s, Peer* peer, unsigned type, const uint8_t* payload, size_t size)
{
    if (type == FAILURE_REQUEST || type == FAILURE_NOTICE) {
        if (size < 2 || size > sizeof(s->status) || payload[size - 1] || !valid_text((const char*)payload, size)) return false;
        if (s->host) {
            if (type != FAILURE_REQUEST || peer->slot == COOP_NO_SLOT || !s->locked) return false;
            char reason[sizeof(s->status)];
            snprintf(reason, sizeof(reason), "%s: %.*s", s->players[peer->slot].name,
                (int)(sizeof(reason) - COOP_NAME_SIZE - 2), (const char*)payload);
            coop_session_abort(s, reason);
        } else {
            if (type != FAILURE_NOTICE || (s->phase != COOP_LOBBY && !s->terminating)) return false;
            abort_session(s, (const char*)payload, true);
            s->terminal_received = true;
        }
        return true;
    }
    if (type == FAILURE_ACK) {
        if (size || !s->host || !s->terminating || !peer->terminal_sent) return false;
        peer->terminal_ack = true;
        return true;
    }
    /* In-flight application frames still consume their sequence numbers, but
     * must never run native callbacks after a terminal failure. */
    if (s->terminating) return true;
    if (type == PING || type == PONG) {
        if (size != 0 || (s->host && peer->slot == COOP_NO_SLOT)) return false;
        if (type == PING) queue_frame(peer, PONG, NULL, 0);
        return true;
    }
    if (type >= 16 && type <= 95) {
        return s->phase == COOP_LOBBY && (!s->host || peer->slot != COOP_NO_SLOT)
            && s->handler && s->handler(s->handler_context, s->host ? peer->slot : 0, (uint8_t)type, payload, size);
    }
    if (s->host) {
        if (peer->slot == COOP_NO_SLOT) {
            if (type != HELLO || size != 32 + COOP_IDENTITY_SIZE + COOP_NAME_SIZE
                || !coop_identity_valid(payload + 32)
                || !valid_text((const char*)payload + 32 + COOP_IDENTITY_SIZE, COOP_NAME_SIZE)) return false;
            if (memcmp(s->config.content_id, payload, 32) != 0) {
                reject_peer(peer, "Game content differs from the host. Use the same campaign data and patches.");
                return true;
            }
            for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
                if (s->players[i].connected && memcmp(s->players[i].identity, payload + 32, COOP_IDENTITY_SIZE) == 0) {
                    reject_peer(peer, "This player profile is already connected.");
                    return true;
                }
            }
            uint8_t slot;
            if (s->locked) {
                for (slot = 1; slot < COOP_MAX_PLAYERS; slot++)
                    if ((s->reserved & (1u << slot)) && !memcmp(s->players[slot].identity, payload + 32, COOP_IDENTITY_SIZE)) break;
                if (slot == COOP_MAX_PLAYERS) {
                    reject_peer(peer, "The campaign has started. Slots are reserved for the original player profiles.");
                    return true;
                }
            } else for (slot = 1; slot < COOP_MAX_PLAYERS && s->players[slot].connected; slot++) { }
            if (slot == COOP_MAX_PLAYERS) {
                reject_peer(peer, "The party is full (four players).");
                return true;
            }
            peer->slot = slot;
            s->players[slot].connected = true;
            if (s->locked) s->players[slot].ready = (s->reserved_ready & (1u << slot)) != 0;
            memcpy(s->players[slot].identity, payload + 32, COOP_IDENTITY_SIZE);
            memcpy(s->players[slot].name, payload + 32 + COOP_IDENTITY_SIZE, COOP_NAME_SIZE);
            roster_changed(s, !s->locked);
            chat_replay(s, peer);
            return true;
        }
        CoopPlayer* player = &s->players[peer->slot];
        if (type == READY && size == 1 && payload[0] <= 1) {
            if (s->locked) return player->ready == (payload[0] != 0);
            if (payload[0] && player->prototype == 0) return false;
            if (player->ready != (payload[0] != 0)) {
                player->ready = payload[0] != 0;
                roster_changed(s, false);
            }
            return true;
        }
        if (type == CHARACTER && (size == 4 + COOP_NAME_SIZE || size == 4 + COOP_NAME_SIZE + COOP_CHARACTER_BYTES)
            && prototype_valid(read_u32(payload)) && valid_text((const char*)payload + 4, COOP_NAME_SIZE)) {
            if (s->locked) return false;
            CoopCharacter build = { 0 };
            bool custom = size > 4 + COOP_NAME_SIZE;
            if (custom && (read_u32(payload) != 16066 || !coop_character_decode(&build, payload + 36, COOP_CHARACTER_BYTES)
                || strcmp(build.name, (const char*)payload + 4))) return false;
            player->prototype = read_u32(payload);
            memcpy(player->character, payload + 4, COOP_NAME_SIZE);
            player->custom = custom;
            player->build = build;
            roster_changed(s, true);
            return true;
        }
        if (type == CHAT_REQUEST && size >= 2 && size <= COOP_CHAT_SIZE
            && valid_text((const char*)payload, size) && payload[size - 1] == 0) {
            if (s->now - peer->chat_window >= 1000) {
                peer->chat_window = s->now;
                peer->chat_count = 0;
            }
            if (++peer->chat_count > 8) {
                reject_peer(peer, "Chat rate exceeded. Rejoin and send messages more slowly.");
                return true;
            }
            chat_broadcast(s, peer->slot, (const char*)payload);
            return true;
        }
        return false;
    }
    if (type == ERROR_TEXT && size >= 2 && size <= sizeof(s->status)
        && valid_text((const char*)payload, size) && payload[size - 1] == 0) {
        status_set(s, (const char*)payload);
        s->retry_allowed = strcmp((const char*)payload, "This player profile is already connected.") == 0;
        peer->closing = true;
        return true;
    }
    if (type == ROSTER && size == ROSTER_SIZE && payload[0] > 0 && payload[0] < COOP_MAX_PLAYERS) {
        if (payload[5] > 1 || payload[6] > 15 || (!payload[5] && payload[6])
            || (payload[5] && (!(payload[6] & 1) || !(payload[6] & (1u << payload[0]))))) return false;
        CoopPlayer players[COOP_MAX_PLAYERS];
        memset(players, 0, sizeof(players));
        if (read_u32(payload + 1) <= s->revision) return false;
        if (s->local_slot != COOP_NO_SLOT && payload[0] != s->local_slot) return false;
        for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
            const uint8_t* p = payload + 7 + i * PLAYER_WIRE_SIZE;
            if (p[0] > 1 || p[1] > 1 || p[70] > 1) return false;
            if (payload[5] && p[0] && !(payload[6] & (1u << i))) return false;
            players[i].connected = p[0] != 0;
            players[i].ready = p[1] != 0;
            players[i].prototype = read_u32(p + 2);
            if (p[0] && !valid_text((const char*)p + 6, COOP_NAME_SIZE)) return false;
            if (players[i].prototype != 0 && (!prototype_valid(players[i].prototype) || !valid_text((const char*)p + 38, COOP_NAME_SIZE))) return false;
            if (p[1] && (!p[0] || players[i].prototype == 0)) return false;
            memcpy(players[i].name, p + 6, COOP_NAME_SIZE);
            memcpy(players[i].character, p + 38, COOP_NAME_SIZE);
            players[i].custom = p[70] != 0;
            if (players[i].custom) {
                if ((!players[i].connected && !(payload[6] & (1u << i))) || players[i].prototype != 16066
                    || !coop_character_decode(&players[i].build, p + 71, COOP_CHARACTER_BYTES)
                    || strcmp(players[i].build.name, players[i].character)) return false;
            } else {
                for (unsigned j = 0; j < COOP_CHARACTER_BYTES; j++) if (p[71 + j]) return false;
            }
        }
        if (!players[0].connected || !players[payload[0]].connected) return false;
        memcpy(s->players, players, sizeof(players));
        s->local_slot = payload[0];
        memcpy(s->players[s->local_slot].identity, s->config.identity, COOP_IDENTITY_SIZE);
        s->revision = read_u32(payload + 1);
        s->locked = payload[5] != 0; s->reserved = payload[6];
        s->phase = COOP_LOBBY;
        status_set(s, s->locked ? "Connected to the running party. Waiting for campaign synchronization." : "Connected. Choose your character and ready up.");
        return true;
    }
    if (type == CHAT && s->phase == COOP_LOBBY && size >= 1 + COOP_NAME_SIZE + 2
        && size <= 1 + COOP_NAME_SIZE + COOP_CHAT_SIZE && payload[0] < COOP_MAX_PLAYERS
        && valid_text((const char*)payload + 1, COOP_NAME_SIZE)
        && valid_text((const char*)payload + 1 + COOP_NAME_SIZE, size - 1 - COOP_NAME_SIZE)
        && payload[size - 1] == 0) {
        chat_add(s, payload[0], (const char*)payload + 1, (const char*)payload + 1 + COOP_NAME_SIZE);
        return true;
    }
    return false;
}

static bool flush(Peer* peer)
{
    if (peer->output_size == 0) return true;
#ifdef MSG_NOSIGNAL
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    int sent = (int)send(peer->socket, (const char*)peer->output, (int)peer->output_size, flags);
    if (sent < 0) return would_block();
    if (sent == 0) return false;
    peer->output_size -= (size_t)sent;
    memmove(peer->output, peer->output + sent, peer->output_size);
    return true;
}

static bool receive_frames(CoopSession* s, Peer* peer)
{
    uint64_t generation = s->generation, connection = peer->generation;
    /* Bound per-poll work, including a peer that continuously sends data. */
    for (int batch = 0;; batch++) {
        while (peer->input_size >= HEADER_SIZE) {
            uint8_t* p = peer->input;
            if (memcmp(p, "ACMP", 4) != 0 || p[4] != 0 || p[5] != COOP_PROTOCOL_VERSION || p[6] != 0) {
                reject_peer(peer, "Incompatible co-op protocol. Use the same Arcanum MP build.");
                return true;
            }
            size_t size = read_u32(p + 8);
            if (size > MAX_PAYLOAD || read_u32(p + 12) != peer->receive_sequence + 1) return false;
            if (peer->input_size < HEADER_SIZE + size) break;
            peer->receive_sequence++;
            peer->received_at = s->now;
            unsigned type = p[7];
            uint8_t payload[MAX_PAYLOAD];
            if (size) memcpy(payload, p + HEADER_SIZE, size);
            /* Consume before calling native code. A nested poll must see the
             * next frame, and its buffer edits must not change this payload. */
            peer->input_size -= HEADER_SIZE + size;
            memmove(p, p + HEADER_SIZE + size, peer->input_size);
            bool accepted = dispatch(s, peer, type, payload, size);
            if (s->generation != generation || peer->generation != connection || s->destroying) return true;
            if (!accepted && !s->terminating) {
                fprintf(stderr, "ARCMP rejected message type=%u slot=%u bytes=%zu\n", type, peer->slot, size);
                reject_peer(peer, "Invalid co-op message.");
                return true;
            }
            if (peer->closing) return true;
        }
        if (batch == 4) return true;
        int count = (int)recv(peer->socket, (char*)peer->input + peer->input_size,
            (int)(BUFFER_SIZE - peer->input_size), 0);
        if (count < 0) return would_block();
        if (count == 0) return false;
        peer->input_size += (size_t)count;
    }
}

static void accept_peers(CoopSession* s)
{
    for (int attempts = 0; attempts < COOP_MAX_PLAYERS; attempts++) {
        NetSocket socket = accept(s->listener, NULL, NULL);
        if (socket == BAD_SOCKET) return;
        size_t index;
        for (index = 0; index < COOP_MAX_PLAYERS && s->peers[index].socket != BAD_SOCKET; index++) { }
        if (index == COOP_MAX_PLAYERS || !socket_setup(socket)) {
            socket_close(socket);
            continue;
        }
        Peer* peer = &s->peers[index];
        peer->socket = socket;
        peer->opened_at = peer->received_at = peer->ping_at = s->now;
    }
}

static bool finish_connect(CoopSession* s, Peer* peer)
{
    fd_set write_set, error_set;
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(peer->socket, &write_set);
    FD_SET(peer->socket, &error_set);
    struct timeval timeout = { 0, 0 };
    int selected = select((int)peer->socket + 1, NULL, &write_set, &error_set, &timeout);
    if (selected < 0) return would_block();
    if (selected == 0) return true;
    int error = 0;
#ifdef _WIN32
    int size = sizeof(error);
#else
    socklen_t size = sizeof(error);
#endif
    if (getsockopt(peer->socket, SOL_SOCKET, SO_ERROR, (char*)&error, &size) != 0 || error != 0) {
        status_set(s, "Connection refused or unreachable. Check the address, port and host firewall.");
        return false;
    }
    peer->connecting = false;
    uint8_t hello[32 + COOP_IDENTITY_SIZE + COOP_NAME_SIZE];
    memcpy(hello, s->config.content_id, 32);
    memcpy(hello + 32, s->config.identity, COOP_IDENTITY_SIZE);
    memcpy(hello + 32 + COOP_IDENTITY_SIZE, s->config.name, COOP_NAME_SIZE);
    return queue_frame(peer, HELLO, hello, sizeof(hello));
}

static void poll_session(CoopSession* s, uint64_t now)
{
    if (s->phase == COOP_OFFLINE && !s->terminating) return;
    uint64_t generation = s->generation;
    if (now < s->now) now = s->now;
    s->now = now;
    if (s->host && !s->terminating) accept_peers(s);
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        Peer* peer = &s->peers[i];
        if (peer->socket == BAD_SOCKET) continue;
        uint64_t connection = peer->generation;
        bool good = true;
        if (s->terminating) {
            if (peer->connecting || (s->host && peer->slot == COOP_NO_SLOT)) { peer_clear(peer); continue; }
            terminal_output(s, peer);
        }
        if (peer->connecting) good = finish_connect(s, peer);
        if (good && !peer->connecting) {
            good = flush(peer);
            if (good && !peer->closing) good = receive_frames(s, peer);
        }
        if (s->generation != generation || s->destroying) return;
        if (peer->generation != connection) continue;
        /* Nested native UI pumps may have advanced the shared clock. */
        now = s->now;
        if (s->terminating) {
            if (!good || peer->closing || (s->host && peer->terminal_ack)) peer_clear(peer);
            continue;
        }
        bool handshaking = s->host ? peer->slot == COOP_NO_SLOT : s->phase == COOP_CONNECTING;
        if (good && !peer->closing && ((handshaking && now - peer->opened_at >= 5000) || now - peer->received_at >= 15000)) {
            if (!s->host) status_set(s, "Connection timed out. Check the host and try joining again.");
            good = false;
        }
        if (good && !peer->connecting && !peer->closing && !handshaking && now - peer->ping_at >= 3000) {
            queue_frame(peer, PING, NULL, 0);
            peer->ping_at = now;
        }
        if (peer->closing) {
            /* A rejected/slow peer must not retain a slot indefinitely. A
             * best-effort flush delivers the useful rejection before FIN. */
            flush(peer);
            good = false;
        }
        if (!good) {
            if (!s->host) {
                if (s->phase == COOP_LOBBY && !peer->closing)
                    status_set(s, "The host disconnected.");
                else if (s->phase == COOP_CONNECTING && strstr(s->status, "Connecting") == s->status)
                    status_set(s, "The host closed the connection.");
                bool preserve = s->locked && s->reserved;
                CoopPlayer saved[COOP_MAX_PLAYERS]; memcpy(saved, s->players, sizeof(saved));
                uint8_t local = s->local_slot;
                connections_clear(s);
                if (preserve) {
                    memcpy(s->players, saved, sizeof(saved)); s->local_slot = local;
                    for (unsigned j = 0; j < COOP_MAX_PLAYERS; j++) s->players[j].connected = s->players[j].ready = false;
                }
                return;
            }
            uint8_t slot = peer->slot;
            peer_clear(peer);
            if (slot != COOP_NO_SLOT) {
                if (s->locked) s->players[slot].connected = s->players[slot].ready = false;
                else memset(&s->players[slot], 0, sizeof(s->players[slot]));
                roster_changed(s, !s->locked);
            }
        }
    }
    if (s->terminating) {
        if (s->terminal_at == UINT64_MAX) s->terminal_at = s->now;
        bool connected = false;
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) connected |= s->peers[i].socket != BAD_SOCKET;
        if (!connected || s->now - s->terminal_at >= 5000) connections_clear(s);
    }
}

void coop_session_poll(CoopSession* s, uint64_t now)
{
    if (!s || s->destroying) return;
    s->polling++;
    poll_session(s, now);
    if (!--s->polling && s->destroying) session_free(s);
}

bool coop_session_character(CoopSession* s, uint32_t prototype, const char* name)
{
    if (s->phase != COOP_LOBBY || s->locked || !prototype_valid(prototype) || !valid_text(name, COOP_NAME_SIZE)) return false;
    uint8_t payload[4 + COOP_NAME_SIZE] = { 0 };
    write_u32(payload, prototype);
    memcpy(payload + 4, name, strlen(name));
    if (!s->host) return queue_frame(&s->peers[0], CHARACTER, payload, sizeof(payload));
    CoopPlayer* p = &s->players[0];
    p->prototype = prototype;
    p->custom = false;
    memset(&p->build, 0, sizeof(p->build));
    memset(p->character, 0, sizeof(p->character));
    memcpy(p->character, name, strlen(name));
    roster_changed(s, true);
    return true;
}

bool coop_session_custom_character(CoopSession* s, const CoopCharacter* c)
{
    if (!s || s->phase != COOP_LOBBY || s->locked || !coop_character_valid(c)) return false;
    uint8_t payload[36 + COOP_CHARACTER_BYTES] = { 0 };
    write_u32(payload, 16066);
    memcpy(payload + 4, c->name, strlen(c->name));
    if (!coop_character_encode(c, payload + 36)) return false;
    if (!s->host) return queue_frame(&s->peers[0], CHARACTER, payload, sizeof(payload));
    CoopPlayer* p = &s->players[0];
    p->prototype = 16066; p->custom = true; p->build = *c;
    memset(p->character, 0, sizeof(p->character));
    memcpy(p->character, c->name, strlen(c->name));
    roster_changed(s, true);
    return true;
}

bool coop_session_ready(CoopSession* s, bool ready)
{
    if (s->locked) return false;
    if (s->phase != COOP_LOBBY || (ready && s->players[s->local_slot].prototype == 0)) return false;
    if (!s->host) {
        uint8_t value = ready;
        return queue_frame(&s->peers[0], READY, &value, 1);
    }
    if (s->players[0].ready != ready) {
        s->players[0].ready = ready;
        roster_changed(s, false);
    }
    return true;
}
void coop_session_lock(CoopSession* s, bool locked)
{
    if (!s || s->locked == locked) return;
    s->locked = locked;
    if (!s->host) return;
    s->reserved = s->reserved_ready = 0;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (locked && s->players[i].connected) {
            s->reserved |= (uint8_t)(1u << i);
            if (s->players[i].ready) s->reserved_ready |= (uint8_t)(1u << i);
        } else if (!locked && !s->players[i].connected) memset(&s->players[i], 0, sizeof(s->players[i]));
    }
    roster_changed(s, false);
}
bool coop_session_locked(const CoopSession* s) { return s && s->locked; }
bool coop_session_remove(CoopSession* s, uint8_t slot, const uint8_t identity[COOP_IDENTITY_SIZE])
{
    if (!s || !s->host || s->phase != COOP_LOBBY || s->locked || !slot || slot >= COOP_MAX_PLAYERS
        || !coop_identity_valid(identity) || !s->players[slot].connected || memcmp(identity, s->players[slot].identity, COOP_IDENTITY_SIZE)) return false;
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        Peer* peer = &s->peers[i];
        if (peer->slot != slot || peer->socket == BAD_SOCKET || peer->closing) continue;
        reject_peer(peer, "The host removed you from the lobby. You can join again when invited.");
        /* No launch can race the removal while its socket finishes closing. */
        roster_changed(s, true);
        return true;
    }
    return false;
}
uint8_t coop_session_reserved(const CoopSession* s) { return s ? s->reserved : 0; }
bool coop_session_can_retry(const CoopSession* s) { return s && !s->host && s->retry_allowed && s->retry_port && s->retry_address[0]; }
bool coop_session_retry(CoopSession* s, uint64_t now)
{
    if (!coop_session_can_retry(s) || s->phase != COOP_OFFLINE) return false;
    char address[16]; memcpy(address, s->retry_address, sizeof(address));
    return coop_session_join(s, address, s->retry_port, now);
}
bool coop_session_drop_transport(CoopSession* s)
{
    if (!s || s->host || s->phase != COOP_LOBBY) return false;
    bool preserve=s->locked && s->reserved;
    CoopPlayer saved[COOP_MAX_PLAYERS]; memcpy(saved,s->players,sizeof(saved)); uint8_t local=s->local_slot;
    connections_clear(s);
    if (preserve) {
        memcpy(s->players,saved,sizeof(saved)); s->local_slot=local;
        for (unsigned i=0;i<COOP_MAX_PLAYERS;i++) s->players[i].connected=s->players[i].ready=false;
    }
    status_set(s,"Connection interrupted."); return true;
}

bool coop_session_chat(CoopSession* s, const char* text)
{
    if (s->phase != COOP_LOBBY || !valid_text(text, COOP_CHAT_SIZE)) return false;
    if (!s->host) return queue_frame(&s->peers[0], CHAT_REQUEST, text, strlen(text) + 1);
    chat_broadcast(s, 0, text);
    return true;
}

bool coop_session_all_ready(const CoopSession* s)
{
    if (s->locked) for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if ((s->reserved & (1u << i)) && !s->players[i].connected) return false;
    if (s->phase != COOP_LOBBY) return false;
    unsigned count = 0;
    for (size_t i = 0; i < COOP_MAX_PLAYERS; i++) {
        if (s->players[i].connected) {
            count++;
            if (!s->players[i].ready || s->players[i].prototype == 0) return false;
        }
    }
    return count >= 2;
}

CoopPhase coop_session_phase(const CoopSession* s) { return s->phase; }
bool coop_session_is_host(const CoopSession* s) { return s->host; }
uint8_t coop_session_local_slot(const CoopSession* s) { return s->local_slot; }
uint16_t coop_session_port(const CoopSession* s) { return s->port; }
uint32_t coop_session_revision(const CoopSession* s) { return s->revision; }
uint64_t coop_session_now(const CoopSession* s) { return s ? s->now : 0; }
const CoopPlayer* coop_session_players(const CoopSession* s) { return s->players; }
const char* coop_session_status(const CoopSession* s) { return s->status; }
uint64_t coop_session_chat_count(const CoopSession* s) { return s->chat_count; }
const CoopChat* coop_session_chat_at(const CoopSession* s, uint64_t ordinal)
{
    if (ordinal >= s->chat_count || s->chat_count - ordinal > COOP_CHAT_HISTORY) return NULL;
    return &s->chat[ordinal % COOP_CHAT_HISTORY];
}
