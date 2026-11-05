#include "coop/modern.h"
#include "coop/localization.h"
#include "coop/hud.h"
#include "coop/saves_ui.h"
#include "coop/runtime.h"
#include "coop/engine_dialogue.h"
#include "game/hrp.h"
#include "game/map.h"
#include "ui/mainmenu_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <tig/tig.h>
static tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds;
static CoopSavesMode mode;
static unsigned page, selection, confirm;
static uint32_t displayed, roster;
static bool editing, text_was_active, name_dirty, select_latest;
static const TigRect shortcuts[]={{372,8,112,28},{492,8,126,28},{626,8,116,28}};
static char name[COOP_CHECKPOINT_DESCRIPTION], feedback[180];
static uint64_t drawn;
static const TigRect buttons[] = { { 18, 350, 100, 30 }, { 126, 350, 100, 30 }, { 236, 350, 154, 30 },
    { 400, 350, 140, 30 }, { 550, 350, 92, 30 }, { 650, 350, 92, 30 } };
bool coop_saves_ui_active(void) { return window != TIG_WINDOW_HANDLE_INVALID; }
static const CoopSaveEntry* selected(void)
{
    CoopSaveCatalog* catalog = coop_runtime_save_catalog();
    const CoopSavePage* current = coop_save_catalog_current(catalog);
    return current && current->page == page && selection < current->count && !coop_save_catalog_pending(catalog) ? &current->entries[selection] : NULL;
}
static void text(const char* value, int x, int y, int width, MainMenuColor color)
{
    TigRect rect = { x, y, width, 24 };
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, color));
    tig_window_text_write(window, coop_tr_rendered(value), &rect);
    tig_font_pop();
}
static bool inside(const TigRect* rect, int x, int y)
{
    return x >= rect->x && y >= rect->y && x < rect->x + rect->width && y < rect->y + rect->height;
}
static void default_name(void)
{
    time_t now = time(NULL); struct tm* date = localtime(&now);
    if (!date || !strftime(name, sizeof(name), coop_tr("Campaign %Y-%m-%d %H:%M"), date)) snprintf(name, sizeof(name), coop_tr("Campaign save"));
}
static void choose(unsigned row)
{
    const CoopSavePage* current = coop_save_catalog_current(coop_runtime_save_catalog());
    if (current && current->page == page && row >= current->count) return;
    selection = row; confirm = 0; editing = name_dirty = false; feedback[0] = 0;
    const CoopSaveEntry* entry = selected();
    if (entry && entry->status == COOP_CHECKPOINT_OK) snprintf(name, sizeof(name), "%s",
        entry->slot >= COOP_SAVE_AUTOSAVE_FIRST ? coop_tr_rendered(entry->info.description) : entry->info.description);
    else default_name();
}
static void request_page(unsigned wanted)
{
    if (wanted >= COOP_SAVE_PAGES) return;
    if (wanted != page) { name_dirty = false; selection = 0; }
    page = wanted; confirm = 0; editing = false; feedback[0] = 0;
    select_latest = false;
    coop_save_catalog_request(coop_runtime_save_catalog(), page);
}
void coop_saves_ui_close(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    coop_save_catalog_cancel(coop_runtime_save_catalog());
    tig_window_destroy(window); window = TIG_WINDOW_HANDLE_INVALID;
    SDL_Window* native;
    if (!text_was_active && tig_video_window_get(&native) == TIG_OK) SDL_StopTextInput(native);
}
static bool can_use(const CoopSaveEntry* entry)
{
    if (!entry) return false;
    if (mode == COOP_SAVES_SAVE) return entry->slot < COOP_SAVE_SLOTS
        && (entry->status == COOP_CHECKPOINT_OK || entry->status == COOP_CHECKPOINT_MISSING) && name[0];
    if (mode == COOP_SAVES_RESUME) return entry->status == COOP_CHECKPOINT_OK && coop_session_is_host(coop_runtime_session())
        && coop_session_all_ready(coop_runtime_session());
    return entry->status == COOP_CHECKPOINT_OK;
}
static void primary(void)
{
    const CoopSaveEntry* entry = selected();
    if (!can_use(entry)) { snprintf(feedback, sizeof(feedback), coop_tr("Select an available save slot. All saved owners must be present to load.")); return; }
    if (mode == COOP_SAVES_SAVE && entry->status == COOP_CHECKPOINT_OK && confirm != 1) {
        confirm = 1; snprintf(feedback, sizeof(feedback), coop_tr("Replace the selected save? Choose Confirm save to continue.")); return;
    }
    bool good = mode == COOP_SAVES_RESUME ? coop_runtime_resume_slot(entry)
        : coop_runtime_checkpoint_request(mode == COOP_SAVES_SAVE ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD, entry, name);
    if (good) coop_saves_ui_close();
    else snprintf(feedback, sizeof(feedback), "%s", mode == COOP_SAVES_RESUME && coop_runtime_status()[0] ? coop_runtime_status() : coop_tr("The party cannot do that right now. Finish the current operation and refresh the list."));
}
static void remove_copy(void)
{
    const CoopSaveEntry* entry = selected();
    if (!entry || !entry->slot || entry->slot >= COOP_SAVE_SLOTS || entry->status != COOP_CHECKPOINT_OK || !coop_session_is_host(coop_runtime_session())) return;
    if (confirm != 2) { confirm = 2; snprintf(feedback, sizeof(feedback), coop_tr("Remove this local copy? Other players keep theirs. Choose Confirm remove.")); return; }
    if (coop_runtime_remove_slot(entry) == COOP_CHECKPOINT_OK) request_page(page);
    else { confirm = 0; snprintf(feedback, sizeof(feedback), coop_tr("The save changed or could not be removed. Refresh the list.")); }
}
static void draw(void)
{
    TigRect whole = { 0, 0, bounds.width, bounds.height };
    coop_hud_frame(window,whole,true);
    const char* heading = mode == COOP_SAVES_SAVE ? coop_tr("SAVE PARTY CAMPAIGN") : mode == COOP_SAVES_LOAD ? coop_tr("LOAD PARTY CAMPAIGN") : coop_tr("RESUME PARTY CAMPAIGN");
    text(heading, 18, 12, 345, MM_COLOR_GOLD);
    const char* jumps[]={coop_tr("Quick save"),coop_tr("Autosaves"),coop_tr("Recovery")};
    for (unsigned i=0;i<SDL_arraysize(shortcuts);i++) {
        coop_hud_surface(window,HUD_ART_BUTTON,shortcuts[i],false);
        text(jumps[i],shortcuts[i].x+7,shortcuts[i].y+5,shortcuts[i].width-14,MM_COLOR_GOLD);
    }
    char label[256];
    snprintf(label, sizeof(label), coop_tr("Saves for this party | Page %u/%u | %s"), page + 1, COOP_SAVE_PAGES,
        mode == COOP_SAVES_SAVE ? coop_tr("Choose a slot and name") : coop_tr("Loading requires every traveller's agreement"));
    text(label, 18, 40, 722, MM_COLOR_WHITE);
    CoopSaveCatalog* catalog = coop_runtime_save_catalog();
    const CoopSavePage* current = coop_save_catalog_current(catalog);
    bool pending = coop_save_catalog_pending(catalog) || !current || current->page != page;
    if (pending) text(coop_tr("Reading the host's saved campaigns..."), 30, 90, 700, MM_COLOR_WHITE);
    else for (unsigned row = 0; row < current->count; row++) {
        const CoopSaveEntry* entry = &current->entries[row];
        TigRect box = { 18, 72 + (int)row * 54, 724, 50 };
        if (row == selection) tig_window_box(window, &box, tig_color_make(186, 141, 67));
        char automatic[24];
        snprintf(automatic, sizeof(automatic), coop_tr("Autosave %u"), entry->slot >= COOP_SAVE_AUTOSAVE_FIRST ? entry->slot - COOP_SAVE_AUTOSAVE_FIRST + 1 : 0);
        const char* kind = entry->slot == 0 ? coop_tr("Quick save") : entry->slot == COOP_SAVE_RECOVERY ? coop_tr("Recovery")
            : entry->slot >= COOP_SAVE_AUTOSAVE_FIRST ? automatic : NULL;
        if (kind) snprintf(label, sizeof(label), "%s | %s", kind, entry->status == COOP_CHECKPOINT_OK
            ? (entry->slot >= COOP_SAVE_AUTOSAVE_FIRST ? coop_tr_rendered(entry->info.description) : entry->info.description) : "");
        else snprintf(label, sizeof(label), "%02u | %s", entry->slot, entry->status == COOP_CHECKPOINT_OK ? entry->info.description : entry->status == COOP_CHECKPOINT_MISSING ? coop_tr("Empty slot") : coop_tr("Unavailable save"));
        text(label, 30, box.y + 3, 700, row == selection ? MM_COLOR_GOLD : MM_COLOR_WHITE);
        if (entry->status == COOP_CHECKPOINT_OK) {
            char saved[48]; snprintf(saved, sizeof(saved), "%s", coop_tr("Unknown date")); time_t when = (time_t)entry->modified;
            struct tm* date = entry->modified <= INT64_MAX ? localtime(&when) : NULL;
            if (date) strftime(saved, sizeof(saved), "%d.%m.%Y %H:%M", date);
            char* area = NULL; unsigned members = 0;
            for (unsigned n = 0; n < COOP_MAX_PLAYERS; n++) members += (entry->info.members >> n) & 1u;
            if (!map_get_name((int)entry->info.map, &area)) area = NULL;
            snprintf(label, sizeof(label), coop_tr("%s | %s | %u travellers"), saved, area ? area : coop_tr("Campaign"), members);
        } else {
            const char* errors[] = { "", coop_tr("No save in this slot."), coop_tr("This save is damaged."), coop_tr("Requires different game files."), coop_tr("Belongs to a different party."), coop_tr("Could not read this save.") };
            snprintf(label, sizeof(label), "%s", entry->status <= COOP_CHECKPOINT_IO ? errors[entry->status] : coop_tr("Unavailable save."));
        }
        text(label, 30, box.y + 25, 700, MM_COLOR_GRAY);
    }
    const CoopSaveEntry* entry = selected();
    text(mode == COOP_SAVES_SAVE ? coop_tr("Save name (click to edit):") : coop_tr("Selected campaign:"), 20, 292, 400, MM_COLOR_GRAY);
    TigRect edit = { 18, 316, 724, 27 }; tig_window_box(window, &edit, tig_color_make(editing ? 232 : 130, editing ? 190 : 105, 67));
    text(name, 26, 317, 706, mode == COOP_SAVES_SAVE ? MM_COLOR_GOLD : MM_COLOR_WHITE);
    const char* labels[] = { "< Previous", coop_tr("Next >"), mode == COOP_SAVES_SAVE ? (confirm == 1 ? coop_tr("Confirm save") : entry && entry->status == COOP_CHECKPOINT_OK ? coop_tr("Overwrite...") : coop_tr("Save here"))
        : mode == COOP_SAVES_RESUME ? coop_tr("Resume campaign") : coop_tr("Propose load"), confirm == 2 ? coop_tr("Confirm remove") : coop_tr("Remove local"), coop_tr("Refresh"), coop_tr("Close") };
    for (unsigned i = 0; i < 6; i++) {
        bool enabled = i == 0 ? page > 0 : i == 1 ? page + 1 < COOP_SAVE_PAGES : i == 2 ? can_use(entry)
            : i == 3 ? entry && entry->status == COOP_CHECKPOINT_OK && entry->slot && entry->slot < COOP_SAVE_SLOTS && coop_session_is_host(coop_runtime_session()) : true;
        TigRect button = buttons[i];
        tig_window_box(window, &button, tig_color_make(130, 105, 67));
        text(labels[i], buttons[i].x + 5, buttons[i].y + 3, buttons[i].width - 8, enabled ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    }
    text(feedback[0] ? feedback : coop_tr("Left/Right: pages | Home: quick save | End: recovery/autosaves | Enter: choose | Esc: close"), 18, 390, 724, MM_COLOR_WHITE);
    drawn = SDL_GetTicks();
}
static bool input(TigMessage* message)
{
    if (message->type == TIG_MESSAGE_KEYBOARD && message->data.keyboard.pressed) {
        SDL_Scancode key = message->data.keyboard.scancode;
        if (key == SDL_SCANCODE_ESCAPE) { if (confirm) { confirm = 0; feedback[0] = 0; } else coop_saves_ui_close(); }
        else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) primary();
        else if (key == SDL_SCANCODE_PAUSE) coop_play_pause_toggle();
        else if (key == SDL_SCANCODE_TAB && mode == COOP_SAVES_SAVE) editing = !editing;
        else if (editing && key == SDL_SCANCODE_BACKSPACE) { size_t size = strlen(name); if (size) name[size - 1] = 0; name_dirty = true; confirm = 0; }
        else if (!editing) {
            if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_PAGEUP) { if (page) request_page(page - 1); }
            else if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_PAGEDOWN) request_page(page + 1);
            else if (key == SDL_SCANCODE_HOME) request_page(0);
            else if (key == SDL_SCANCODE_END) { request_page(COOP_SAVE_PAGES - 1); select_latest=true; }
            else if (key == SDL_SCANCODE_R) request_page(page);
            else if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_4) choose((unsigned)(key - SDL_SCANCODE_1));
        }
    } else if (message->type == TIG_MESSAGE_TEXT_INPUT && editing && mode == COOP_SAVES_SAVE) {
        size_t size = strlen(name);
        const unsigned char* value = (const unsigned char*)message->data.text.text;
        for (; *value && size + 1 < sizeof(name); value++) if (*value >= 32 && *value <= 126) name[size++] = (char)*value;
        name[size] = 0; name_dirty = true; confirm = 0;
    } else if (message->type == TIG_MESSAGE_MOUSE && message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
        int x = message->data.mouse.x - bounds.x, y = message->data.mouse.y - bounds.y;
        if (y >= 72 && y < 288 && x >= 18 && x < 742) choose((unsigned)(y - 72) / 54);
        else if (y >= 316 && y < 343 && x >= 18 && x < 742 && mode == COOP_SAVES_SAVE) { editing = true; confirm = 0; }
        else if (inside(&shortcuts[0],x,y)) request_page(0);
        else if (inside(&shortcuts[1],x,y)) { request_page(COOP_SAVE_PAGES-1); select_latest=true; }
        else if (inside(&shortcuts[2],x,y)) request_page(COOP_SAVE_PAGES-1);
        else for (unsigned i = 0; i < 6; i++) if (inside(&buttons[i], x, y)) {
            if (i == 0 && page) request_page(page - 1);
            else if (i == 1) request_page(page + 1);
            else if (i == 2) primary();
            else if (i == 3) remove_copy();
            else if (i == 4) request_page(page);
            else if (i == 5) coop_saves_ui_close();
            break;
        }
    }
    if (window != TIG_WINDOW_HANDLE_INVALID) draw();
    return message->type == TIG_MESSAGE_MOUSE || message->type == TIG_MESSAGE_KEYBOARD || message->type == TIG_MESSAGE_TEXT_INPUT;
}
bool coop_saves_ui_open(CoopSavesMode requested)
{
    if (requested > COOP_SAVES_RESUME || !coop_runtime_save_catalog()) return false;
    CoopRuntimePhase state = coop_runtime_phase();
    if (requested == COOP_SAVES_RESUME ? state != COOP_RUNTIME_LOBBY : state != COOP_RUNTIME_PLAYING) return false;
    if (requested == COOP_SAVES_RESUME && !coop_session_is_host(coop_runtime_session())) return false;
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    if (!control || control->phase != COOP_CONTROL_IDLE || coop_controls_pending(coop_runtime_controls())) return false;
    CoopGameplay* game = coop_runtime_gameplay();
    if (game && (coop_dialogue_current(coop_gameplay_dialogue(game))->active || coop_engine_dialogue_pending() || coop_gameplay_finishing(game))) return false;
    coop_saves_ui_close(); mode = requested;
    bounds = (TigRect) { 20, 60, 760, 430 }; hrp_apply(&bounds, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
    TigWindowData data = { .rect = bounds, .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
        .message_filter = input, .background_color = tig_color_make(36, 26, 18) };
    if (tig_window_create(&data, &window) != TIG_OK) { window = TIG_WINDOW_HANDLE_INVALID; return false; }
    SDL_Window* native;
    text_was_active = false;
    if (tig_video_window_get(&native) == TIG_OK) { text_was_active = SDL_TextInputActive(native); SDL_StartTextInput(native); }
    displayed = page = selection = 0; name_dirty = false; roster = coop_session_revision(coop_runtime_session()); default_name(); request_page(0); draw();
    return true;
}
bool coop_saves_ui_open_autosaves(CoopSavesMode requested)
{
    if (!coop_saves_ui_open(requested)) return false;
    request_page(COOP_SAVE_PAGES-1); select_latest=true; draw(); return true;
}
void coop_saves_ui_tick(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    CoopRuntimePhase state = coop_runtime_phase();
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    CoopGameplay* game = coop_runtime_gameplay();
    if ((mode == COOP_SAVES_RESUME ? state != COOP_RUNTIME_LOBBY : state != COOP_RUNTIME_PLAYING)
        || !control || control->phase != COOP_CONTROL_IDLE
        || (game && (coop_dialogue_current(coop_gameplay_dialogue(game))->active || coop_engine_dialogue_pending()))) { coop_saves_ui_close(); return; }
    if (roster != coop_session_revision(coop_runtime_session())) {
        roster = coop_session_revision(coop_runtime_session()); request_page(page);
    }
    const CoopSavePage* current = coop_save_catalog_current(coop_runtime_save_catalog());
    if (current && current->page == page && !coop_save_catalog_pending(coop_runtime_save_catalog()) && current->serial != displayed) {
        displayed = current->serial; confirm = 0;
        if (select_latest && !name_dirty) {
            unsigned newest=0; uint64_t modified=0; bool found=false;
            for (unsigned i=0;i<current->count;i++) {
                const CoopSaveEntry* entry=&current->entries[i];
                if (entry->slot>=COOP_SAVE_AUTOSAVE_FIRST && entry->status==COOP_CHECKPOINT_OK
                    && (!found || entry->modified>=modified)) { newest=i; modified=entry->modified; found=true; }
            }
            choose(newest); select_latest=false;
            if (!found) snprintf(feedback,sizeof(feedback),"%s",coop_tr("No autosave is available for this party yet."));
            else snprintf(feedback,sizeof(feedback),"%s",coop_tr("Latest autosave selected. Choose Propose load to ask the party."));
        } else if (!name_dirty) choose(selection < current->count ? selection : 0);
        draw();
    } else if (SDL_GetTicks() - drawn >= 250) draw();
}
