#include "coop/world_net.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include "coop/world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct WorldPeer {
    bool begun;
    bool acknowledged;
    size_t offset;
} WorldPeer;
struct CoopWorldNetwork {
    CoopSession* session;
    CoopWorldTracker* tracker;
    bool host;
    bool settle;
    bool settled;
    bool capture_requested;
    uint64_t captures;
    uint32_t applied_revision;
    uint32_t epoch;
    uint32_t revision;
    uint32_t checksum;
    uint8_t* pending;
    size_t size;
    size_t decoded_size;
    unsigned pending_deletions;
    size_t received;
    uint64_t now;
    uint64_t captured_at;
    uint64_t progressed_at;
    uint64_t bytes;
    uint64_t wire_bytes;
    uint64_t deletions;
    bool initialized;
    bool retired;
    WorldPeer peers[COOP_MAX_PLAYERS];
};
CoopWorldNetwork* coop_world_net_create(CoopSession* session, uint32_t epoch)
{
    CoopWorldNetwork* net = calloc(1, sizeof(*net));
    if (!net) return NULL;
    net->session = session;
    net->host = coop_session_is_host(session);
    net->epoch = epoch;
    if (net->host) {
        net->tracker = coop_world_tracker_create(true);
        if (!net->tracker) {
            free(net);
            return NULL;
        }
    }
    return net;
}
void coop_world_net_destroy(CoopWorldNetwork* net)
{
    if (!net) return;
    coop_world_tracker_destroy(net->tracker);
    free(net->pending);
    free(net);
}
void coop_world_net_retire(CoopWorldNetwork* net) { if (net) net->retired = true; }
bool coop_world_net_retired(const CoopWorldNetwork* net) { return net && net->retired; }
bool coop_world_net_message(CoopWorldNetwork* net, uint8_t sender, uint8_t type, const uint8_t* p, size_t size)
{
    if (size < 8 || coop_object_read32(p) != net->epoch) return false;
    uint32_t revision = coop_object_read32(p + 4);
    if (net->host) {
        if (!sender || sender >= COOP_MAX_PLAYERS || type != COOP_WORLD_ACK || size != 8 || !net->pending
            || revision != net->revision || !net->peers[sender].begun || net->peers[sender].offset != net->size || net->peers[sender].acknowledged) return false;
        net->peers[sender].acknowledged = true;
        net->progressed_at = net->now;
        return true;
    }
    if (sender) return false;
    if (type == COOP_WORLD_BEGIN) {
        if (size != 20 || net->pending || revision != net->revision + 1 || revision == 0) return false;
        size_t length = coop_object_read32(p + 8);
        size_t decoded_size = coop_object_read32(p + 16);
        if (!length || decoded_size < 20 || decoded_size > COOP_WORLD_LIMIT || length > decoded_size) return false;
        net->pending = malloc(length);
        if (!net->pending) return false;
        net->revision = revision;
        net->size = length;
        net->decoded_size = decoded_size;
        net->received = 0;
        net->checksum = coop_object_read32(p + 12);
        net->progressed_at = net->now;
        return true;
    }
    if (type == COOP_WORLD_CHUNK) {
        if (!net->pending || size <= 12 || size > COOP_MESSAGE_CAPACITY || revision != net->revision
            || coop_object_read32(p + 8) != net->received || size - 12 > net->size - net->received) return false;
        memcpy(net->pending + net->received, p + 12, size - 12);
        net->received += size - 12;
        net->progressed_at = net->now;
        return true;
    }
    return false;
}
bool coop_world_net_poll(CoopWorldNetwork* net, uint64_t now)
{
    if (net->retired) return true;
    if (now > net->now) net->now = now;
    if (net->pending && net->now - net->progressed_at > 30000) {
        fprintf(stderr, "ARCMP world transfer timeout host=%d epoch=%u revision=%u received=%zu bytes=%zu age=%llu\n",
            net->host, net->epoch, net->revision, net->received, net->size, (unsigned long long)(net->now - net->progressed_at));
        return false;
    }
    uint8_t p[COOP_MESSAGE_CAPACITY];
    coop_object_write32(p, net->epoch);
    coop_object_write32(p + 4, net->revision);
    if (!net->host) {
        if (net->pending && net->received == net->size) {
            if (!net->peers[0].acknowledged) {
                uint8_t* decoded = net->pending;
                if (net->size < net->decoded_size) {
                    decoded = malloc(net->decoded_size);
                    if (!decoded) return false;
                    uLongf length = (uLongf)net->decoded_size;
                    uLong consumed = (uLong)net->size;
                    int result = uncompress2(decoded,&length,net->pending,&consumed);
                    if (result != Z_OK || length != net->decoded_size || consumed != net->size) {
                        fprintf(stderr,"ARCMP invalid compressed world epoch=%u revision=%u compressed=%zu decoded=%zu\n",net->epoch,net->revision,net->size,net->decoded_size);
                        free(decoded); return false;
                    }
                }
                bool checksum = coop_snapshot_crc(decoded, net->decoded_size) == net->checksum;
                if (!checksum || !coop_world_apply(decoded, net->decoded_size)) {
                    fprintf(stderr, "ARCMP world transfer rejected epoch=%u revision=%u bytes=%zu checksum=%d\n", net->epoch, net->revision, net->size, checksum);
                    if (getenv("ARCMP_TRACE_WORLD")) {
                        FILE* file = fopen("arcmp-world-rejected.bin", "wb");
                        if (file) {
                            fwrite(decoded, 1, net->decoded_size, file);
                            fclose(file);
                        }
                    }
                    if (decoded != net->pending) free(decoded);
                    return false;
                }
                net->peers[0].acknowledged = true;
                net->applied_revision = net->revision;
                net->bytes += net->decoded_size;
                net->wire_bytes += net->size;
                net->deletions += coop_world_deletions(decoded, net->decoded_size);
                if (decoded != net->pending) free(decoded);
            }
            if (coop_session_send(net->session, 0, COOP_WORLD_ACK, p, 8)) {
                free(net->pending);
                net->pending = NULL;
                net->peers[0].acknowledged = false;
            }
        }
        return true;
    }
    const CoopPlayer* players = coop_session_players(net->session);
    if (net->pending) {
        bool acknowledged = true;
        for (uint8_t i = 1; i < COOP_MAX_PLAYERS; i++) {
            if (!players[i].connected) continue;
            WorldPeer* peer = &net->peers[i];
            if (!peer->begun) {
                coop_object_write32(p + 8, (uint32_t)net->size);
                coop_object_write32(p + 12, net->checksum);
                coop_object_write32(p + 16, (uint32_t)net->decoded_size);
                peer->begun = coop_session_send(net->session, i, COOP_WORLD_BEGIN, p, 20);
            }
            for (unsigned chunk = 0; peer->begun && peer->offset < net->size && chunk < 24; chunk++) {
                size_t count = net->size - peer->offset;
                if (count > COOP_MESSAGE_CAPACITY - 12) count = COOP_MESSAGE_CAPACITY - 12;
                coop_object_write32(p + 8, (uint32_t)peer->offset);
                memcpy(p + 12, net->pending + peer->offset, count);
                if (!coop_session_send(net->session, i, COOP_WORLD_CHUNK, p, count + 12)) break;
                peer->offset += count;
                net->progressed_at = net->now;
            }
            acknowledged &= peer->acknowledged;
        }
        if (!acknowledged) return true;
        net->initialized = true;
        net->bytes += net->decoded_size;
        net->wire_bytes += net->size;
        net->deletions += net->pending_deletions;
        free(net->pending);
        net->pending = NULL;
    }
    if (!net->settled && (net->settle || net->capture_requested || !net->captured_at || net->now - net->captured_at >= 200)) {
        if (!coop_world_capture(net->tracker, &net->pending, &net->size)) return false;
        net->captures++;
        net->capture_requested = false;
        net->captured_at = net->now;
        if (net->pending) {
            if (++net->revision == 0) return false;
            memset(net->peers, 0, sizeof(net->peers));
            net->checksum = coop_snapshot_crc(net->pending, net->size);
            net->decoded_size = net->size;
            net->pending_deletions = coop_world_deletions(net->pending,net->size);
            /* Keep canonical bytes/checksum/ACK semantics. Only the transport
             * representation changes; incompressible batches stay unmodified. */
            if (net->size >= 256) {
                uLongf length = compressBound((uLong)net->size);
                uint8_t* compressed = malloc(length);
                if (compressed && compress2(compressed,&length,net->pending,(uLong)net->size,Z_BEST_SPEED) == Z_OK
                    && length < net->size) {
                    free(net->pending); net->pending = compressed; net->size = (size_t)length;
                } else free(compressed);
            }
            net->progressed_at = net->now;
        } else if (net->settle) {
            net->settled = true;
        }
    }
    return true;
}
void coop_world_net_settle(CoopWorldNetwork* net) { net->settle = true; }
bool coop_world_net_settled(const CoopWorldNetwork* net) { return net->settled; }
bool coop_world_net_initialized(const CoopWorldNetwork* net) { return net->host ? net->initialized : net->applied_revision != 0; }
uint32_t coop_world_net_revision(const CoopWorldNetwork* net) { return net->revision; }
uint32_t coop_world_net_applied_revision(const CoopWorldNetwork* net) { return net->host ? net->revision : net->applied_revision; }
uint64_t coop_world_net_captures(const CoopWorldNetwork* net) { return net->captures; }
uint64_t coop_world_net_request_capture(CoopWorldNetwork* net)
{
    if (net->retired) return 0;
    /* A completion ticket must remain satisfiable even if a caller asks just
     * after a previous settle. The gameplay input fence controls finality. */
    net->settled = false;
    net->capture_requested = true;
    return net->captures + 1;
}
uint64_t coop_world_net_bytes(const CoopWorldNetwork* net) { return net->bytes; }
uint64_t coop_world_net_wire_bytes(const CoopWorldNetwork* net) { return net->wire_bytes; }
uint64_t coop_world_net_deletions(const CoopWorldNetwork* net) { return net->deletions; }
