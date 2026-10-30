#include "coop/social.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "game/critter.h"
#include "game/reputation.h"
#include "game/stat.h"
static bool shared(int64_t actor)
{
    return coop_authority_get() == COOP_AUTHORITY_HOST && coop_campaign_slot_for_object(actor) >= 0;
}
void coop_social_xp(int64_t actor, int amount)
{
    if (!actor || !coop_authority_can_simulate()) return;
    if (shared(actor)) {
        for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) critter_give_xp(coop_campaign_object(i), amount);
    } else critter_give_xp(actor, amount);
}
void coop_social_reputation(int64_t actor, int number, int remove)
{
    if (!actor || number < 1000 || number >= 2000 || !coop_authority_can_simulate()) return;
    bool party = shared(actor);
    unsigned count = party ? COOP_MAX_PLAYERS : 1;
    for (unsigned i = 0; i < count; i++) {
        int64_t pc = party ? coop_campaign_object(i) : actor;
        if (!pc) continue;
        if (remove) reputation_remove(pc, number); else reputation_add(pc, number);
    }
}
void coop_social_alignment(int64_t actor, char operation, int value)
{
    if (!actor || !coop_authority_can_simulate()) return;
    bool party = shared(actor);
    unsigned count = party ? COOP_MAX_PLAYERS : 1;
    for (unsigned i = 0; i < count; i++) {
        int64_t pc = party ? coop_campaign_object(i) : actor;
        if (!pc) continue;
        int old = stat_base_get(pc, STAT_ALIGNMENT), next = value;
        if (operation == '+') next = old + value;
        else if (operation == '>') next = old < value ? value : old;
        else if (operation == '<') next = old > value ? value : old;
        stat_base_set(pc, STAT_ALIGNMENT, next);
    }
}
