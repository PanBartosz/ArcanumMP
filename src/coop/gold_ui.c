#include "coop/gold_ui.h"
#include "coop/campaign.h"
#include "coop/hud.h"
#include "coop/localization.h"
#include "coop/runtime.h"
#include "game/critter.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/player.h"
#include "ui/mainmenu_ui.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tig/tig.h>

static tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds;
static const TigRect amount_box = { 22, 206, 396, 28 };
static const TigRect presets[] = { { 22, 242, 94, 26 }, { 126, 242, 94, 26 }, { 230, 242, 94, 26 } };
static const TigRect send_button = { 22, 324, 230, 32 }, close_button = { 270, 324, 148, 32 };
static unsigned recipient = COOP_MAX_PLAYERS, recipients[COOP_MAX_PLAYERS], recipient_count;
static char amount_text[12] = "1", feedback[200];
static bool pending, text_was_active, replace_amount;
static uint32_t previous_result;
static uint64_t drawn;

static bool inside(const TigRect* r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->width && y < r->y + r->height;
}
static bool online(unsigned member)
{
    const CoopCampaignMember* owner = coop_campaign_member(member);
    CoopSession* session = coop_runtime_session();
    if (!owner || !session) return false;
    const CoopPlayer* players = coop_session_players(session);
    CoopGameplay* game = coop_runtime_gameplay();
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++)
        if (players[i].connected && coop_gameplay_member_for_connection(game, i) == (int)member) return true;
    return false;
}
static bool nearby(int64_t pc, int64_t target)
{
    return pc && target && critter_is_active(pc) && critter_is_active(target)
        && location_dist(obj_field_int64_get(pc, OBJ_F_LOCATION), obj_field_int64_get(target, OBJ_F_LOCATION)) <= 8;
}
static int amount(void)
{
    char* end;
    errno = 0;
    long value = strtol(amount_text, &end, 10);
    return !errno && *amount_text && !*end && value > 0 && value <= INT_MAX ? (int)value : 0;
}
static void text(const char* value, TigRect rect, MainMenuColor color)
{
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, color));
    tig_window_text_write(window, value, &rect);
    tig_font_pop();
}
static void button(const TigRect* r, const char* label, bool enabled)
{
    coop_hud_surface(window, HUD_ART_BUTTON, *r, false);
    text(label, (TigRect){ r->x + 9, r->y + 6, r->width - 18, r->height - 6 }, enabled ? MM_COLOR_GOLD : MM_COLOR_GRAY);
}
static void draw(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    coop_hud_frame(window, (TigRect){ 0, 0, bounds.width, bounds.height }, true);
    text(coop_tr("Give gold"), (TigRect){ 22, 16, 396, 24 }, MM_COLOR_GOLD);
    int64_t pc = player_get_local_pc_obj();
    char label[200];
    snprintf(label, sizeof(label), coop_tr("Your gold: %d"), pc ? item_gold_get(pc) : 0);
    text(label, (TigRect){ 22, 44, 396, 22 }, MM_COLOR_WHITE);
    text(coop_tr("Choose a party member:"), (TigRect){ 22, 68, 396, 22 }, MM_COLOR_GRAY);
    recipient_count = 0;
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1)) {
        int64_t target = coop_campaign_object(i);
        if (!target || target == pc || !online(i)) continue;
        unsigned row = recipient_count++;
        recipients[row] = i;
        TigRect rect = { 22, 92 + (int)row * 28, 396, 26 };
        if (i == recipient) tig_window_box(window, &rect, tig_color_make(186, 141, 67));
        const CoopCampaignMember* member = coop_campaign_member(i);
        snprintf(label, sizeof(label), "%s%s", member->character, nearby(pc, target) ? "" : coop_tr(" (not nearby)"));
        text(label, (TigRect){ rect.x + 8, rect.y + 4, rect.width - 16, 22 }, nearby(pc, target) ? MM_COLOR_WHITE : MM_COLOR_GRAY);
    }
    if (!recipient_count) text(coop_tr("No other traveller is available."), (TigRect){ 30, 100, 380, 30 }, MM_COLOR_GRAY);
    text(coop_tr("Amount:"), (TigRect){ 22, 182, 396, 22 }, MM_COLOR_GOLD);
    tig_window_fill(window, &amount_box, tig_color_make(19, 14, 11));
    tig_window_box(window, &amount_box, tig_color_make(186, 141, 67));
    snprintf(label, sizeof(label), "%s%s", amount_text, !pending && (SDL_GetTicks() / 500) % 2 ? "_" : "");
    text(label, (TigRect){ 30, 211, 380, 22 }, MM_COLOR_WHITE);
    button(&presets[0], "25", !pending);
    button(&presets[1], "100", !pending);
    button(&presets[2], coop_tr("All"), !pending);
    text(feedback[0] ? feedback : coop_tr("Stand near the recipient. Type an amount, then choose Give gold."),
        (TigRect){ 22, 278, 396, 38 }, MM_COLOR_WHITE);
    int value = amount();
    button(&send_button, pending ? coop_tr("Sending...") : coop_tr("Give gold"), !pending && value && pc
        && item_gold_get(pc) >= value && recipient < COOP_MAX_PLAYERS && nearby(pc, coop_campaign_object(recipient)));
    button(&close_button, coop_tr("Close"), true);
    drawn = SDL_GetTicks();
}
void coop_gold_ui_close(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    tig_window_destroy(window); window = TIG_WINDOW_HANDLE_INVALID;
    SDL_Window* native;
    if (!text_was_active && tig_video_window_get(&native) == TIG_OK) SDL_StopTextInput(native);
    pending = false;
}
static void send(void)
{
    if (pending) return;
    int value = amount();
    int64_t pc = player_get_local_pc_obj(), target = recipient < COOP_MAX_PLAYERS ? coop_campaign_object(recipient) : 0;
    const char* error = !value ? "Enter a positive whole amount."
        : !pc || item_gold_get(pc) < value ? "You do not have that much gold."
        : !target || !online(recipient) || !nearby(pc, target) ? "Stand near the recipient before giving gold." : NULL;
    if (error) { snprintf(feedback, sizeof(feedback), "%s", coop_tr(error)); return; }
    CoopItems* items = coop_gameplay_items(coop_runtime_gameplay());
    unsigned status;
    previous_result = coop_items_last_result(items, &status);
    if (!coop_runtime_item(COOP_ITEM_GOLD, 0, (uint8_t)recipient, value))
        snprintf(feedback, sizeof(feedback), "%s", coop_tr("Finish the current party action, then try again."));
    else { pending = true; snprintf(feedback, sizeof(feedback), "%s", coop_tr("Sending gold...")); }
}
static bool input(TigMessage* message)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return false;
    if (message->type == TIG_MESSAGE_KEYBOARD && message->data.keyboard.pressed) {
        unsigned key = message->data.keyboard.scancode;
        if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F12) coop_gold_ui_close();
        else if (!pending && key == SDL_SCANCODE_BACKSPACE) {
            size_t length = strlen(amount_text); if (length) amount_text[length - 1] = 0;
            replace_amount = false; feedback[0] = 0;
        } else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) send();
        else if (!pending && (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_TAB) && recipient_count) {
            unsigned index = 0; while (index < recipient_count && recipients[index] != recipient) index++;
            index = key == SDL_SCANCODE_UP ? (index + recipient_count - 1) % recipient_count : (index + 1) % recipient_count;
            recipient = recipients[index];
        }
    } else if (message->type == TIG_MESSAGE_TEXT_INPUT && !pending) {
        for (const char* p = message->data.text.text; *p; p++) if (*p >= '0' && *p <= '9') {
            if (replace_amount) { amount_text[0] = 0; replace_amount = false; }
            size_t length = strlen(amount_text);
            if (length < 10) { amount_text[length] = *p; amount_text[length + 1] = 0; feedback[0] = 0; }
        }
    } else if (message->type == TIG_MESSAGE_MOUSE && message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
        int x = message->data.mouse.x - bounds.x, y = message->data.mouse.y - bounds.y;
        if (inside(&close_button, x, y)) coop_gold_ui_close();
        else if (!pending) {
            if (inside(&send_button, x, y)) send();
            else if (inside(&amount_box, x, y)) replace_amount = true;
            for (unsigned i = 0; i < recipient_count; i++) {
                TigRect row = { 22, 92 + (int)i * 28, 396, 26 };
                if (inside(&row, x, y)) { recipient = recipients[i]; feedback[0] = 0; }
            }
            for (unsigned i = 0; i < 3; i++) if (inside(&presets[i], x, y)) {
                snprintf(amount_text, sizeof(amount_text), "%d", i == 0 ? 25 : i == 1 ? 100 : item_gold_get(player_get_local_pc_obj()));
                replace_amount = true; feedback[0] = 0;
            }
        }
    }
    draw();
    return message->type != TIG_MESSAGE_REDRAW && message->type != TIG_MESSAGE_QUIT;
}
bool coop_gold_ui_open(void)
{
    if (coop_runtime_phase() != COOP_RUNTIME_PLAYING || !coop_runtime_gameplay()) return false;
    coop_gold_ui_close();
    recipient = COOP_MAX_PLAYERS;
    int64_t pc = player_get_local_pc_obj();
    for (unsigned i = coop_campaign_next_slot(0); i < COOP_MAX_PLAYERS; i = coop_campaign_next_slot(i + 1))
        if (coop_campaign_object(i) != pc && online(i)) { recipient = i; break; }
    bounds = (TigRect){ 180, 112, 440, 376 };
    hrp_apply(&bounds, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
    TigWindowData info = { .rect = bounds, .flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER,
        .message_filter = input, .background_color = tig_color_make(36, 26, 18) };
    if (tig_window_create(&info, &window) != TIG_OK) { window = TIG_WINDOW_HANDLE_INVALID; return false; }
    SDL_Window* native;
    text_was_active = false;
    if (tig_video_window_get(&native) == TIG_OK) { text_was_active = SDL_TextInputActive(native); SDL_StartTextInput(native); }
    pending = false; replace_amount = true; strcpy(amount_text, "1"); feedback[0] = 0;
    draw(); return true;
}
void coop_gold_ui_tick(void)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    CoopGameplay* game = coop_runtime_gameplay();
    const CoopControlView* control = coop_controls_current(coop_runtime_controls());
    if (coop_runtime_phase() != COOP_RUNTIME_PLAYING || !game || !control || control->phase != COOP_CONTROL_IDLE
        || coop_dialogue_current(coop_gameplay_dialogue(game))->active) { coop_gold_ui_close(); return; }
    if (pending) {
        unsigned status;
        uint32_t result = coop_items_last_result(coop_gameplay_items(game), &status);
        if (result != previous_result) {
            pending = false;
            snprintf(feedback, sizeof(feedback), "%s", coop_tr(!status ? "Gold given to your party member."
                : status == COOP_ITEM_DISTANCE ? "Stand near the recipient before giving gold." : "Gold could not be given. Check the amount and recipient."));
            replace_amount = true;
        }
    }
    if (SDL_GetTicks() - drawn >= 150) draw();
}
