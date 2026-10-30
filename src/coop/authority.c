#include "coop/authority.h"
static CoopAuthority role;
void coop_authority_set(CoopAuthority authority) { role = authority; }
CoopAuthority coop_authority_get(void) { return role; }
bool coop_authority_can_simulate(void) { return role != COOP_AUTHORITY_REPLICA; }
