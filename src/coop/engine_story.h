#ifndef ARCMP_ENGINE_STORY_H
#define ARCMP_ENGINE_STORY_H
#include "coop/story_state.h"
bool coop_engine_story_capture(CoopStoryState* state);
bool coop_engine_story_matches_content(const CoopStoryState* state);
/* Only call after validation of the entire enclosing world batch. This copies
 * state and refreshes presentation; it does not execute campaign consequences. */
bool coop_engine_story_install(const CoopStoryState* state);
#endif
