#ifndef ARCMP_FOLLOWERS_H
#define ARCMP_FOLLOWERS_H
#include "coop/follower_state.h"
#include "coop/world_net.h"
typedef struct CoopFollowers CoopFollowers;
typedef int64_t (*CoopFollowerActor)(void*, uint8_t, const uint8_t*);
CoopFollowers* coop_followers_create(CoopSession*, CoopWorldNetwork*, uint32_t epoch, CoopFollowerActor, void*);
void coop_followers_destroy(CoopFollowers*);
bool coop_followers_message(CoopFollowers*, uint8_t sender, uint8_t type, const uint8_t*, size_t);
bool coop_followers_poll(CoopFollowers*, uint64_t now, bool accepting);
bool coop_followers_submit(CoopFollowers*, const CoopFollowerRequest*);
bool coop_followers_order(CoopFollowers*, int64_t follower, CoopFollowerOrder, int64_t target, int64_t location, bool force);
bool coop_followers_broadcast(CoopFollowers*, const char* command, int64_t target, int64_t location);
bool coop_followers_settled(const CoopFollowers*);
bool coop_followers_busy(const CoopFollowers*);
uint32_t coop_followers_last_result(const CoopFollowers*, unsigned* status);
unsigned coop_followers_executed(const CoopFollowers*);
unsigned coop_followers_rejected(const CoopFollowers*);
#endif
