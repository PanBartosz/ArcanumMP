#include "coop/modern.h"
#include "coop/hud.h"
#include "coop/dialogue_ui.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_journal.h"
#include "coop/lobby_ui.h"
#include "game/hrp.h"
#include "ui/mainmenu_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tig/tig.h>
enum { LINES = 8192,
    RESERVED = 0 };
#define LINE_HEIGHT coop_play_line_height()
#define CONTENT_Y (2 * LINE_HEIGHT + 16)
#define VISIBLE ((unsigned)SDL_max(1,(bounds.height - CONTENT_Y - LINE_HEIGHT - 10) / LINE_HEIGHT))
typedef struct DialogueLine {
    char* text;
    MainMenuColor color;
    int option;
} DialogueLine;
static tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds;
static CoopDialogueView page_view;
static char page_speaker[COOP_JOURNAL_SPEAKER];
static bool browsing;
static unsigned history_count, history_index, line_count, scroll;
static DialogueLine lines[LINES];
static bool can_choose;
static void draw(void);
static bool latest(void) { return !browsing && history_count && history_index + 1 == history_count; }
static bool page(void) { return coop_journal_page(coop_engine_journal(), history_index, &page_view, page_speaker); }
static void clear_lines(void)
{
    for (unsigned i = 0; i < line_count; i++) free(lines[i].text);
    line_count = 0;
}
static bool line(const char* text, size_t size, MainMenuColor color, int option)
{
    if (line_count >= LINES) return false;
    char* copy = malloc(size + 1);
    if (!copy) return false;
    memcpy(copy, text, size);
    copy[size] = 0;
    lines[line_count++] = (DialogueLine) { copy, color, option };
    return true;
}
static bool wrap(const char* text, MainMenuColor color, int option)
{
    tig_font_push(coop_play_font(color));
    while (*text) {
        while (*text == ' ') text++;
        if (!*text) break;
        char buffer[COOP_DIALOGUE_TEXT + 16];
        size_t count = 0, space = 0;
        bool overflow = false;
        while (text[count] && text[count] != '\n' && text[count] != '\r' && count + 1 < sizeof(buffer)) {
            buffer[count] = text[count] == '\t' ? ' ' : text[count];
            buffer[count + 1] = 0;
            TigFont measure = { .str = buffer };
            tig_font_measure(&measure);
            if (measure.width > bounds.width - 40) { overflow = true; break; }
            if (buffer[count] == ' ') space = count;
            count++;
        }
        if (overflow && space) count = space;
        if (!count && *text != '\n' && *text != '\r') count = 1;
        if (!line(buffer, count, color, option)) { tig_font_pop(); return false; }
        text += count;
        if (*text == '\r') text++;
        if (*text == '\n') text++;
    }
    tig_font_pop();
    return true;
}
static bool layout(void)
{
    clear_lines();
    scroll = 0;
    if (!history_count || !page()) return true;
    const CoopDialogueView* view = &page_view;
    if (view->selected[0] && view->mode != COOP_DIALOGUE_FLOAT) {
        if (!wrap(view->selected, MM_COLOR_GREEN, -1) || !line("", 0, MM_COLOR_WHITE, -1)) return false;
    }
    if (!wrap(view->reply, MM_COLOR_WHITE, -1) || !line("", 0, MM_COLOR_WHITE, -1)) return false;
    if (view->mode == 3 || view->mode == 5 || view->mode == 6 || view->mode == 7 || view->mode == 8 || view->mode == 9) {
        char status[128];
        const char* action = view->mode == 3 ? "exchanging items" : view->mode == 5 ? "viewing a character sheet"
            : view->mode == 6                                                       ? "studying a map"
            : view->mode == 7                                                       ? "combining schematic components"
            : view->mode == 8                                                       ? "having items identified"
                                                                                    : "having items repaired";
        snprintf(status, sizeof(status), "%s is %s.", view->chooser_name, action);
        if (!wrap(status, MM_COLOR_GOLD, -1) || !line("", 0, MM_COLOR_WHITE, -1)) return false;
    }
    for (unsigned i = 0; i < view->option_count; i++) {
        char option[COOP_DIALOGUE_TEXT + 16];
        snprintf(option, sizeof(option), "%u. %s", i + 1, view->options[i]);
        if (!wrap(option, latest() && can_choose ? MM_COLOR_GOLD : MM_COLOR_WHITE, (int)i)
            || !line("", 0, MM_COLOR_WHITE, -1)) return false;
    }
    return true;
}
static void text(const char* value, int x, int y, int width, MainMenuColor color)
{
    TigRect rect = { x, y, width, LINE_HEIGHT };
    tig_font_push(coop_play_font(color));
    tig_window_text_write(window, value, &rect);
    tig_font_pop();
}
static void draw(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    TigRect rect = { 0, 0, bounds.width, bounds.height };
    coop_hud_frame(window,rect,false);
    page();
    const CoopDialogueView* view = &page_view;
    char title[256];
    snprintf(title, sizeof(title), "%s  |  %s chooses", page_speaker, view->chooser_name);
    text(title, 30, 9, bounds.width-284, MM_COLOR_GOLD);
    text("< Previous", bounds.width - 236, 9, 115, history_index ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    text("Next >", bounds.width - 110, 9, 94, history_index + 1 == history_count ? MM_COLOR_GRAY : MM_COLOR_GOLD);
    snprintf(title, sizeof(title), "%s | Page %u of %u | F8: conversation journal", latest() ? can_choose ? "Your reply" : "Observing" : "Saved conversation history", history_count ? history_index + 1 : 0, history_count);
    text(title, 16, LINE_HEIGHT + 12, 720, MM_COLOR_GRAY);
    for (unsigned i = 0; i < VISIBLE && scroll + i < line_count; i++) {
        DialogueLine* item = &lines[scroll + i];
        text(item->text, 20, CONTENT_Y + (int)i * LINE_HEIGHT, bounds.width - 40, item->color);
    }
    char footer[160];
    snprintf(footer, sizeof(footer), "Wheel / Up / Down: scroll   Left / Right: history   %u-%u of %u lines",
        line_count ? scroll + 1 : 0, scroll + VISIBLE < line_count ? scroll + VISIBLE : line_count, line_count);
    text(footer, 16, bounds.height - LINE_HEIGHT - 4, bounds.width - 32, MM_COLOR_GRAY);
    coop_hud_handles(HUD_DIALOGUE,window,&bounds,bounds.width-244,true);
}
static bool input(TigMessage* msg)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return false;
    if (coop_hud_input(HUD_DIALOGUE,window,&bounds,msg,bounds.width-244,600,220,true)) {
        if (coop_hud_changed(HUD_DIALOGUE)) { layout(); draw(); }
        return true;
    }
    int change = 0, page = 0, option = -1;
    if (msg->type == TIG_MESSAGE_MOUSE) {
        int x = msg->data.mouse.x - bounds.x, y = msg->data.mouse.y - bounds.y;
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL) change = msg->data.mouse.dy > 0 ? -3 : 3;
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
            if (y >= 5 && y < 28) {
                if (x >= bounds.width - 236 && x < bounds.width - 121) page = -1;
                if (x >= bounds.width - 110 && x < bounds.width - 16) page = 1;
            }
            if (x >= 20 && x < bounds.width - 20 && y >= CONTENT_Y && y < CONTENT_Y + VISIBLE * LINE_HEIGHT) {
                unsigned index = scroll + (unsigned)(y - CONTENT_Y) / LINE_HEIGHT;
                if (index < line_count) option = lines[index].option;
            }
        }
    } else if (msg->type == TIG_MESSAGE_KEYBOARD && msg->data.keyboard.pressed) {
        SDL_Scancode key = msg->data.keyboard.scancode;
        if (key == SDL_SCANCODE_UP) change = -1;
        if (key == SDL_SCANCODE_DOWN) change = 1;
        if (key == SDL_SCANCODE_PAGEUP) change = -VISIBLE;
        if (key == SDL_SCANCODE_PAGEDOWN) change = VISIBLE;
        if (key == SDL_SCANCODE_HOME) change = -(int)line_count;
        if (key == SDL_SCANCODE_END) change = (int)line_count;
        if (key == SDL_SCANCODE_LEFT) page = -1;
        if (key == SDL_SCANCODE_RIGHT) page = 1;
        if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_5) option = key - SDL_SCANCODE_1;
        if (key == SDL_SCANCODE_ESCAPE) {
            if (browsing) {
                coop_dialogue_ui_hide();
                return true;
            }
            if (latest() && can_choose) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
        }
        if (key == SDL_SCANCODE_F8 && browsing) {
            coop_dialogue_ui_hide();
            return true;
        }
        if (key == SDL_SCANCODE_F12) coop_lobby_open();
    }
    if ((page < 0 && history_index) || (page > 0 && history_index + 1 < history_count)) {
        history_index = (unsigned)((int)history_index + page);
        layout();
    }
    if (change) {
        int position = (int)scroll + change;
        unsigned maximum = line_count > VISIBLE ? line_count - VISIBLE : 0;
        scroll = position < 0 ? 0 : (unsigned)position > maximum ? maximum : (unsigned)position;
    }
    if (option >= 0 && latest() && can_choose && (unsigned)option < page_view.option_count)
        coop_engine_dialogue_submit((unsigned)option);
    if (page || change) draw();
    return msg->type == TIG_MESSAGE_MOUSE || msg->type == TIG_MESSAGE_KEYBOARD || msg->type == TIG_MESSAGE_TEXT_INPUT;
}
static bool open_window(void)
{
    if (window != TIG_WINDOW_HANDLE_INVALID) return true;
    bounds = (TigRect) { 20, 70, 760, 370 };
    hrp_apply(&bounds, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
    bounds=coop_hud_rect(HUD_DIALOGUE,bounds,600,220);
    TigWindowData data = { 0 };
    data.rect = bounds;
    data.flags = TIG_WINDOW_MESSAGE_FILTER | TIG_WINDOW_ALWAYS_ON_TOP;
    data.message_filter = input;
    data.background_color = tig_color_make(36, 26, 18);
    if (tig_window_create(&data, &window) != TIG_OK) {
        window = TIG_WINDOW_HANDLE_INVALID;
        return false;
    }
    coop_hud_attach(HUD_DIALOGUE,window);
    return true;
}
bool coop_dialogue_ui_show(const CoopDialogueView* view, bool chooser, const char* speaker)
{
    (void)view;
    (void)speaker;
    bool follow = window == TIG_WINDOW_HANDLE_INVALID || browsing || !history_count || history_index + 1 == history_count;
    if (!open_window()) return false;
    history_count = coop_journal_count(coop_engine_journal());
    if (follow || history_index >= history_count) history_index = history_count ? history_count - 1 : 0;
    browsing = false;
    can_choose = chooser;
    if (!layout()) return false;
    draw();
    return true;
}
bool coop_dialogue_ui_browse(void)
{
    if (!coop_journal_count(coop_engine_journal())) return false;
    if (window != TIG_WINDOW_HANDLE_INVALID) return false;
    if (!open_window()) return false;
    history_count = coop_journal_count(coop_engine_journal());
    history_index = history_count - 1;
    browsing = true;
    can_choose = false;
    if (!layout()) {
        coop_dialogue_ui_hide();
        return false;
    }
    draw();
    return true;
}
bool coop_dialogue_ui_browsing(void) { return browsing && window != TIG_WINDOW_HANDLE_INVALID; }
unsigned coop_dialogue_ui_index(void) { return history_index; }
void coop_dialogue_ui_hide(void)
{
    coop_hud_detach(HUD_DIALOGUE);
    if (window != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(window);
    window = TIG_WINDOW_HANDLE_INVALID;
    clear_lines();
}
void coop_dialogue_ui_reset(void)
{
    coop_dialogue_ui_hide();
    history_count = history_index = 0;
    memset(&page_view, 0, sizeof(page_view));
    page_speaker[0] = 0;
    browsing = can_choose = false;
}
