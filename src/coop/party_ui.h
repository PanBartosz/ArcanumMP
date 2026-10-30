#ifndef ARCMP_PARTY_UI_H
#define ARCMP_PARTY_UI_H
#include <stdbool.h>
#include <stdint.h>
void coop_party_ui_tick(uint64_t now);
void coop_party_ui_close(void);
/* Use exactly the native spell/skill/item targeting path used by world clicks. */
bool coop_party_ui_target(unsigned member);
bool coop_party_ui_target_actor(int64_t actor, bool attack_enemy);
enum {
    COOP_STATUS_DEAD = 1u << 0, COOP_STATUS_UNCONSCIOUS = 1u << 1,
    COOP_STATUS_POISONED = 1u << 2, COOP_STATUS_TIRED = 1u << 3,
    COOP_STATUS_STUNNED = 1u << 4, COOP_STATUS_PARALYZED = 1u << 5,
    COOP_STATUS_BLINDED = 1u << 6, COOP_STATUS_INJURED = 1u << 7,
    COOP_STATUS_ENCUMBERED = 1u << 8,
};
unsigned coop_party_ui_statuses(int64_t actor);
#endif
