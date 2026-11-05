#ifndef ARCMP_DISPLAY_H
#define ARCMP_DISPLAY_H
#include "game/context.h"

/* Personal presentation settings. Never part of authoritative campaign state. */
bool coop_display_widescreen(void);
int coop_display_ui_scale(void);
bool coop_display_interactions_run(void);
bool coop_display_settings_set(bool widescreen, int ui_scale, bool run);
void coop_display_startup(bool explicit_geometry);

/* Native world canvas is separate from the unscaled UI coordinate space. */
void coop_view_init(tig_window_handle_t window);
void coop_view_exit(void);
bool coop_view_resize(const GameResizeInfo* screen, GameResizeInfo* world);
void coop_view_present(void);
void coop_view_critical_shake(void);
void coop_view_effect_tick(uint64_t now);
void coop_view_effect_reset(void);
bool coop_view_shaking(void);
bool coop_view_wheel(TigMessage* message);
void coop_view_point(int* x, int* y);
void coop_view_screen_point(int* x, int* y);
void coop_view_delta(int* dx, int* dy);
bool coop_view_location_at(int x, int y, int64_t* location);
int coop_view_zoom(void);
bool coop_view_zoom_set(int percent, int x, int y);
#endif
