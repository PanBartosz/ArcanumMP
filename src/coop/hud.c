#include "coop/localization.h"
#include "coop/hud.h"
#include "game/hrp.h"
#include <stdio.h>
#include <string.h>

typedef struct Placement { TigRect rect; int opacity; bool placed, changed; tig_window_handle_t window; } Placement;
static Placement placements[HUD_COUNT];
static bool loaded, editing, log_visible, log_compact = true;
static struct {
    bool active, resize;
    unsigned element;
    tig_window_handle_t window;
    TigRect origin;
    TigRect* rect;
    int x, y, min_width, min_height;
} drag;
static const char* names[HUD_COUNT] = {
    "Compact combat log", "Expanded combat log", "Play tools", "Extra action rows",
    "Quest tracker", "Turn order", "Party portraits", "Pause banner", "Combat preview",
    "Combat log button", "Hover tooltips", "Conversation panel"
};
int coop_hud_text_width(const char* text)
{
    TigFont measure={.str=text};
    tig_font_push(coop_play_font(MM_COLOR_WHITE)); tig_font_measure(&measure); tig_font_pop();
    return measure.width;
}
int coop_hud_text_height(const char* text, int width)
{
    if (!text || !*text) return 0;
    TigFont measure={.str=text,.width=SDL_max(1,width)};
    tig_font_push(coop_play_font(MM_COLOR_WHITE)); tig_font_measure(&measure); tig_font_pop();
    return measure.height;
}
void coop_hud_text_fit(char* text, size_t size, int width, tig_font_handle_t font)
{
    if (size<4) return;
    TigFont measure={.str=text};
    tig_font_push(font); tig_font_measure(&measure);
    if (measure.width>width) {
        size_t end=SDL_min(strlen(text),size-4);
        do {
            while (end && ((unsigned char)text[end]&0xc0)==0x80) end--;
            memcpy(text+end,"...",4);
            measure.width=0; measure.height=0;
            tig_font_measure(&measure);
            if (measure.width<=width || !end) break;
            end--;
        } while (true);
        if (measure.width>width) *text=0;
    }
    tig_font_pop();
}
int coop_hud_card_height(const char* title, const char* body, int width)
{
    return 32+coop_hud_text_height(title,width-24)+coop_hud_text_height(body,width-24);
}
void coop_hud_card(tig_window_handle_t window, TigRect rect, const char* title, const char* body, bool active)
{
    coop_hud_frame(window,rect,active);
    int heading=coop_hud_text_height(title,rect.width-24);
    TigRect text={rect.x+12,rect.y+10,rect.width-24,SDL_min(heading,rect.height-20)};
    tig_font_push(coop_play_font(MM_COLOR_GOLD)); tig_window_text_write(window,coop_tr(title),&text); tig_font_pop();
    TigLine rule={rect.x+12,rect.y+heading+14,rect.x+rect.width-13,rect.y+heading+14};
    tig_window_line(window,&rule,tig_color_make(91,73,46));
    text.y=rect.y+heading+21; text.height=rect.y+rect.height-text.y-11;
    if (text.height>0) {
        tig_font_push(coop_play_font(MM_COLOR_WHITE)); tig_window_text_write(window,body,&text); tig_font_pop();
    }
}
static void load(void)
{
    if (loaded) return;
    loaded = true;
    for (unsigned i = 0; i < HUD_COUNT; i++) {
        placements[i].opacity = 100;
        placements[i].window = TIG_WINDOW_HANDLE_INVALID;
        placements[i].changed = true;
    }
    FILE* f = fopen("arcmp-ui.cfg", "rt");
    if (!f) return;
    char line[192];
    while (fgets(line, sizeof(line), f)) {
        unsigned i; int x, y, w, h, opacity, value;
        if (sscanf(line, "panel%u=%d,%d,%d,%d,%d", &i, &x, &y, &w, &h, &opacity) == 6
            && i < HUD_COUNT && x >= -32768 && x <= 32768 && y >= -32768 && y <= 32768
            && w >= 1 && w <= 16384 && h >= 1 && h <= 16384 && opacity >= 25 && opacity <= 100) {
            placements[i].rect = (TigRect){x,y,w,h}; placements[i].placed = true; placements[i].opacity = opacity;
        } else if (sscanf(line, "opacity%u=%d", &i, &opacity) == 2 && i < HUD_COUNT && opacity >= 25 && opacity <= 100) {
            placements[i].opacity = opacity;
        } else if (sscanf(line, "log_visible=%d", &value) == 1) log_visible = value == 1;
        else if (sscanf(line, "log_compact=%d", &value) == 1) log_compact = value != 0;
    }
    fclose(f);
}
static bool save(void)
{
    FILE* f = fopen("arcmp-ui.cfg.tmp", "wt");
    if (!f) return false;
    bool good = fprintf(f, "version=1\nlog_visible=%d\nlog_compact=%d\n", log_visible, log_compact) > 0;
    for (unsigned i = 0; i < HUD_COUNT; i++) {
        Placement* p = &placements[i];
        if (p->placed) good &= fprintf(f,"panel%u=%d,%d,%d,%d,%d\n",i,p->rect.x,p->rect.y,p->rect.width,p->rect.height,p->opacity) > 0;
        else good &= fprintf(f,"opacity%u=%d\n",i,p->opacity) > 0;
    }
    good = fclose(f) == 0 && good;
    return good && SDL_RenamePath("arcmp-ui.cfg.tmp", "arcmp-ui.cfg");
}
const char* coop_hud_name(unsigned i) { return i < HUD_COUNT ? coop_tr(names[i]) : coop_tr("Unknown panel"); }
static TigRect clamp_rect(TigRect r, int min_width, int min_height)
{
    int sw = hrp_iso_window_width_get(), sh = hrp_iso_window_height_get();
    r.width = SDL_clamp(r.width, SDL_min(min_width, sw - 4), sw - 4);
    r.height = SDL_clamp(r.height, SDL_min(min_height, sh - 4), sh - 4);
    r.x = SDL_clamp(r.x, 2, sw - r.width - 2); r.y = SDL_clamp(r.y, 2, sh - r.height - 2);
    return r;
}
TigRect coop_hud_rect(unsigned i, TigRect fallback, int min_width, int min_height)
{
    load();
    return clamp_rect(i < HUD_COUNT && placements[i].placed ? placements[i].rect : fallback, min_width, min_height);
}
bool coop_hud_store(unsigned i, const TigRect* rect)
{
    load(); if (i >= HUD_COUNT || !rect || rect->width <= 0 || rect->height <= 0) return false;
    Placement old = placements[i]; placements[i].rect = *rect; placements[i].placed = true;
    if (save()) return true;
    placements[i] = old; return false;
}
int coop_hud_opacity(unsigned i) { load(); return i < HUD_COUNT ? placements[i].opacity : 100; }
bool coop_hud_opacity_set(unsigned i, int percent)
{
    load(); if (i >= HUD_COUNT || percent < 25 || percent > 100) return false;
    int old = placements[i].opacity; placements[i].opacity = percent;
    if (!save()) { placements[i].opacity = old; return false; }
    placements[i].changed = true;
    if (placements[i].window != TIG_WINDOW_HANDLE_INVALID)
        tig_window_opacity_set(placements[i].window, (uint8_t)((percent * 255 + 50) / 100));
    return true;
}
void coop_hud_attach(unsigned i, tig_window_handle_t window)
{
    load(); if (i >= HUD_COUNT || window == TIG_WINDOW_HANDLE_INVALID) return;
    if (placements[i].window != window) placements[i].changed = true;
    placements[i].window = window;
    tig_window_opacity_set(window, (uint8_t)((placements[i].opacity * 255 + 50) / 100));
}
void coop_hud_detach(unsigned i)
{
    load(); if (i >= HUD_COUNT) return;
    placements[i].window = TIG_WINDOW_HANDLE_INVALID;
    if (drag.active && drag.element == i) {
        coop_hud_store(i, drag.rect); drag.active = false;
    }
}
bool coop_hud_changed(unsigned i)
{ load(); if (i >= HUD_COUNT) return false; bool changed = placements[i].changed; placements[i].changed = false; return changed; }
bool coop_hud_editing(void) { return editing; }
void coop_hud_refresh(void)
{
    load(); for (unsigned i=0;i<HUD_COUNT;i++) placements[i].changed=true;
}
void coop_hud_edit_toggle(void)
{
    load(); editing = !editing;
    if (drag.active) { coop_hud_store(drag.element, drag.rect); drag.active = false; }
    for (unsigned i = 0; i < HUD_COUNT; i++) placements[i].changed = true;
}
void coop_hud_reset_layout(void)
{
    load(); drag.active = false;
    for (unsigned i = 0; i < HUD_COUNT; i++) { placements[i].placed = false; placements[i].changed = true; }
    save();
}
bool coop_hud_log_visible(void) { load(); return log_visible; }
bool coop_hud_log_compact(void) { load(); return log_compact; }
void coop_hud_log_state(bool visible, bool compact)
{
    load(); if (visible == log_visible && compact == log_compact) return;
    log_visible = visible; log_compact = compact; save();
}
static void drag_update(int x, int y)
{
    TigRect r = drag.origin;
    if (drag.resize) { r.width += x - drag.x; r.height += y - drag.y; }
    else { r.x += x - drag.x; r.y += y - drag.y; }
    r = clamp_rect(r, drag.min_width, drag.min_height);
    if (!memcmp(&r, drag.rect, sizeof(r))) return;
    if (tig_window_resize(drag.window,r.width,r.height) != TIG_OK) return;
    tig_window_move(drag.window,r.x,r.y);
    *drag.rect = r; placements[drag.element].rect = r; placements[drag.element].placed = true;
    placements[drag.element].changed = true;
}
void coop_hud_tick(void)
{
    if (!drag.active) return;
    TigMouseState mouse;
    if (tig_mouse_get_state(&mouse) == TIG_OK) drag_update(mouse.x, mouse.y);
    if (!tig_mouse_button_is_down(TIG_MOUSE_BUTTON_LEFT)) {
        coop_hud_store(drag.element,drag.rect); drag.active = false;
    }
}
bool coop_hud_input(unsigned i, tig_window_handle_t window, TigRect* rect, TigMessage* msg,
    int title_width, int min_width, int min_height, bool always_editable)
{
    if (msg->type != TIG_MESSAGE_MOUSE || i >= HUD_COUNT) return false;
    int x = msg->data.mouse.x - rect->x, y = msg->data.mouse.y - rect->y;
    if (drag.active) {
        if (drag.element != i) return false;
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_MOVE || msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP)
            drag_update(msg->data.mouse.x, msg->data.mouse.y);
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
            coop_hud_store(i,rect); drag.active = false;
        }
        return true;
    }
    if (x < 0 || y < 0 || x >= rect->width || y >= rect->height || (!editing && !always_editable)) return false;
    bool resize = x >= rect->width - 18 && y >= rect->height - 18;
    bool move = x >= 3 && x < title_width && y >= 3 && y < 23;
    if ((resize || move) && msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
        drag.active = true; drag.resize = resize; drag.element = i; drag.window = window; drag.rect = rect;
        drag.origin = *rect; drag.x = msg->data.mouse.x; drag.y = msg->data.mouse.y;
        drag.min_width = min_width; drag.min_height = min_height;
        tig_window_move_on_top(window); return true;
    }
    return editing && !always_editable;
}
void coop_hud_handles(unsigned i, tig_window_handle_t window, const TigRect* rect, int title_width, bool always_editable)
{
    (void)i;
    if (!editing && !always_editable) return;
    coop_hud_art(window,HUD_ART_RESIZE,(TigRect){rect->width-16,rect->height-16,14,14});
    if (editing) {
        TigRect title = {3,3,SDL_max(12,title_width-3),20};
        coop_hud_surface(window,HUD_ART_BUTTON,title,false);
        coop_hud_art(window,HUD_ART_MOVE,(TigRect){6,5,16,16});
        if (title_width<110) return;
        tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12,MM_COLOR_GOLD));
        title.x += 24; title.width -= 28;
        tig_window_text_write(window,coop_tr("Drag to move"),&title); tig_font_pop();
    } else if (always_editable) coop_hud_art(window,HUD_ART_MOVE,(TigRect){8,9,14,14});
}
