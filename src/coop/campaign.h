#ifndef ARCMP_CAMPAIGN_H
#define ARCMP_CAMPAIGN_H
#include "coop/session.h"
#include "game/obj_id.h"

typedef struct CoopCampaignMember {
    bool occupied;
    uint8_t owner[COOP_IDENTITY_SIZE];
    ObjectID object_id;
    uint32_t prototype;
    char character[COOP_NAME_SIZE];
} CoopCampaignMember;

/* Campaign slots persist independently of transient TCP connection slots. */
bool coop_campaign_begin(const CoopPlayer players[COOP_MAX_PLAYERS]);
bool coop_campaign_selections_valid(const CoopPlayer players[COOP_MAX_PLAYERS]);
void coop_campaign_reset(void);
bool coop_campaign_active(void);
/* Count is a population, never a slot boundary: lobby departures leave holes. */
unsigned coop_campaign_count(void);
/* Returns the first occupied slot at/after start, or COOP_MAX_PLAYERS. */
unsigned coop_campaign_next_slot(unsigned start);
const CoopCampaignMember* coop_campaign_member(unsigned slot);
int64_t coop_campaign_object(unsigned slot);
/* Native simulation proximity follows every present owned PC on the host.
 * Other authority roles retain the ordinary local-PC anchor. */
int64_t coop_campaign_nearest_player(int64_t object);
int coop_campaign_slot_for_object(int64_t object);
int coop_campaign_slot_for_owner(const uint8_t owner[COOP_IDENTITY_SIZE]);
bool coop_campaign_save(void);
bool coop_campaign_load(void);
bool coop_campaign_validate(void);
#endif
