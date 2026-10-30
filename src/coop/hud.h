#ifndef ARCMP_HUD_H
#define ARCMP_HUD_H
#include "coop/modern.h"

typedef enum CoopHudElement {
    HUD_LOG_COMPACT, HUD_LOG, HUD_TOOLS, HUD_ACTIONS, HUD_QUESTS, HUD_TURNS,
    HUD_PARTY, HUD_PAUSE, HUD_PREVIEW, HUD_LOG_BUTTON, HUD_TOOLTIP, HUD_DIALOGUE,
    HUD_COUNT
} CoopHudElement;

typedef enum CoopHudArt {
    HUD_ART_PANEL, HUD_ART_JOURNAL,
    HUD_ART_DEAD, HUD_ART_UNCONSCIOUS, HUD_ART_POISONED, HUD_ART_TIRED,
    HUD_ART_STUNNED, HUD_ART_PARALYZED, HUD_ART_BLINDED, HUD_ART_INJURED, HUD_ART_ENCUMBERED,
    HUD_ART_CLOSE, HUD_ART_COMPACT, HUD_ART_EXPAND, HUD_ART_WIDE, HUD_ART_OPACITY,
    HUD_ART_MOVE, HUD_ART_RESIZE, HUD_ART_TURN, HUD_ART_TURN_ACTIVE,
    HUD_ART_ACTION_SLOT, HUD_ART_PORTRAIT, HUD_ART_BUTTON, HUD_ART_COUNT
} CoopHudArt;
struct PlayRow;

const char* coop_hud_name(unsigned element);
TigRect coop_hud_rect(unsigned element, TigRect fallback, int min_width, int min_height);
bool coop_hud_store(unsigned element, const TigRect* rect);
void coop_hud_attach(unsigned element, tig_window_handle_t window);
void coop_hud_detach(unsigned element);
void coop_hud_tick(void);
bool coop_hud_input(unsigned element, tig_window_handle_t window, TigRect* rect,
    TigMessage* message, int title_width, int min_width, int min_height, bool always_editable);
void coop_hud_handles(unsigned element, tig_window_handle_t window, const TigRect* rect, int title_width, bool always_editable);
bool coop_hud_changed(unsigned element);
bool coop_hud_editing(void);
void coop_hud_edit_toggle(void);
void coop_hud_reset_layout(void);
void coop_hud_refresh(void);
int coop_hud_opacity(unsigned element);
bool coop_hud_opacity_set(unsigned element, int percent);
bool coop_hud_log_visible(void);
bool coop_hud_log_compact(void);
void coop_hud_log_state(bool visible, bool compact);
void coop_hud_frame(tig_window_handle_t window, TigRect rect, bool active);
int coop_hud_text_width(const char* text);
int coop_hud_text_height(const char* text, int width);
void coop_hud_text_fit(char* text, size_t size, int width, tig_font_handle_t font);
int coop_hud_card_height(const char* title, const char* body, int width);
void coop_hud_card(tig_window_handle_t window, TigRect rect, const char* title, const char* body, bool active);
void coop_hud_icon(tig_window_handle_t window, TigRect rect, bool active);
void coop_hud_surface(tig_window_handle_t window, unsigned art, TigRect rect, bool active);
bool coop_hud_art(tig_window_handle_t window, unsigned art, TigRect rect);
const char* coop_hud_art_name(unsigned art);
bool coop_hud_action_icon(tig_window_handle_t window, const struct PlayRow* row, TigRect rect);
#endif
