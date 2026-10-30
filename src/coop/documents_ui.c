#include "coop/dialogue_ui.h"
#include "coop/engine_documents.h"
#include "coop/runtime.h"
#include "game/hrp.h"
#include "ui/mainmenu_ui.h"
#include "ui/written_ui.h"
#include <stdio.h>
#include <string.h>
#include <tig/tig.h>
enum { ROWS = 9,
    ROW_HEIGHT = 30,
    ROW_Y = 66 };
static tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds;
static unsigned first;
bool coop_documents_ui_active(void) { return window != TIG_WINDOW_HANDLE_INVALID; }
static void text(const char* value, int x, int y, int width, MainMenuColor color)
{
    TigRect rect = { x, y, width, 22 };
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, color));
    tig_window_text_write(window, value, &rect);
    tig_font_pop();
}
void coop_documents_ui_close(void)
{
    if (window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(window);
    window = TIG_WINDOW_HANDLE_INVALID;
}
void coop_documents_ui_refresh(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    const CoopDocuments* documents = coop_engine_documents();
    if (first >= documents->count) first = documents->count ? ((documents->count - 1) / ROWS) * ROWS : 0;
    TigRect rect = { 0, 0, bounds.width, bounds.height };
    tig_window_fill(window, &rect, tig_color_make(36, 26, 18));
    tig_window_box(window, &rect, tig_color_make(186, 141, 67));
    char label[256];
    snprintf(label, sizeof(label), "Party Documents | %u collected", documents->count);
    text(label, 16, 10, 440, MM_COLOR_GOLD);
    text("< Previous", 500, 10, 110, first ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    text("Next >", 635, 10, 95, first + ROWS < documents->count ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    text("Discovered documents are available to the whole party.", 16, 35, 720, MM_COLOR_WHITE);
    if (!documents->count) text("Read a book, note or newspaper to add it here.", 24, ROW_Y, 710, MM_COLOR_WHITE);
    for (unsigned row = 0; row < ROWS && first + row < documents->count; row++) {
        CoopDocument d = documents->records[first + row];
        char title[220];
        if (!written_ui_coop_title(d.type, (int)d.number, title, sizeof(title))) snprintf(title, sizeof(title), "Document %u", d.number);
        snprintf(label, sizeof(label), "%u. %s", row + 1, title);
        tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, MM_COLOR_GOLD));
        for (size_t n = strlen(label); n > 0; n--) {
            TigFont measure = { .str = label };
            tig_font_measure(&measure);
            if (measure.width <= bounds.width - 60) break;
            label[n - 1] = 0;
        }
        tig_font_pop();
        text(label, 24, ROW_Y + (int)row * ROW_HEIGHT, 710, MM_COLOR_GOLD);
    }
    text("1-9 / click: read    Left / Right: pages    Esc / F7: close", 16, bounds.height - 28, 720, MM_COLOR_GRAY);
}
static bool input(TigMessage* msg)
{
    int change = 0, selected = -1;
    if (msg->type == TIG_MESSAGE_KEYBOARD && msg->data.keyboard.pressed) {
        SDL_Scancode key = msg->data.keyboard.scancode;
        if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F7) {
            coop_documents_ui_close();
            return true;
        }
        if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_PAGEUP) change = -1;
        if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_PAGEDOWN) change = 1;
        if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) selected = key - SDL_SCANCODE_1;
    } else if (msg->type == TIG_MESSAGE_MOUSE) {
        int x = msg->data.mouse.x - bounds.x, y = msg->data.mouse.y - bounds.y;
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL) change = msg->data.mouse.dy > 0 ? -1 : 1;
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
            if (y >= 5 && y < 32) {
                if (x >= 490 && x < 620) change = -1;
                if (x >= 625) change = 1;
            }
            if (x >= 16 && x < bounds.width - 16 && y >= ROW_Y && y < ROW_Y + ROWS * ROW_HEIGHT) selected = (y - ROW_Y) / ROW_HEIGHT;
        }
    }
    const CoopDocuments* documents = coop_engine_documents();
    if (change < 0 && first >= ROWS) first -= ROWS;
    if (change > 0 && first + ROWS < documents->count) first += ROWS;
    if (selected >= 0 && first + (unsigned)selected < documents->count) {
        CoopDocument d = documents->records[first + (unsigned)selected];
        coop_documents_ui_close();
        written_ui_coop_present(d.type, (int)d.number, d.variant != 0);
        return true;
    }
    if (change) coop_documents_ui_refresh();
    return msg->type == TIG_MESSAGE_MOUSE || msg->type == TIG_MESSAGE_KEYBOARD || msg->type == TIG_MESSAGE_TEXT_INPUT;
}
bool coop_documents_ui_open(void)
{
    if (window != TIG_WINDOW_HANDLE_INVALID) {
        coop_documents_ui_close();
        return true;
    }
    CoopGameplay* game = coop_runtime_gameplay();
    if (!game || coop_runtime_phase() != COOP_RUNTIME_PLAYING || coop_dialogue_current(coop_gameplay_dialogue(game))->active) return false;
    if (coop_dialogue_ui_browsing()) coop_dialogue_ui_hide();
    written_ui_coop_stop();
    bounds = (TigRect) { 20, 70, 760, 370 };
    hrp_apply(&bounds, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
    TigWindowData data = { 0 };
    data.rect = bounds;
    data.flags = TIG_WINDOW_MESSAGE_FILTER | TIG_WINDOW_ALWAYS_ON_TOP;
    data.message_filter = input;
    data.background_color = tig_color_make(36, 26, 18);
    if (tig_window_create(&data, &window) != TIG_OK) {
        window = TIG_WINDOW_HANDLE_INVALID;
        return false;
    }
    first = 0;
    coop_documents_ui_refresh();
    return true;
}
