#include "coop/engine_story.h"
#include "coop/engine_combat.h"
#include "coop/engine_magic.h"
#include "coop/engine_environment.h"
#include "game/area.h"
#include "game/monstergen.h"
#include "game/newspaper.h"
#include "game/quest.h"
#include "game/rumor.h"
#include "game/script.h"
#include "game/timeevent.h"
#include "game/ui.h"
#include "ui/intgame.h"
#include <string.h>
static_assert(COOP_STORY_VARIABLES == SCRIPT_CAMPAIGN_VARIABLES, "script variable schema");
static_assert(COOP_STORY_FLAG_WORDS == SCRIPT_CAMPAIGN_FLAG_WORDS, "script flag schema");
static_assert(COOP_STORY_QUESTS == QUEST_CAMPAIGN_COUNT, "quest schema");
static_assert(COOP_STORY_RUMOR_BYTES == RUMOR_CAMPAIGN_BYTES, "rumor schema");
static_assert(COOP_STORY_GENERATORS == MONSTERGEN_CAMPAIGN_BYTES, "generator schema");
static_assert(COOP_STORY_NEWSPAPERS == NEWSPAPER_CAMPAIGN_COUNT, "newspaper schema");
bool coop_engine_story_capture(CoopStoryState* state)
{
    memset(state, 0, sizeof(*state));
    int count = area_count();
    if (count < 0 || count > COOP_STORY_AREAS) return false;
    state->area_count = (uint32_t)count;
    script_campaign_state_get(state->variables, state->flags, &state->story);
    if (!quest_campaign_states_get(state->quests) || !area_campaign_flags_get(state->areas, state->area_count)) return false;
    rumor_campaign_state_get(state->rumors);
    monstergen_campaign_state_get(state->generators);
    newspaper_campaign_state_get(state->newspapers, &state->newspaper_count);
    DateTime game, animation;
    timeevent_campaign_clock_get(&game, &animation);
    state->game_time = (CoopStoryClock) { game.days, game.milliseconds };
    state->animation_time = (CoopStoryClock) { animation.days, animation.milliseconds };
    return coop_engine_combat_capture(&state->combat) && coop_engine_magic_capture(&state->magic)
        && coop_engine_environment_capture(&state->environment) && coop_story_state_valid(state);
}
bool coop_engine_story_matches_content(const CoopStoryState* state)
{
    return coop_story_state_valid(state) && state->area_count == (uint32_t)area_count()
        && coop_engine_environment_matches_content(&state->environment);
}
bool coop_engine_story_install(const CoopStoryState* state)
{
    if (!coop_engine_story_matches_content(state)) return false;
    uint8_t previous_quests[COOP_STORY_QUESTS];
    bool previous_valid = quest_campaign_states_get(previous_quests);
    script_campaign_state_restore(state->variables, state->flags, state->story);
    quest_campaign_states_restore(state->quests);
    rumor_campaign_state_restore(state->rumors);
    monstergen_campaign_state_restore(state->generators);
    newspaper_campaign_state_restore(state->newspapers, state->newspaper_count);
    area_campaign_flags_restore(state->areas, state->area_count);
    DateTime game = { .days = state->game_time.days, .milliseconds = state->game_time.milliseconds };
    DateTime animation = { .days = state->animation_time.days, .milliseconds = state->animation_time.milliseconds };
    timeevent_campaign_clock_restore(&game, &animation);
    intgame_clock_refresh();
    if (!previous_valid || memcmp(previous_quests, state->quests, sizeof(previous_quests)))
        ui_toggle_primary_button(UI_PRIMARY_BUTTON_LOGBOOK, true);
    return coop_engine_combat_install(&state->combat) && coop_engine_magic_install(&state->magic)
        && coop_engine_environment_install(&state->environment);
}
