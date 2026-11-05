#ifndef ARCMP_MODERN_H
#define ARCMP_MODERN_H
#include "coop/session.h"
#include "coop/play_events.h"
#include "game/context.h"
#include "ui/mainmenu_ui.h"

enum { COOP_PLAY_EVENT = 76, COOP_PLAY_PING = 77, COOP_PLAY_POLICY = 78, COOP_PLAY_MAP_PING = 79, COOP_PLAY_FEEDBACK = 80 };
enum { PLAY_PAUSE_ENEMY = 1, PLAY_PAUSE_CRITICAL = 2, PLAY_PAUSE_DOWN = 4, PLAY_PAUSE_BREAK = 8 };
enum { PLAY_PREF_TEXT, PLAY_PREF_CONTRAST, PLAY_PREF_LABELS, PLAY_PREF_EMPTY,
    PLAY_PREF_SYMBOLS, PLAY_PREF_PREVIEW, PLAY_PREF_BAR, PLAY_PREF_TIP_DELAY,
    PLAY_PREF_AUTOPAUSE, PLAY_PREF_AUTOSAVE_MINUTES, PLAY_PREF_AUTOSAVE_TRAVEL, PLAY_PREF_TURN_RADIUS, PLAY_PREF_CRIT_SHAKE, PLAY_PREF_COUNT };
enum { PLAY_TAB_LOG, PLAY_TAB_INVENTORY, PLAY_TAB_ACTIONS, PLAY_TAB_QUESTS,
    PLAY_TAB_CRAFT, PLAY_TAB_SETTINGS, PLAY_TABS };

int coop_play_pref(unsigned index);
bool coop_play_pref_set(unsigned index, int value);
tig_font_handle_t coop_play_font(MainMenuColor color);
tig_font_handle_t coop_play_chrome_font(MainMenuColor color);
int coop_play_line_height(void);
void coop_play_name(int64_t obj, char* text, size_t size);
unsigned coop_play_members(int64_t obj);
void coop_play_event(unsigned kind, int64_t actor, int64_t target, const char* summary, const char* details);
void coop_play_float(int64_t obj, const char* text);
void coop_play_status_change(int64_t obj,int field,unsigned before,unsigned after);
void coop_play_attack_miss(int64_t actor, int64_t target, bool critical);
void coop_play_damage(int64_t actor, int64_t target, int hp, int fatigue, unsigned flags, unsigned effects);
void coop_play_roll(int64_t actor,int64_t target,int skill,int effectiveness,int difficulty,int roll,int critical_roll,unsigned flags);
uint32_t coop_play_event_first(void);
uint32_t coop_play_event_end(void);
const PlayEvent* coop_play_event_at(uint32_t sequence);
void coop_play_network_tick(CoopSession* session, uint32_t epoch, unsigned ready);
bool coop_play_network_message(CoopSession* session, uint32_t epoch, uint8_t sender, int member,
    uint8_t type, const uint8_t* bytes, size_t size);
bool coop_play_ping(int64_t location);
bool coop_play_map_ping(unsigned context,unsigned x,unsigned y);
unsigned coop_play_pause_policy(void);
unsigned coop_play_turn_radius(void);
bool coop_play_turn_trigger(int64_t actor, const char* reason);
void coop_play_status_feedback(int64_t actor, const char* status, bool active);
void coop_play_critical_feedback(int64_t target);
unsigned coop_play_autosave_minutes(void);
bool coop_play_autosave_travel(void);
void coop_play_pause_toggle(void);
void coop_play_tick(uint64_t now);
void coop_play_reset(void);
bool coop_play_save(void);
bool coop_play_load(void);
void coop_play_draw_begin(void);
void coop_play_label_object(int64_t obj, const TigRect* rect);
/* Read-only query of replicated inventory; never runs loot or item scripts. */
bool coop_play_loot_empty(int64_t obj);
void coop_play_draw(tig_window_handle_t window);
bool coop_play_input(TigMessage* message);
unsigned coop_play_binding_count(void);
const char* coop_play_binding_name(unsigned index);
SDL_Scancode coop_play_binding_key(unsigned index);
SDL_Scancode coop_play_binding_original(unsigned index);
bool coop_play_binding_set(unsigned index, SDL_Scancode key);
void coop_play_binding_reset(void);
/* Expand shortcut tokens in UI hints using the current personal bindings. */
void coop_play_shortcuts(const char* source, char* text, size_t size);

bool coop_play_ui_open(unsigned tab);
bool coop_play_autosave_settings_open(void);
void coop_play_ui_close(void);
bool coop_play_ui_active(void);
bool coop_play_ui_blocking(void);
void coop_play_ui_tick(uint64_t now);
void coop_play_settings_link(bool show);
bool coop_play_control_hint(int x,int y,char* text,size_t size);
void coop_play_ui_reset(void);
void coop_play_world_reset(void);
#endif
