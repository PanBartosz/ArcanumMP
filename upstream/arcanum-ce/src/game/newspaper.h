#ifndef ARCANUM_GAME_NEWSPAPER_H_
#define ARCANUM_GAME_NEWSPAPER_H_

#include "game/context.h"
#include "game/timeevent.h"

#define NEWSPAPER_CURRENT 4
#define NEWSPAPER_CAMPAIGN_COUNT 25
void newspaper_campaign_state_get(int32_t* queue, uint32_t* count);
void newspaper_campaign_state_restore(const int32_t* queue, uint32_t count);

bool newspaper_init(GameInitInfo* init_info);
void newspaper_reset(void);
void newspaper_exit(void);
bool newspaper_load(GameLoadInfo* load_info);
bool newspaper_save(TigFile* stream);
void newspaper_enqueue(int num, bool priority);
int newspaper_get(int index);
int64_t newspaper_create(int num, int64_t loc);
bool newspaper_timeevent_process(TimeEvent* timeevent);

#endif /* ARCANUM_GAME_NEWSPAPER_H_ */
