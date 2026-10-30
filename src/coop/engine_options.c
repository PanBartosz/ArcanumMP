#include "coop/engine_options.h"
#include "coop/display.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/runtime.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/gamelib.h"
#include "game/player.h"
#include "game/critter.h"
#include "game/object.h"
#include "game/skill.h"
#include <string.h>
static int difficulty = -1;
static uint8_t owners[4];
void coop_engine_options_reset(void)
{
    difficulty = -1; memset(owners, 0, sizeof(owners));
    combat_campaign_fast_set(settings_get_value(&settings, FAST_TURN_BASED_KEY) != 0);
}
int coop_engine_options_difficulty(void) { return coop_campaign_active() ? difficulty : -1; }
bool coop_engine_options_difficulty_set(unsigned value)
{
    if (value > 2 || !coop_campaign_active() || !coop_authority_can_simulate()) return false;
    difficulty = (int)value; return true;
}
void coop_engine_options_capture(CoopEnvironmentState* state)
{
    state->difficulty = (uint8_t)gamelib_game_difficulty_get();
    state->fast_turn_based = combat_fast_turn_based_get();
    memcpy(state->owner_options, owners, sizeof(owners));
}
void coop_engine_options_install(const CoopEnvironmentState* state)
{
    difficulty = state->difficulty;
    combat_campaign_fast_set(state->fast_turn_based);
    memcpy(owners, state->owner_options, sizeof(owners));
    /* Protocol52 derived running from Num Lock's platform-dependent initial
     * state. Migrate those automatic defaults once; current explicit choices
     * carry bit6 and are preserved, including an intentional walking default. */
    for (unsigned i = 0; i < 4; i++)
        if ((owners[i] & 0x80) && !(owners[i] & 0x40)) owners[i] |= 0x44;
}
unsigned coop_engine_options_record(int64_t actor)
{
    int member = coop_campaign_slot_for_object(actor);
    return member < 0 || member >= 4 ? 0 : owners[member];
}
int coop_engine_options_get(int64_t actor, unsigned option)
{
    unsigned record = coop_engine_options_record(actor);
    return !record || option >= COOP_OPTION_COUNT ? -1 : (record >> option) & 1;
}
unsigned coop_engine_options_defaults(void)
{
    return (settings_get_value(&settings, AUTO_ATTACK_KEY) ? 1u : 0)
        | (settings_get_value(&settings, AUTO_SWITCH_WEAPON_KEY) ? 2u : 0)
        | 4u
        | (settings_get_value(&settings, FOLLOWER_SKILLS_KEY) ? 8u : 0)
        | (coop_display_interactions_run() ? 16u : 0);
}
bool coop_engine_options_run_interaction(int64_t actor)
{
    int preference = coop_engine_options_get(actor, COOP_OPTION_RUN_INTERACTIONS);
    return preference >= 0 ? preference != 0 : coop_display_interactions_run();
}
bool coop_engine_options_apply(int64_t actor, unsigned expected, unsigned mask)
{
    int member = coop_campaign_slot_for_object(actor);
    if (!coop_authority_can_simulate() || member < 0 || member >= 4 || mask > 31 || expected != owners[member]) return false;
    owners[member] = (uint8_t)(0xc0 | mask | (owners[member] & 0x20)); return true;
}
bool coop_engine_options_immortal(int64_t actor)
{
    return actor && coop_campaign_active() && (coop_engine_options_record(actor) & 0x20) != 0;
}
bool coop_engine_options_immortal_set(unsigned member, bool enabled)
{
    if (!coop_authority_can_simulate() || !coop_campaign_active() || member >= 4) return false;
    int64_t actor = coop_campaign_object(member);
    /* This switch prevents death; resurrection remains a native spell/action. */
    if (!actor || (enabled && critter_is_dead(actor))) return false;
    if (!owners[member]) owners[member] = (uint8_t)(0xc0 | coop_engine_options_defaults());
    owners[member] = (uint8_t)((owners[member] & ~0x20u) | (enabled ? 0x20u : 0));
    if (enabled) object_hp_damage_set(actor, object_hp_damage_get(actor));
    return true;
}
void coop_engine_options_tick(void)
{
    CoopGameplay* game = coop_runtime_gameplay();
    if (!game || coop_runtime_entry_pending() || coop_engine_dialogue_pending()
        || coop_dialogue_current(coop_gameplay_dialogue(game))->active) return;
    int64_t actor = player_get_local_pc_obj();
    if (actor && !coop_engine_options_record(actor)) coop_progression_defaults(coop_gameplay_progression(game), coop_engine_options_defaults());
}
