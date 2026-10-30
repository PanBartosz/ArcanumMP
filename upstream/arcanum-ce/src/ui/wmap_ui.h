#ifndef ARCANUM_UI_WMAP_UI_H_
#define ARCANUM_UI_WMAP_UI_H_

#include "game/context.h"
#include "game/timeevent.h"

bool wmap_ui_init(GameInitInfo* init_info);
void wmap_ui_exit(void);
void wmap_ui_reset(void);
bool wmap_ui_save(TigFile* stream);
bool wmap_ui_load(GameLoadInfo* load_info);
void wmap_ui_encounter_start(void);
bool wmap_ui_is_encounter(void);
void wmap_ui_encounter_end(void);
void wmap_ui_open(void);
void wmap_ui_play_tick(uint64_t now);
void wmap_ui_select(int64_t obj, int spell);
void wmap_ui_close(void);
int wmap_ui_is_created(void);
void wmap_ui_scroll(int direction);
bool wmap_ui_can_pan_at(int x, int y);
void wmap_ui_pan_by(int dx, int dy);
void sub_564000(int a1);
void wmap_ui_mark_townmap(int64_t obj);
bool wmap_ui_bkg_process_callback(TimeEvent* timeevent);
void wmap_ui_notify_sector_changed(int64_t pc_obj, int64_t sec);
void wmap_ui_get_current_location(int64_t* loc_ptr);
bool wmap_ui_coop_begin(int64_t destination);
bool wmap_ui_coop_begin_route(const int64_t* destinations, unsigned count);
bool wmap_ui_coop_moving(void);
void wmap_ui_coop_refresh(void);
void wmap_ui_coop_areas_changed(void);
bool wmap_ui_coop_select(uint64_t token, int64_t actor, int spell);
void wmap_ui_coop_dismiss_spell(void);
void wmap_ui_coop_notes_changed(void);
/* True when an active editor supplied specific conflict feedback. */
bool wmap_ui_coop_note_result(unsigned status);
/* Read-only projection of a shared annotation in the current map viewport. */
bool wmap_ui_coop_note_position(uint32_t id, int* x, int* y);
bool wmap_ui_coop_import_notes(void);

#endif /* ARCANUM_UI_WMAP_UI_H_ */
