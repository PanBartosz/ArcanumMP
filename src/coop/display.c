#include "coop/display.h"
#include "game/gamelib.h"
#include "game/hrp.h"
#include "game/location.h"
#include "ui/intgame.h"
#include "ui/mainmenu_ui.h"
#include <stdio.h>
#include <string.h>

static bool loaded, widescreen = true, interactions_run = true;
static int ui_scale = 100;

static void load(void)
{
    if (loaded) return;
    loaded = true;
    FILE* file = fopen("arcmp-display.cfg", "rt");
    if (!file) return;
    char line[128], key[48]; int value;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%47[^=]=%d", key, &value) != 2) continue;
        if (!strcmp(key,"widescreen") && (value == 0 || value == 1)) widescreen = value != 0;
        else if (!strcmp(key,"ui_scale") && value >= 75 && value <= 200 && value % 25 == 0) ui_scale = value;
        else if (!strcmp(key,"run_interactions") && (value == 0 || value == 1)) interactions_run = value != 0;
    }
    fclose(file);
}
bool coop_display_widescreen(void) { load(); return widescreen; }
int coop_display_ui_scale(void) { load(); return ui_scale; }
bool coop_display_interactions_run(void) { load(); return interactions_run; }
bool coop_display_settings_set(bool wide, int scale, bool run)
{
    load();
    if (scale < 75 || scale > 200 || scale % 25) return false;
    FILE* file = fopen("arcmp-display.cfg.tmp", "wt");
    if (!file) return false;
    bool good = fprintf(file,"widescreen=%d\nui_scale=%d\nrun_interactions=%d\n",wide,scale,run)>0;
    good = fclose(file) == 0 && good;
    if (good) good = SDL_RenamePath("arcmp-display.cfg.tmp","arcmp-display.cfg");
    if (!good) return false;
    widescreen = wide; ui_scale = scale; interactions_run = run;
    return true;
}
void coop_display_startup(bool explicit_geometry)
{
    load();
    if (!explicit_geometry) tig_video_presentation_configure(widescreen,ui_scale);
}


static tig_window_handle_t screen_window = TIG_WINDOW_HANDLE_INVALID, world_window = TIG_WINDOW_HANDLE_INVALID;
static GameResizeInfo screen_view;
static TigRect world_rect;
static int zoom = 100;
static bool resize_failed;
static int pan_remainder_x, pan_remainder_y;

void coop_view_init(tig_window_handle_t window)
{
    screen_window = window;
    TigWindowData data;
    if (tig_window_data(window,&data) == TIG_OK)
        screen_view = (GameResizeInfo){ .window_handle=window, .window_rect=data.rect, .content_rect=data.rect };
    world_rect = screen_view.content_rect;
    zoom = 100;
}
void coop_view_exit(void)
{
    if (world_window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(world_window);
    world_window = screen_window = TIG_WINDOW_HANDLE_INVALID;
    zoom = 100;
}
bool coop_view_resize(const GameResizeInfo* screen, GameResizeInfo* world)
{
    if (screen_window == TIG_WINDOW_HANDLE_INVALID || screen->window_handle != screen_window) return false;
    resize_failed = false;
    pan_remainder_x = pan_remainder_y = 0;
    screen_view = *screen;
    if (zoom == 100) {
        if (world_window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(world_window);
        world_window = TIG_WINDOW_HANDLE_INVALID;
        world_rect = screen->content_rect;
        return false;
    }
    TigRect size = {0,0,(screen->content_rect.width*100+zoom/2)/zoom,(screen->content_rect.height*100+zoom/2)/zoom};
    if (world_window == TIG_WINDOW_HANDLE_INVALID || size.width != world_rect.width || size.height != world_rect.height) {
        tig_window_handle_t replacement;
        TigWindowData data = { .flags=TIG_WINDOW_HIDDEN, .rect=size, .background_color=0 };
        if (tig_window_create(&data,&replacement) != TIG_OK) { resize_failed = true; return false; }
        if (world_window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(world_window);
        world_window = replacement;
    }
    world_rect = size;
    *world = (GameResizeInfo){ .window_handle=world_window, .window_rect=size, .content_rect=size };
    return true;
}
void coop_view_present(void)
{
    if (world_window == TIG_WINDOW_HANDLE_INVALID) return;
    TigWindowBlitInfo blit = { .type=TIG_WINDOW_BLIT_WINDOW_TO_WINDOW,
        .src_window_handle=world_window, .src_rect=&world_rect,
        .dst_window_handle=screen_window, .dst_rect=&screen_view.content_rect,
        .vb_blit_flags=TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR };
    tig_window_blit(&blit);
}
void coop_view_point(int* x, int* y)
{
    if (world_window == TIG_WINDOW_HANDLE_INVALID) return;
    *x = (*x-screen_view.content_rect.x)*world_rect.width/screen_view.content_rect.width;
    *y = (*y-screen_view.content_rect.y)*world_rect.height/screen_view.content_rect.height;
}
void coop_view_screen_point(int* x, int* y)
{
    if (world_window == TIG_WINDOW_HANDLE_INVALID) return;
    *x = screen_view.content_rect.x + *x*screen_view.content_rect.width/world_rect.width;
    *y = screen_view.content_rect.y + *y*screen_view.content_rect.height/world_rect.height;
}
void coop_view_delta(int* dx, int* dy)
{
    if (world_window == TIG_WINDOW_HANDLE_INVALID) return;
    int x = *dx*world_rect.width + pan_remainder_x;
    int y = *dy*world_rect.height + pan_remainder_y;
    *dx = x/screen_view.content_rect.width;
    *dy = y/screen_view.content_rect.height;
    pan_remainder_x = x%screen_view.content_rect.width;
    pan_remainder_y = y%screen_view.content_rect.height;
}
bool coop_view_location_at(int x, int y, int64_t* location)
{
    coop_view_point(&x,&y);
    return location_at(x,y,location);
}
int coop_view_zoom(void) { return zoom; }
bool coop_view_zoom_set(int percent, int x, int y)
{
    if (screen_window == TIG_WINDOW_HANDLE_INVALID || percent < 50 || percent > 200) return false;
    /* SectorRect supports three 64-tile sectors per axis. Bound the expanded
     * isometric diamond including its 256px margin rather than overrunning it
     * on very large desktops. */
    int minimum = (screen_view.content_rect.width*100/80 + screen_view.content_rect.height*100/40 + 105)/106;
    percent = SDL_max(percent,SDL_max(50,minimum));
    if (percent == zoom || percent > 200) return false;
    int old_x=x, old_y=y;
    coop_view_point(&old_x,&old_y);
    int previous = zoom; zoom = percent;
    GameResizeInfo screen = screen_view;
    gamelib_resize(&screen);
    if (resize_failed || (percent != 100 && world_window == TIG_WINDOW_HANDLE_INVALID)) {
        zoom = previous; gamelib_resize(&screen); return false;
    }
    int new_x=x, new_y=y;
    coop_view_point(&new_x,&new_y);
    location_origin_scroll(new_x-old_x,new_y-old_y);
    gamelib_invalidate_rect(NULL);
    return true;
}
bool coop_view_wheel(TigMessage* message)
{
    if (message->type != TIG_MESSAGE_MOUSE || message->data.mouse.event != TIG_MESSAGE_MOUSE_WHEEL
        || !message->data.mouse.dy || !intgame_interface_is_visible() || mainmenu_ui_is_active()
        || intgame_map_pan_active()) return false;
    IntgameMode mode = intgame_mode_get();
    if (mode != INTGAME_MODE_MAIN && mode != INTGAME_MODE_SPELL && mode != INTGAME_MODE_SKILL
        && mode != INTGAME_MODE_ITEM && mode != INTGAME_MODE_FOLLOWER) return false;
    tig_window_handle_t top;
    if (tig_window_get_at_position(message->data.mouse.x,message->data.mouse.y,&top) != TIG_OK || top != screen_window) return false;
    static const int levels[] = {50,60,70,80,90,100,110,125,150,175,200};
    int next = zoom;
    if (message->data.mouse.dy > 0) {
        for (unsigned i=0; i<SDL_arraysize(levels); i++) if (levels[i]>zoom) { next=levels[i]; break; }
    } else {
        for (unsigned i=SDL_arraysize(levels); i>0; i--) if (levels[i-1]<zoom) { next=levels[i-1]; break; }
    }
    coop_view_zoom_set(next,message->data.mouse.x,message->data.mouse.y);
    return true;
}
