#ifndef ARCMP_BOOTSTRAP_H
#define ARCMP_BOOTSTRAP_H
#include "coop/session.h"

#define COOP_SNAPSHOT_LIMIT (64u * 1024u * 1024u)
enum CoopBootstrapMessage {
    COOP_SNAPSHOT_OFFER = 16,
    COOP_SNAPSHOT_CHUNK,
    COOP_SNAPSHOT_READY,
    COOP_SNAPSHOT_COMMIT,
    COOP_SNAPSHOT_CANCEL,
};
typedef enum CoopBootstrapPhase {
    COOP_BOOT_IDLE,
    COOP_BOOT_SENDING,
    COOP_BOOT_RECEIVING,
    COOP_BOOT_RECEIVED,
    COOP_BOOT_WAITING,
    COOP_BOOT_COMMITTED,
    COOP_BOOT_FAILED,
} CoopBootstrapPhase;
typedef struct CoopBootstrap CoopBootstrap;

/* Borrows the session and installs its engine-message handler until destroyed.
 * A future engine adapter can forward these message types to message(). */
CoopBootstrap* coop_bootstrap_create(CoopSession* session);
void coop_bootstrap_destroy(CoopBootstrap* bootstrap);
/* Host borrows immutable data until the bootstrap is destroyed. A new instance
 * is required for a new attempt. Transfer metadata never selects a file path. */
bool coop_bootstrap_begin(CoopBootstrap* bootstrap, uint32_t epoch, const void* data, size_t size, uint64_t now_ms);
void coop_bootstrap_poll(CoopBootstrap* bootstrap, uint64_t now_ms);
bool coop_bootstrap_message(void* bootstrap, uint8_t sender, uint8_t type, const uint8_t* payload, size_t size);
/* Client calls this only after validating/loading the complete snapshot. */
bool coop_bootstrap_loaded(CoopBootstrap* bootstrap);
void coop_bootstrap_cancel(CoopBootstrap* bootstrap);
CoopBootstrapPhase coop_bootstrap_phase(const CoopBootstrap* bootstrap);
const uint8_t* coop_bootstrap_data(const CoopBootstrap* bootstrap, size_t* size);
const char* coop_bootstrap_error(const CoopBootstrap* bootstrap);
uint32_t coop_snapshot_crc(const void* data, size_t size);
#endif
