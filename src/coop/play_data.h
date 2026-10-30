#ifndef ARCMP_PLAY_DATA_H
#define ARCMP_PLAY_DATA_H
#include "game/context.h"
#include "ui/types.h"
enum { PLAY_ROWS = 2000, PLAY_EXTRA_SLOTS = 12 };
typedef struct PlayRow {
    int64_t object;
    int id, kind;
    bool marked, available;
    char name[160], detail[1600];
} PlayRow;
unsigned coop_play_inventory(PlayRow* rows,const char* query,unsigned category);
unsigned coop_play_actions(PlayRow* rows,const char* query);
unsigned coop_play_quests(PlayRow* rows,const char* query,bool recent);
unsigned coop_play_recipes(PlayRow* rows,const char* query,bool craftable);
void coop_play_data_bind(void);
bool coop_play_junk(int64_t object,bool toggle);
bool coop_play_pin(int quest,bool toggle);
unsigned coop_play_pin_at(unsigned index);
const char* coop_play_note(void);
bool coop_play_note_set(const char* text);
void coop_play_quests_seen(void);
void coop_play_action_activate(const PlayRow* row);
bool coop_play_action_assign(unsigned slot,const PlayRow* row);
bool coop_play_action_slot(unsigned slot,PlayRow* row);
void coop_play_bulk_start(void);
void coop_play_bulk_tick(void);
const char* coop_play_bulk_status(void);
void coop_play_data_reset(void);
#endif
