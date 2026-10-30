#ifndef ARCMP_DISCOVERY_H
#define ARCMP_DISCOVERY_H
#include "coop/session.h"
enum { COOP_DISCOVERY_PORT=35740, COOP_DISCOVERY_CAPACITY=16, COOP_DISCOVERY_BYTES=92, COOP_DISCOVERY_TTL=6000 };
typedef struct CoopDiscoveryParty {
    char address[16],name[COOP_NAME_SIZE];
    uint8_t content[32],players;
    uint16_t port,protocol;
    uint64_t instance,seen;
    bool locked;
} CoopDiscoveryParty;
typedef struct CoopDiscovery CoopDiscovery;
CoopDiscovery* coop_discovery_create(bool host,uint16_t discovery_port);
void coop_discovery_destroy(CoopDiscovery* discovery);
/* Host advertisements never contain persistent profile identities. */
bool coop_discovery_advertise(CoopDiscovery*,const CoopDiscoveryParty*);
/* A NULL target searches the local subnet and loopback. A numeric target also
 * supports directed LAN queries and deterministic socket integration tests. */
bool coop_discovery_poll(CoopDiscovery*,uint64_t now,const char* target);
void coop_discovery_refresh(CoopDiscovery*);
unsigned coop_discovery_count(const CoopDiscovery*);
const CoopDiscoveryParty* coop_discovery_party(const CoopDiscovery*,unsigned index);
uint16_t coop_discovery_port(const CoopDiscovery*);
bool coop_discovery_encode(const CoopDiscoveryParty*,uint64_t query,uint8_t bytes[COOP_DISCOVERY_BYTES]);
bool coop_discovery_decode(const void* bytes,size_t size,uint64_t query,CoopDiscoveryParty* out);
#endif
