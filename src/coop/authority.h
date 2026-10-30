#ifndef ARCMP_AUTHORITY_H
#define ARCMP_AUTHORITY_H
#include <stdbool.h>
typedef enum CoopAuthority {
    COOP_AUTHORITY_LOCAL,
    COOP_AUTHORITY_HOST,
    COOP_AUTHORITY_REPLICA,
} CoopAuthority;
/* Process-wide engine role; all mutation/time/script gates use this explicit
 * role rather than enabling legacy multiplayer behavior. */
void coop_authority_set(CoopAuthority authority);
CoopAuthority coop_authority_get(void);
bool coop_authority_can_simulate(void);
#endif
