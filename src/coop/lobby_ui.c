#include "coop/localization.h"
#include "coop/lobby_ui.h"
#include "coop/campaign.h"
#include "coop/content.h"
#include "coop/discovery.h"
#include "coop/draft.h"
#include "coop/engine_character.h"
#include "coop/engine_documents.h"
#include "coop/engine_options.h"
#include "coop/lobby_settings.h"
#include "coop/runtime.h"
#include "coop/saves_ui.h"
#include "coop/session.h"
#include "coop/starting_shop.h"
#include "game/gamelib.h"
#include "game/critter.h"
#include "game/hrp.h"
#include "game/obj.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/proto.h"
#include "ui/gameuilib.h"
#include "ui/intgame.h"
#include "ui/mainmenu_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tig/tig.h>

static tig_window_handle_t window = TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds;
static CoopSession* session;
static char player_name[COOP_NAME_SIZE] = "Traveller";
static char address[64] = "127.0.0.1";
static char port_text[6] = "35741";
/* Keep this below the legacy TextEdit backup limit should it be used later. */
static char chat_text[192];
enum { CHAT_VISIBLE_LINES = 4, CHAT_MESSAGE_LINES = 8 };
typedef struct ChatLine { uint64_t ordinal; unsigned part; char text[COOP_CHAT_SIZE + COOP_NAME_SIZE + 4]; } ChatLine;
static ChatLine chat_lines[COOP_CHAT_HISTORY * CHAT_MESSAGE_LINES];
static unsigned chat_line_count, chat_anchor_part;
static uint64_t chat_layout_revision = UINT64_MAX, chat_anchor;
static bool chat_live = true;
static unsigned focus;
static unsigned selected_character;
static uint8_t selected_player = COOP_NO_SLOT, selected_owner[COOP_IDENTITY_SIZE];
static uint64_t remove_confirm_until;
static uint64_t last_draw;
static uint32_t logged_revision = UINT32_MAX;
static uint64_t logged_chats = UINT64_MAX;
static CoopPhase logged_phase = COOP_OFFLINE;
static char feedback[160];
static int pending_connect = -1;
static bool party_overlay;
static bool character_requested;
static bool character_open;
static bool character_edit;
static bool character_pending;
static CoopCharacter submitted_character;
static uint32_t submitted_prototype;
static uint8_t draft_content[32], draft_owner[COOP_IDENTITY_SIZE];
static char draft_path[96];
static bool library_open, equipment_requested, equipment_open, equipment_selling;
static unsigned library_page, library_slot, equipment_page, equipment_selection, equipment_quantity = 1;
static char transfer_path[192] = "arcmp-character-share.build";
static CoopCharacter equipment_character;
static CoopStartingShop* equipment_shop;
static CoopStartingItem equipment_items[300];
static unsigned equipment_count;
static uint32_t control_proposal;
static CoopControlPhase previous_control_phase;
static uint8_t previous_pauses;
static uint32_t previous_control_result;
static uint32_t drawn_control_revision;
static CoopDiscovery* discovery;
static bool discovery_host,browsing,settings_loaded;
static unsigned browser_page,browser_selection;
static uint64_t browser_instance;
static uint16_t browser_port;
static uint8_t browser_content[32];
static CoopSession* visible_session(void) { CoopSession* active = coop_runtime_session(); return active ? active : session; }
static const CoopDiscoveryParty* selected_lan(void)
{
    for (unsigned i=0;i<coop_discovery_count(discovery);i++) {
        const CoopDiscoveryParty* found=coop_discovery_party(discovery,i);
        if (found->instance==browser_instance && found->port==browser_port) return found;
    }
    return NULL;
}

static const TigRect fields[] = {
    { 24, 84, 210, 28 },
    { 250, 84, 300, 28 },
    { 566, 84, 166, 28 },
    { 24, 492, 570, 28 },
};
static const TigRect buttons[] = {
    { 24, 128, 145, 32 },
    { 185, 128, 145, 32 },
    { 346, 128, 145, 32 },
    { 610, 128, 122, 32 },
    { 24, 348, 100, 32 },
    { 506, 348, 98, 32 },
    { 610, 492, 122, 28 },
    { 616, 348, 116, 32 },
    { 136, 348, 112, 32 },
    { 260, 348, 112, 32 },
    { 384, 348, 110, 32 },
    { 24, 535, 118, 28 },
    { 156, 535, 126, 28 },
    { 296, 535, 140, 28 },
    { 450, 535, 126, 28 },
    { 590, 535, 142, 28 },
    { 506, 128, 98, 32 },
    { 600, 382, 132, 26 },
    { 450, 382, 132, 26 },
    { 286, 382, 154, 26 },
};
static const unsigned focus_order[] = { 0, 1, 2, 3, 4, 5, 20, 6, 7, 8, 12, 13, 14, 9, 11, 15, 16, 17, 18, 19, 21, 22, 23, 10 };
static bool manage_lobby(void)
{
    CoopSession* active = visible_session();
    return active && coop_session_phase(active) == COOP_LOBBY && coop_session_is_host(active)
        && !coop_session_locked(active) && coop_runtime_phase() == COOP_RUNTIME_LOBBY && !library_open && !equipment_open && !browsing;
}
static bool selected_player_valid(void)
{
    CoopSession* active = visible_session();
    return manage_lobby() && selected_player > 0 && selected_player < COOP_MAX_PLAYERS
        && coop_session_players(active)[selected_player].connected
        && !memcmp(selected_owner, coop_session_players(active)[selected_player].identity, COOP_IDENTITY_SIZE);
}

static bool contains(const TigRect* r, int x, int y)
{
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

static void activate(unsigned button);
void coop_lobby_release_previews(void)
{
    coop_starting_close(equipment_shop);
    equipment_shop = NULL;
    equipment_open = equipment_requested = library_open = false;
}
static void close_window(void)
{
    pending_connect = -1;
    coop_lobby_release_previews();
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    tig_window_destroy(window);
    window = TIG_WINDOW_HANDLE_INVALID;
    SDL_Window* native_window;
    if (tig_video_window_get(&native_window) == TIG_OK) SDL_StopTextInput(native_window);
}
static void leave_party(void)
{
    pending_connect = -1;
    character_requested = false;
    character_pending = false;
    mainmenu_ui_cancel_party_character();
    character_open = false;
    bool campaign = coop_runtime_detach();
    if (session) coop_session_leave(session);
    feedback[0] = 0;
    if (campaign) {
        close_window();
        mainmenu_ui_reset();
        gameuilib_wants_mainmenu_set();
    }
}

static void text_raw(const char* value, int x, int y, int width, MainMenuColor color)
{
    TigRect r = { x, y, width, 22 };
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, color));
    tig_window_text_write(window, value, &r);
    tig_font_pop();
}
static void text(const char* value, int x, int y, int width, MainMenuColor color)
{
    text_raw(coop_tr_rendered(value), x, y, width, color);
}

static void frame(const TigRect* rect, bool active)
{
    TigRect r = *rect;
    tig_window_fill(window, &r, tig_color_make(19, 14, 11));
    tig_window_box(window, &r, active ? tig_color_make(220, 175, 85) : tig_color_make(106, 80, 47));
}

static unsigned chat_layout(void)
{
    CoopSession* active = visible_session();
    uint64_t count = active ? coop_session_chat_count(active) : 0;
    if (chat_layout_revision != count) {
        chat_layout_revision = count;
        chat_line_count = 0;
        tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, MM_COLOR_WHITE));
        for (uint64_t i = count > COOP_CHAT_HISTORY ? count - COOP_CHAT_HISTORY : 0; i < count; i++) {
            const CoopChat* entry = coop_session_chat_at(active, i);
            if (!entry) continue;
            char message[sizeof(chat_lines[0].text)];
            snprintf(message, sizeof(message), "%s: %s", entry->name, entry->text);
            const char* at = message;
            for (unsigned part = 0; *at && part < CHAT_MESSAGE_LINES; part++) {
                ChatLine* row = &chat_lines[chat_line_count++];
                row->ordinal = i; row->part = part;
                while (*at == ' ') at++;
                size_t n = 0, space = 0;
                while (at[n] && n + 1 < sizeof(row->text)) {
                    row->text[n] = at[n]; row->text[n + 1] = 0;
                    TigFont measure = { .str = row->text };
                    tig_font_measure(&measure);
                    if (measure.width > 708) break;
                    if (at[n] == ' ') space = n;
                    n++;
                }
                if (at[n] && space) n = space;
                if (!n && *at) n = 1;
                row->text[n] = 0; at += n;
            }
        }
        tig_font_pop();
    }
    unsigned latest = chat_line_count > CHAT_VISIBLE_LINES ? chat_line_count - CHAT_VISIBLE_LINES : 0;
    if (chat_live) return latest;
    for (unsigned i = 0; i < chat_line_count; i++)
        if (chat_lines[i].ordinal > chat_anchor || (chat_lines[i].ordinal == chat_anchor && chat_lines[i].part >= chat_anchor_part))
            return i < latest ? i : latest;
    return latest;
}

static void chat_scroll(int change)
{
    if (library_open || equipment_open || browsing) return;
    int first = (int)chat_layout() + change;
    int latest = chat_line_count > CHAT_VISIBLE_LINES ? (int)chat_line_count - CHAT_VISIBLE_LINES : 0;
    if (first < 0) first = 0;
    chat_live = first >= latest;
    if (!chat_live) { chat_anchor = chat_lines[first].ordinal; chat_anchor_part = chat_lines[first].part; }
}

static bool content_read(uint8_t id[32])
{
    return coop_content_read(id);
}

static bool connect_party(bool host)
{
    if (coop_campaign_active()) {
        snprintf(feedback, sizeof(feedback), coop_tr("Leave the current campaign before starting another party."));
        return false;
    }
    char* end;
    unsigned long port = strtoul(port_text, &end, 10);
    if (*end || port == 0 || port > 65535) {
        snprintf(feedback, sizeof(feedback), coop_tr("Choose a port from 1 to 65535."));
        return false;
    }
    CoopConfig config = { 0 };
    memcpy(config.name, player_name, sizeof(config.name));
    if (!coop_identity_load_or_create("arcmp-player.id", config.identity)) {
        snprintf(feedback, sizeof(feedback), coop_tr("Could not read your player profile. Preserve arcmp-player.id to keep campaign ownership."));
        return false;
    }
    if (!content_read(config.content_id)) {
        pending_connect = host ? 1 : 0;
        return false;
    }
    pending_connect = -1;
    coop_runtime_detach();
    if (session) coop_session_destroy(session);
    chat_live = true; chat_layout_revision = UINT64_MAX;
    session = coop_session_create(&config);
    if (!session) {
        snprintf(feedback, sizeof(feedback), coop_tr("Enter a player name using printable characters."));
        return false;
    }
    feedback[0] = 0;
    bool connected = host ? coop_session_host(session, (uint16_t)port, SDL_GetTicks())
                          : coop_session_join(session, address, (uint16_t)port, SDL_GetTicks());
    if (connected) {
        CoopLobbySettings settings={.port=(uint16_t)port};
        snprintf(settings.name,sizeof(settings.name),"%s",player_name);
        snprintf(settings.address,sizeof(settings.address),"%.15s",address);
        coop_lobby_settings_save("arcmp-lobby.bin",&settings);
        browsing=false;
    }
    if (connected && !coop_runtime_attach(session, config.identity)) {
        coop_session_leave(session);
        snprintf(feedback, sizeof(feedback), coop_tr("Could not prepare the campaign connection."));
        return false;
    }
    if (connected) {
        character_pending = false;
        memcpy(draft_content, config.content_id, sizeof(draft_content));
        memcpy(draft_owner, config.identity, sizeof(draft_owner));
        strcpy(draft_path, "arcmp-character-");
        size_t prefix = strlen(draft_path);
        for (unsigned i = 0; i < COOP_IDENTITY_SIZE; i++) snprintf(draft_path + prefix + i * 2, 3, "%02x", draft_owner[i]);
        strcat(draft_path, ".draft");
    }
    return connected;
}

static void send_chat(void)
{
    if (session && coop_session_chat(session, chat_text)) { chat_text[0] = 0; chat_live = true; }
}

static void choose_character(void)
{
    if (!session || character_pending || coop_session_phase(session) != COOP_LOBBY || coop_runtime_phase() != COOP_RUNTIME_LOBBY) return;
    uint32_t prototype = 16067 + selected_character;
    int64_t object = sub_4685A0((int)prototype);
    char* name = NULL;
    if (object != OBJ_HANDLE_NULL) obj_field_string_get(object, OBJ_F_PC_PLAYER_NAME, &name);
    if (name && name[0] && coop_session_character(session, prototype, name)) {
        submitted_prototype = prototype;
        snprintf(submitted_character.name, sizeof(submitted_character.name), "%s", name);
        character_pending = !coop_session_is_host(session);
        feedback[0] = 0;
    } else {
        snprintf(feedback, sizeof(feedback), coop_tr("Could not select the campaign character prototype."));
    }
    if (name) FREE(name);
    selected_character = (selected_character + 1) % 8;
}

static void await_character(const CoopCharacter* character)
{
    submitted_character = *character;
    submitted_prototype = 0;
    character_pending = !coop_session_is_host(session);
}
static bool store_character(const CoopCharacter* c, bool archive)
{
    if (!session || coop_runtime_phase() != COOP_RUNTIME_LOBBY || !coop_engine_character_validate(c) || !coop_session_custom_character(session, c)) return false;
    await_character(c);
    bool saved = coop_draft_save(draft_path, draft_content, draft_owner, c);
    fprintf(stderr, "ARCMP character submitted name=%s race=%u background=%u points=%u trades=%u saved=%d\n", c->name, c->race, c->background, c->unspent, c->trades, saved);
    fflush(stderr);
    unsigned slot = 0;
    bool archived = !archive || (saved && coop_draft_remember(draft_path, draft_content, draft_owner, c, &slot));
    if (archive && archived) library_slot = slot;
    snprintf(feedback, sizeof(feedback), "%s", !saved ? coop_tr("Character selected, but its saved file could not be written.") : !archived ? coop_tr("Character selected. Library full or unavailable; choose a slot to replace.")
                                                                                                                                   : coop_tr("Character saved. Mark Ready when you are happy with your choices."));
    return true;
}
static CoopDraftStatus library_read(unsigned slot, CoopCharacter* c)
{
    char path[512];
    if (!coop_draft_slot_path(path, sizeof(path), draft_path, slot)) return COOP_DRAFT_INVALID;
    return coop_draft_load(path, draft_content, draft_owner, c);
}
static bool equipment_rebuild(void)
{
    CoopStartingShop* next = coop_starting_open(&equipment_character);
    if (!next) return false;
    coop_starting_close(equipment_shop);
    equipment_shop = next;
    equipment_count = coop_starting_items(equipment_shop, equipment_selling, equipment_items, SDL_arraysize(equipment_items));
    if (equipment_page * 4 >= equipment_count) equipment_page = 0;
    if (equipment_selection >= equipment_count) equipment_selection = 0;
    return true;
}
static void equipment_refresh(void)
{
    equipment_count = coop_starting_items(equipment_shop, equipment_selling, equipment_items, SDL_arraysize(equipment_items));
    if (equipment_page * 4 >= equipment_count) equipment_page = 0;
    if (equipment_selection >= equipment_count) equipment_selection = 0;
}
static void library_action(unsigned button)
{
    CoopCharacter c;
    CoopDraftStatus status = library_read(library_slot, &c);
    const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
    if (button == 4) {
        if (library_page) library_page--;
        return;
    }
    if (button == 8) {
        if ((library_page + 1) * 4 <= COOP_DRAFT_SLOTS) library_page++;
        return;
    }
    if (button == 5 || button == 17) {
        library_open = false;
        return;
    }
    if (button == 9) {
        if (status == COOP_DRAFT_OK && store_character(&c, false))
            library_open = false;
        else
            snprintf(feedback, sizeof(feedback), coop_tr("This slot cannot be selected. Check its saved character and game data."));
    } else if (button == 10) {
        char path[512];
        bool good = local->custom && library_slot && coop_draft_slot_path(path, sizeof(path), draft_path, library_slot)
            && coop_draft_save(path, draft_content, draft_owner, &local->build);
        snprintf(feedback, sizeof(feedback), "%s", good ? coop_tr("Current character saved in the selected slot.") : coop_tr("Select a named slot and a custom character to save."));
    } else if (button == 11) {
        CoopDraftStatus imported = coop_draft_import(transfer_path, draft_content, &c);
        unsigned slot = 0;
        bool good = imported == COOP_DRAFT_OK && coop_engine_character_validate(&c)
            && coop_draft_remember(draft_path, draft_content, draft_owner, &c, &slot);
        if (good) {
            library_slot = slot;
            library_page = slot / 4;
        }
        snprintf(feedback, sizeof(feedback), "%s", good ? coop_tr("Character imported into your library. Select it to use it.") : coop_tr("Import failed. Check the file, game data, native choices and free library slots."));
    } else if (button == 12) {
        bool good = status == COOP_DRAFT_OK && coop_draft_export(transfer_path, draft_content, &c);
        snprintf(feedback, sizeof(feedback), "%s", good ? coop_tr("Character exported without your player identity.") : coop_tr("Export failed. Select a valid character and writable file path."));
    } else if (button == 13) {
        bool good = library_slot && coop_draft_delete(draft_path, library_slot);
        snprintf(feedback, sizeof(feedback), "%s", good ? coop_tr("Slot removed. Its file is retained with a .deleted suffix.") : coop_tr("That slot cannot be removed."));
    } else if (button == 14 || button == 15) {
        if (button == 15 && status != COOP_DRAFT_OK) return;
        if (button == 15 && !store_character(&c, false)) return;
        library_open = false;
        if (coop_session_ready(session, false)) {
            character_requested = true;
            character_edit = button == 15;
        }
    }
}
static void equipment_action(unsigned button)
{
    if (button == 5 || button == 18) {
        coop_lobby_release_previews();
        return;
    }
    if (button == 4) {
        if (equipment_page) equipment_page--;
        return;
    }
    if (button == 8) {
        if ((equipment_page + 1) * 4 < equipment_count) equipment_page++;
        return;
    }
    if (button == 9) {
        if (store_character(&equipment_character, true))
            coop_lobby_release_previews();
        else
            snprintf(feedback, sizeof(feedback), coop_tr("Equipment selection could not be accepted."));
        return;
    }
    CoopCharacter old = equipment_character;
    if (button == 10) {
        equipment_character.trades = 0;
        memset(equipment_character.purchases, 0, sizeof(equipment_character.purchases));
    } else if (button == 11) {
        if (!equipment_count || equipment_selection >= equipment_count || equipment_character.trades == COOP_STARTING_TRADES) {
            snprintf(feedback, sizeof(feedback), coop_tr("Choose an available item. At most sixteen trades can be saved; Undo or Reset to change them."));
            return;
        }
        CoopStartingItem* item = &equipment_items[equipment_selection];
        unsigned count = equipment_quantity < item->quantity ? equipment_quantity : item->quantity;
        if (!count || count > INT16_MAX || !item->price) {
            snprintf(feedback, sizeof(feedback), coop_tr("That item is not available for this trade."));
            return;
        }
        equipment_character.purchases[equipment_character.trades++] = (CoopStartingTrade) { item->prototype, (int16_t)(equipment_selling ? -(int)count : (int)count) };
    } else if (button == 12) {
        if (!equipment_character.trades) return;
        equipment_character.purchases[--equipment_character.trades] = (CoopStartingTrade) { 0 };
    } else if (button == 13) {
        equipment_quantity = equipment_quantity == 1 ? 10 : equipment_quantity == 10 ? 100
                                                                                     : 1;
        return;
    } else if (button == 14) {
        equipment_selling = !equipment_selling;
        equipment_page = equipment_selection = 0;
        equipment_refresh();
        return;
    } else
        return;
    if (!equipment_rebuild()) {
        equipment_character = old;
        snprintf(feedback, sizeof(feedback), coop_tr("The trade is unavailable: check gold, stock, quantity, weight and inventory room."));
    } else
        snprintf(feedback, sizeof(feedback), coop_tr("%u trade(s) selected. Accept keeps this equipment; Back discards changes."), equipment_character.trades);
}
static void load_character(void)
{
    if (!session || character_pending || coop_session_phase(session) != COOP_LOBBY || coop_runtime_phase() != COOP_RUNTIME_LOBBY) return;
    CoopCharacter character;
    CoopDraftStatus status = coop_draft_load(draft_path, draft_content, draft_owner, &character);
    const char* errors[] = { "", coop_tr("There is no saved character for this player yet."), coop_tr("Could not read your saved character."),
        coop_tr("The saved character is damaged or unsupported."), coop_tr("The saved character uses different game data."), coop_tr("The saved character belongs to another player profile.") };
    if (status != COOP_DRAFT_OK) { snprintf(feedback, sizeof(feedback), "%s", errors[status]); return; }
    if (!coop_engine_character_validate(&character)) { snprintf(feedback, sizeof(feedback), coop_tr("The saved character has invalid choices and has not been loaded.")); return; }
    if (!coop_session_custom_character(session, &character)) { snprintf(feedback, sizeof(feedback), coop_tr("Could not select the saved character.")); return; }
    await_character(&character);
    snprintf(feedback, sizeof(feedback), coop_tr("Saved character selected. Edit it or mark Ready."));
    fprintf(stderr, "ARCMP character loaded name=%s\n", character.name); fflush(stderr);
}

static bool accept_character(int64_t pc)
{
    if (!pc) return true;
    CoopCharacter build;
    if (!session || !coop_engine_character_capture(pc, &build)) return false;
    if (character_edit) {
        const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
        if (local->custom) {
            build.trades = local->build.trades;
            memcpy(build.purchases, local->build.purchases, sizeof(build.purchases));
            if (!coop_engine_character_validate(&build)) {
                build.trades = 0;
                memset(build.purchases, 0, sizeof(build.purchases));
            }
        }
    }
    if (!store_character(&build, true)) {
        intgame_message_window_display_str(-1, coop_tr("Could not submit this character. Check its choices and the party connection."));
        return false;
    }
    return true;
}

static uint32_t enabled_buttons(void)
{
    CoopSession* session = visible_session();
    bool online = session && coop_session_phase(session) == COOP_LOBBY;
    bool connected = session && coop_session_phase(session) != COOP_OFFLINE;
    CoopRuntimePhase phase = coop_runtime_phase();
    const CoopPlayer* local = online ? &coop_session_players(session)[coop_session_local_slot(session)] : NULL;
    bool choosing = online && phase == COOP_RUNTIME_LOBBY && !coop_session_locked(session) && !character_pending;
    bool reconnecting = phase == COOP_RUNTIME_RECONNECTING, failed = phase == COOP_RUNTIME_FAILED;
    bool loading = reconnecting || phase == COOP_RUNTIME_DRAINING || phase == COOP_RUNTIME_LOADING
        || phase == COOP_RUNTIME_STARTING || phase == COOP_RUNTIME_RESTING || phase == COOP_RUNTIME_JOURNEY;
    bool can_start = choosing && coop_session_is_host(session) && coop_session_all_ready(session);
    CoopControls* controls = coop_runtime_controls();
    const CoopControlView* control = coop_controls_current(controls);
    uint8_t slot = online ? coop_session_local_slot(session) : COOP_NO_SLOT;
    bool voted = control && slot < COOP_MAX_PLAYERS && (control->agreed & (1u << slot));
    bool idle = online && !failed && control && control->phase == COOP_CONTROL_IDLE && !coop_controls_pending(controls);
    bool voting = online && !failed && control && control->phase == COOP_CONTROL_VOTING && !coop_controls_pending(controls);
    uint32_t mask = 0;
    for (unsigned i = 0; i < SDL_arraysize(buttons); i++) {
        bool enabled = (i == 3 && !loading) || (i == 0 && !connected && !reconnecting && !failed) || (i == 1 && !connected && !failed)
            || (i == 2 && (connected || reconnecting || failed || pending_connect >= 0))
            || ((i == 4 || i == 5 || i == 8 || i == 10) && choosing) || (i == 9 && choosing && local && local->custom) || (i == 6 && online) || (i == 7 && can_start)
            || (i == 11 && party_overlay && (idle || voting))
            || (i == 12 && ((party_overlay && idle) || can_start))
            || (i == 13 && ((party_overlay && idle && coop_runtime_checkpoint_exists()) || (can_start && coop_runtime_recovery_exists())))
            || (i == 14 && ((voting && !voted) || (party_overlay && control && control->paused && coop_session_is_host(session) && idle)))
            || (i == 15 && online && (voting || (control && (control->phase == COOP_CONTROL_RESTING || control->phase == COOP_CONTROL_JOURNEY) && !coop_controls_pending(controls))));
        if (i==16) enabled=!connected && !loading && !failed;
        if (i == 17 || i == 18) enabled = choosing && (i == 17 || (local && local->custom));
        if (i == 19) enabled = choosing && selected_player_valid();
        if (library_open || equipment_open) {
            enabled = (i == 2 && connected) || (i == 3 && !loading) || i == 5
                || (choosing && (i == 4 || i == 8 || i == 9 || i == 10 || (i >= 11 && i <= 14) || (library_open && i == 15) || i == 17 || i == 18));
            if (i == 4) enabled = choosing && (library_open ? library_page > 0 : equipment_page > 0);
            if (i == 8) enabled = choosing && (library_open ? (library_page + 1) * 4 <= COOP_DRAFT_SLOTS : (equipment_page + 1) * 4 < equipment_count);
        }
        if (browsing && (i==4 || i==5 || i==8 || i==9 || i==10)) {
            const CoopDiscoveryParty* found=selected_lan();
            enabled=i==5 || i==10 || (i==4 && browser_page) || (i==8 && (browser_page+1)*4<coop_discovery_count(discovery))
                || (i==9 && found && found->protocol==COOP_PROTOCOL_VERSION && !memcmp(found->content,browser_content,32) && (found->locked || found->players<4));
        }
        if (enabled) mask |= 1u << i;
    }
    return mask;
}

static bool field_enabled(unsigned field)
{
    CoopSession* active = visible_session();
    bool online = active && coop_session_phase(active) == COOP_LOBBY;
    if (field == 3) return library_open ? online && !character_pending
        : online && !equipment_open;
    CoopRuntimePhase phase = coop_runtime_phase();
    return field < 3 && (!active || coop_session_phase(active) == COOP_OFFLINE)
        && (phase == COOP_RUNTIME_OFF || phase == COOP_RUNTIME_LOBBY);
}

static bool focus_enabled(unsigned value)
{
    if (value < 4) return field_enabled(value);
    /* The removal control also provides keyboard selection of a player. */
    if (value == 23) return manage_lobby();
    return value - 4 < SDL_arraysize(buttons) && (enabled_buttons() & (1u << (value - 4)));
}

static void advance_focus(bool backwards)
{
    unsigned count = SDL_arraysize(focus_order), start = 0;
    for (unsigned i = 0; i < count; i++) if (focus_order[i] == focus) { start = i; break; }
    for (unsigned step = 1; step <= count; step++) {
        unsigned next = (start + (backwards ? count - step : step)) % count;
        if (focus_enabled(focus_order[next])) { focus = focus_order[next]; return; }
    }
}

static void draw(void)
{
    if (!focus_enabled(focus)) advance_focus(false);
    CoopSession* session = visible_session();
    TigRect full = { 0, 0, bounds.width, bounds.height };
    tig_window_fill(window, &full, tig_color_make(36, 26, 18));
    tig_window_box(window, &full, tig_color_make(186, 141, 67));
    text(coop_tr("CAMPAIGN MULTIPLAYER"), 24, 16, 700, MM_COLOR_GOLD);
    text(party_overlay ? coop_tr("Docs: F7 | Journal: F8 | Save: F9 | Load: F10 | Pause | Party: F12") : coop_tr("Four travellers. One shared story."), 24, 42, 700, MM_COLOR_GRAY);
    text(coop_tr("Player name"), 24, 64, 210, MM_COLOR_GOLD);
    text(coop_tr("Host IPv4 address"), 250, 64, 300, MM_COLOR_GOLD);
    text(coop_tr("Port"), 566, 64, 166, MM_COLOR_GOLD);
    const char* values[] = { player_name, address, port_text, library_open ? transfer_path : chat_text };
    for (unsigned i = 0; i < 4; i++) {
        frame(&fields[i], focus == i);
        char value[256];
        snprintf(value, sizeof(value), "%s%s", values[i], focus == i && (SDL_GetTicks() / 500) % 2 ? "_" : "");
        text_raw(value, fields[i].x + 8, fields[i].y + 5, fields[i].width - 16, MM_COLOR_WHITE);
    }
    bool online = session && coop_session_phase(session) == COOP_LOBBY;
    const CoopPlayer* players = session ? coop_session_players(session) : NULL;
    const CoopPlayer* local = online ? &players[coop_session_local_slot(session)] : NULL;
    bool choosing = online && coop_runtime_phase() == COOP_RUNTIME_LOBBY && !coop_session_locked(session) && !character_pending;
    bool reconnecting = coop_runtime_phase() == COOP_RUNTIME_RECONNECTING;
    bool failed = coop_runtime_phase() == COOP_RUNTIME_FAILED;
    bool loading = reconnecting || coop_runtime_phase() == COOP_RUNTIME_DRAINING || coop_runtime_phase() == COOP_RUNTIME_LOADING || coop_runtime_phase() == COOP_RUNTIME_STARTING || coop_runtime_phase() == COOP_RUNTIME_RESTING || coop_runtime_phase() == COOP_RUNTIME_JOURNEY;
    CoopControls* controls = coop_runtime_controls();
    const CoopControlView* control = coop_controls_current(controls);
    uint8_t local_slot = online ? coop_session_local_slot(session) : COOP_NO_SLOT;
    bool own_pause = control && local_slot < COOP_MAX_PLAYERS && (control->paused & (1u << local_slot));
    bool voting = online && coop_runtime_phase() != COOP_RUNTIME_FAILED && control && control->phase == COOP_CONTROL_VOTING && !coop_controls_pending(controls);
    const char* labels[] = { coop_tr("Host"), reconnecting ? coop_tr("Retry now") : coop_tr("Join"), coop_tr("Leave party"), party_overlay ? coop_tr("Return") : coop_tr("Back"), coop_tr("Pick preset >"), local && local->ready ? coop_tr("Not ready") : coop_tr("Ready"), coop_tr("Send"), coop_tr("Start campaign"), local && local->custom ? coop_tr("Edit character") : coop_tr("Create character"), coop_tr("New character"), coop_tr("Load saved"),
        own_pause ? coop_tr("Resume mine") : coop_tr("Pause"), choosing ? coop_tr("Resume save") : coop_tr("Save party"), choosing ? coop_tr("Recover party") : coop_tr("Load party"), voting ? coop_tr("Agree") : coop_tr("Resume all"),
        control && control->phase == COOP_CONTROL_RESTING ? coop_tr("Stop resting") : control && control->phase == COOP_CONTROL_JOURNEY ? coop_tr("Stop journey")
                                                                                                                               : coop_tr("Decline"),
        browsing ? coop_tr("Close LAN") : coop_tr("Find LAN"), coop_tr("Characters"), coop_tr("Equipment"), remove_confirm_until > SDL_GetTicks() ? coop_tr("Confirm remove") : coop_tr("Remove player") };
    if (failed) labels[3] = coop_tr("Back to title");
    if (pending_connect >= 0) labels[2] = coop_tr("Cancel");
    if (browsing) {
        labels[4]=coop_tr("Previous"); labels[8]=coop_tr("Next"); labels[9]=coop_tr("Join selected"); labels[10]=coop_tr("Refresh"); labels[5]=coop_tr("Close list");
    }
    char quantity_label[32];
    snprintf(quantity_label, sizeof(quantity_label), coop_tr("Quantity: %u"), equipment_quantity);
    if (library_open) {
        labels[4] = coop_tr("Previous");
        labels[8] = coop_tr("Next");
        labels[9] = coop_tr("Select");
        labels[10] = coop_tr("Save here");
        labels[5] = coop_tr("Close list");
        labels[11] = coop_tr("Import file");
        labels[12] = coop_tr("Export file");
        labels[13] = coop_tr("Remove slot");
        labels[14] = coop_tr("New character");
        labels[15] = coop_tr("Edit selected");
    }
    if (equipment_open) {
        labels[4] = coop_tr("Previous");
        labels[8] = coop_tr("Next");
        labels[9] = coop_tr("Accept");
        labels[10] = coop_tr("Reset");
        labels[5] = coop_tr("Back");
        labels[11] = equipment_selling ? coop_tr("Sell item") : coop_tr("Buy item");
        labels[12] = coop_tr("Undo last");
        labels[13] = quantity_label;
        labels[14] = equipment_selling ? coop_tr("Shop stock") : coop_tr("Your gear");
        labels[15] = "-";
    }
    uint32_t enabled_mask = enabled_buttons();
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (i == 19 && !manage_lobby()) continue;
        bool enabled = (enabled_mask & (1u << i)) != 0;
        frame(&buttons[i], focus == i + 4);
        text(labels[i], buttons[i].x + 12, buttons[i].y + 7, buttons[i].width - 20, enabled ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    }
    const char* state = coop_runtime_status();
    char operation[180] = { 0 };
    if (control && control->phase == COOP_CONTROL_VOTING) {
        const char* name = control->proposer < COOP_MAX_PLAYERS && players ? players[control->proposer].name : coop_tr("A traveller");
        const char* durations[] = { coop_tr("one hour"), coop_tr("two hours"), coop_tr("four hours"), coop_tr("eight hours"), coop_tr("one day"), coop_tr("until morning"), coop_tr("until evening"), coop_tr("until healed") };
        snprintf(operation, sizeof(operation), coop_tr("%s requests %s%s. Everyone must agree (%u seconds)."), name,
            control->action == COOP_CONTROL_REST ? coop_tr("rest ") : control->action == COOP_CONTROL_TRAVEL ? coop_tr("a world journey") : coop_tr("loading the checkpoint"), control->action == COOP_CONTROL_REST ? durations[control->value] : "", (control->remaining_ms + 999) / 1000);
        if (control->action == COOP_CONTROL_TRAVEL && control->route_count > 1)
            snprintf(operation, sizeof(operation), coop_tr("%s requests a journey via %u waypoints. Everyone must agree (%us)."),
                name, control->route_count, (control->remaining_ms + 999) / 1000);
        if (control->action == COOP_CONTROL_LOAD) {
            char target[24];
            if (!control->value) strcpy(target, coop_tr("Quick save"));
            else if (control->value == COOP_SAVE_RECOVERY) strcpy(target, coop_tr("Recovery"));
            else if (control->value >= COOP_SAVE_AUTOSAVE_FIRST) snprintf(target, sizeof(target), coop_tr("Autosave %u"), control->value - COOP_SAVE_AUTOSAVE_FIRST + 1);
            else snprintf(target, sizeof(target), coop_tr("slot %02u"), control->value);
            snprintf(operation, sizeof(operation), coop_tr("Load %s: %s (%us)"), target,
                control->description[0] ? control->description : coop_tr("Party checkpoint"), (control->remaining_ms + 999) / 1000);
        }
    } else if (control && control->phase == COOP_CONTROL_RESTING) snprintf(operation, sizeof(operation), coop_tr("Resting together: %u hour(s) passed. Anyone may stop."), control->completed);
    else if (control && control->phase == COOP_CONTROL_JOURNEY) snprintf(operation, sizeof(operation), coop_tr("Travelling together: %u hour(s) passed. Anyone may stop."), control->completed);
    else if (control && control->paused && !loading) snprintf(operation, sizeof(operation), coop_tr("The party is paused. Each paused traveller can resume; the host can Resume all."));
    const char* status = coop_runtime_phase() == COOP_RUNTIME_FAILED || reconnecting ? state : operation[0] ? operation : feedback[0] && !loading ? feedback : state[0] ? state : character_pending ? coop_tr("Confirming your character...") : session ? coop_session_status(session)
                                                          : coop_tr("Host a party or join a friend's LAN address.");
    char preview[160];
    if (equipment_open && !feedback[0]) {
        snprintf(preview, sizeof(preview), coop_tr("%s - %d gold remaining. %u/16 trades. Buy or sell, then Accept gear."), equipment_character.name, coop_starting_gold(equipment_shop), equipment_character.trades);
        status = preview;
    }
    if (library_open && !feedback[0]) status = coop_tr("Select a saved character or enter a file path to import/export.");
    if (browsing) status=feedback[0] ? feedback : coop_discovery_count(discovery) ? coop_tr("Select a LAN party, then Join selected.") : coop_tr("Searching the LAN... You can also enter an address to join.");
    char content_status[160];
    if ((!session || coop_session_phase(session) == COOP_OFFLINE)
        && coop_content_status(content_status, sizeof(content_status))) status = content_status;
    TigRect status_rect = { 24, 170, 708, 36 };
    tig_font_push(mainmenu_ui_font(MM_FONT_FLARE12, MM_COLOR_WHITE));
    tig_window_text_write(window, coop_tr_rendered(status), &status_rect);
    tig_font_pop();
    text(library_open ? coop_tr("CHARACTER LIBRARY") : equipment_open ? (equipment_selling ? coop_tr("YOUR EQUIPMENT") : coop_tr("STARTING SHOP"))
            : browsing                                       ? coop_tr("LAN PARTIES")
                                                             : coop_tr("PARTY"),
        24, 211, 250, MM_COLOR_GOLD);
    text(library_open ? coop_tr("BUILD") : equipment_open ? coop_tr("PRICE EACH")
            : browsing                           ? coop_tr("ADDRESS")
                                                 : coop_tr("CHARACTER"),
        280, 211, 300, MM_COLOR_GOLD);
    text(equipment_open ? coop_tr("QUANTITY") : coop_tr("STATUS"), 620, 211, 100, MM_COLOR_GOLD);
    if (party_overlay) text(coop_tr("DEBUG (host)"), 498, 211, 115, MM_COLOR_GOLD);
    for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
        char name[80];
        if (library_open) {
            unsigned slot = library_page * 4 + i;
            if (slot > COOP_DRAFT_SLOTS) continue;
            CoopCharacter saved;
            CoopDraftStatus found = library_read(slot, &saved);
            if (slot == library_slot) {
                TigRect row = { 20, 237 + (int)i * 25, 716, 24 };
                tig_window_fill(window, &row, tig_color_make(65, 46, 24));
            }
            snprintf(name, sizeof(name), "%s%u. %s", slot ? coop_tr("Slot ") : coop_tr("Last "), slot, found == COOP_DRAFT_OK ? saved.name : coop_tr("Empty / unavailable"));
            text(name, 24, 240 + (int)i * 25, 250, found == COOP_DRAFT_OK ? MM_COLOR_WHITE : MM_COLOR_GRAY);
            char detail[100];
            if (found == COOP_DRAFT_OK)
                snprintf(detail, sizeof(detail), coop_tr("Race %u | background %u | %u trades"), saved.race, saved.background, saved.trades);
            else
                snprintf(detail, sizeof(detail), "%s", found == COOP_DRAFT_MISSING ? coop_tr("Save your current custom character here") : coop_tr("Check file, profile and game data"));
            text(detail, 280, 240 + (int)i * 25, 320, MM_COLOR_WHITE);
            text(slot == library_slot ? coop_tr("Selected") : found == COOP_DRAFT_OK ? coop_tr("Saved")
                                                                            : "-",
                620, 240 + (int)i * 25, 110, MM_COLOR_GOLD);
            continue;
        }
        if (equipment_open) {
            unsigned index = equipment_page * 4 + i;
            if (index >= equipment_count) continue;
            const CoopStartingItem* item = &equipment_items[index];
            if (index == equipment_selection) {
                TigRect row = { 20, 237 + (int)i * 25, 716, 24 };
                tig_window_fill(window, &row, tig_color_make(65, 46, 24));
            }
            text(item->name, 24, 240 + (int)i * 25, 250, MM_COLOR_WHITE);
            char price[64], count[32];
            snprintf(price, sizeof(price), item->price > 0 ? coop_tr("%d gold") : coop_tr("Not tradable"), item->price);
            snprintf(count, sizeof(count), "%u", item->quantity);
            text(price, 280, 240 + (int)i * 25, 320, item->price > 0 ? MM_COLOR_GOLD : MM_COLOR_GRAY);
            text(count, 620, 240 + (int)i * 25, 100, MM_COLOR_WHITE);
            continue;
        }
        if (browsing) {
            unsigned index=browser_page*4+i; const CoopDiscoveryParty* found=coop_discovery_party(discovery,index);
            if (!found) continue;
            if (found->instance==browser_instance && found->port==browser_port) { TigRect row={20,237+(int)i*25,716,24}; tig_window_fill(window,&row,tig_color_make(65,46,24)); }
            char endpoint[64],state[40]; snprintf(endpoint,sizeof(endpoint),"%s:%u",found->address,found->port);
            snprintf(state,sizeof(state),"%u/4 %s",found->players,found->locked ? coop_tr("Running") : found->players==4 ? coop_tr("Full") : coop_tr("Lobby"));
            bool compatible=found->protocol==COOP_PROTOCOL_VERSION && !memcmp(found->content,browser_content,32);
            text(found->name,24,240+(int)i*25,250,compatible ? MM_COLOR_WHITE : MM_COLOR_GRAY);
            text(endpoint,280,240+(int)i*25,320,MM_COLOR_WHITE);
            text(compatible ? state : found->protocol!=COOP_PROTOCOL_VERSION ? coop_tr("Version differs") : coop_tr("Data differs"),620,240+(int)i*25,110,compatible ? MM_COLOR_GREEN : MM_COLOR_GRAY);
            continue;
        }
        const CoopPlayer* p = players ? &players[i] : NULL;
        if (i == selected_player && selected_player_valid()) {
            TigRect row = { 20, 237 + (int)i * 25, 716, 24 };
            tig_window_fill(window, &row, tig_color_make(65, 46, 24));
        }
        bool reserved = session && (coop_session_reserved(session) & (1u << i));
        snprintf(name, sizeof(name), "%u. %s%s", i + 1, p && (p->connected || reserved) && p->name[0] ? p->name : coop_tr("Open slot"), i == 0 && p && p->connected ? coop_tr(" (Host)") : "");
        text_raw(name, 24, 240 + (int)i * 25, 250, p && p->connected ? MM_COLOR_WHITE : MM_COLOR_GRAY);
        const char* character = p && p->prototype ? p->character : "-";
        int member = coop_gameplay_member_for_connection(coop_runtime_gameplay(), i);
        const CoopCampaignMember* saved = member >= 0 ? coop_campaign_member((unsigned)member) : NULL;
        char* live_character = NULL;
        if (saved) {
            int64_t actor = coop_campaign_object((unsigned)member);
            if (actor) obj_field_string_get(actor, OBJ_F_PC_PLAYER_NAME, &live_character);
            character = live_character && *live_character ? live_character : saved->character;
        }
        text_raw(character, 280, 240 + (int)i * 25, party_overlay ? 210 : 320, MM_COLOR_WHITE);
        if (party_overlay && saved) {
            bool immortal = coop_engine_options_immortal(coop_campaign_object((unsigned)member));
            TigRect toggle = { 498, 239 + (int)i * 25, 113, 22 };
            frame(&toggle, immortal);
            text(immortal ? coop_tr("Immortal: ON") : coop_tr("Immortal: OFF"), 503, 241 + (int)i * 25, 105,
                coop_session_is_host(visible_session()) ? MM_COLOR_GOLD : MM_COLOR_GRAY);
        }
        if (live_character) FREE(live_character);
        const char* status = p && p->connected ? p->ready ? coop_tr("Ready") : coop_tr("Choosing") : reserved ? online ? coop_tr("Rejoining") : coop_tr("Waiting") : "-";
        char health[64]; bool incapacitated = false;
        if (p && p->connected && control && control->phase == COOP_CONTROL_VOTING) status = control->agreed & (1u << i) ? coop_tr("Agreed") : coop_tr("Vote needed");
        else if (p && p->connected && control && (control->paused & (1u << i))) status = coop_tr("Paused");
        else if (p && p->connected && saved) {
            int64_t actor = coop_campaign_object((unsigned)member);
            if (actor) {
                snprintf(health, sizeof(health), coop_tr("%d/%d HP %d FP"), object_hp_current(actor), object_hp_max(actor), critter_fatigue_current(actor));
                status = critter_is_dead(actor) ? coop_tr("Dead") : critter_is_unconscious(actor) ? coop_tr("Unconscious") : health;
                incapacitated = critter_is_dead(actor) || critter_is_unconscious(actor);
            }
        }
        text(status, 620, 240 + (int)i * 25, 110, incapacitated ? MM_COLOR_RED : p && p->ready ? MM_COLOR_GREEN : MM_COLOR_GRAY);
    }
    char caption[100];
    snprintf(caption, sizeof(caption), coop_tr("%d gold remaining | %s | %u/16 trades"), coop_starting_gold(equipment_shop), equipment_selling ? coop_tr("Sell equipment") : coop_tr("Buy equipment"), equipment_character.trades);
    text(library_open ? coop_tr("IMPORT / EXPORT FILE") : equipment_open ? caption
                                                                : chat_live ? coop_tr("PARTY CHAT (PgUp/PgDn)") : coop_tr("CHAT HISTORY (End: latest)"),
        24, 385, 400, MM_COLOR_GOLD);
    if (library_open) {
        text(coop_tr("Eight named slots plus your last accepted build. Imports keep your own player profile."), 24, 414, 708, MM_COLOR_WHITE);
        text(coop_tr("Select: use a saved build. Save here: replace this slot with your current character."), 24, 437, 708, MM_COLOR_WHITE);
        text(coop_tr("Import/export path below. Removed slots keep a .deleted backup file."), 24, 460, 708, MM_COLOR_WHITE);
        return;
    }
    unsigned start = chat_layout();
    for (unsigned i = start; i < chat_line_count && i < start + CHAT_VISIBLE_LINES; i++)
        text_raw(chat_lines[i].text, 24, 410 + (int)(i - start) * 19, 708, MM_COLOR_WHITE);
}

static void activate(unsigned button)
{
    if (button >= SDL_arraysize(buttons) || !(enabled_buttons() & (1u << button))) return;
    CoopSession* active=visible_session();
    bool connected = active && coop_session_phase(active) != COOP_OFFLINE;
    fprintf(stderr, "ARCMP lobby action=%u\n", button);
    fflush(stderr);
    if (button == 19) {
        if (!selected_player_valid() || character_pending) return;
        if (remove_confirm_until <= SDL_GetTicks()) {
            remove_confirm_until = SDL_GetTicks() + 10000;
            snprintf(feedback, sizeof(feedback), coop_tr("Remove %s from the lobby? Click Confirm remove within ten seconds."), coop_session_players(active)[selected_player].name);
        } else {
            bool removed = coop_session_remove(active, selected_player, selected_owner);
            snprintf(feedback, sizeof(feedback), "%s", removed ? coop_tr("Player removed. The remaining party must mark Ready again.") : coop_tr("The selected player changed. Select the current player again."));
            selected_player = COOP_NO_SLOT; remove_confirm_until = 0;
        }
        return;
    }
    if (browsing) {
        if (button==4) { if (browser_page) browser_page--; return; }
        if (button==8) { if ((browser_page+1)*4<coop_discovery_count(discovery)) browser_page++; return; }
        if (button==5 || button==16) { browsing=false; return; }
        if (button==10) { coop_discovery_refresh(discovery); feedback[0]=0; return; }
        if (button==9) {
            const CoopDiscoveryParty* found=selected_lan();
            if (!found) return;
            if (found->protocol!=COOP_PROTOCOL_VERSION || memcmp(found->content,browser_content,32)) { snprintf(feedback,sizeof(feedback),coop_tr("This party needs the same multiplayer version and game content.")); return; }
            if (found->players==4 && !found->locked) { snprintf(feedback,sizeof(feedback),coop_tr("That party is full.")); return; }
            snprintf(address,sizeof(address),"%s",found->address); snprintf(port_text,sizeof(port_text),"%u",found->port);
            connect_party(false); return;
        }
    }
    if ((library_open || equipment_open) && (!session || coop_runtime_phase() != COOP_RUNTIME_LOBBY || coop_session_phase(session) != COOP_LOBBY || coop_session_locked(session))) {
        coop_lobby_release_previews();
        return;
    }
    if ((library_open || equipment_open) && character_pending && button != 2 && button != 3 && button != 5 && button != 17 && button != 18) return;
    if (library_open && button != 2 && button != 3 && button != 18) {
        library_action(button);
        return;
    }
    if (equipment_open && button != 2 && button != 3) {
        equipment_action(button);
        return;
    }
    if (button == 17 || button == 18) {
        if (session && !character_pending && coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY && !coop_session_locked(session)) {
            const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
            if (button == 17) {
                library_open = true;
                library_page = 0;
                feedback[0] = 0;
                coop_session_ready(session, false);
            } else if (local->custom && coop_session_ready(session, false)) {
                equipment_requested = true;
                library_open = false;
                feedback[0] = 0;
            }
        }
        return;
    }
    switch (button) {
    case 16:
        if (!connected && coop_runtime_phase()!=COOP_RUNTIME_RECONNECTING) {
            browsing=true; browser_page=browser_selection=0; browser_instance=0;browser_port=0; feedback[0]=0;
            content_read(browser_content);
        }
        break;
    case 0:
        if (!connected && coop_runtime_phase() != COOP_RUNTIME_RECONNECTING) connect_party(true);
        break;
    case 1:
        if (coop_runtime_phase() == COOP_RUNTIME_RECONNECTING) coop_runtime_retry_now();
        else if (!connected) connect_party(false);
        break;
    case 2:
        leave_party();
        break;
    case 3:
        if (coop_runtime_phase() == COOP_RUNTIME_RECONNECTING || coop_runtime_phase() == COOP_RUNTIME_DRAINING || coop_runtime_phase() == COOP_RUNTIME_LOADING || coop_runtime_phase() == COOP_RUNTIME_STARTING || coop_runtime_phase() == COOP_RUNTIME_RESTING || coop_runtime_phase() == COOP_RUNTIME_JOURNEY) return;
        if (party_overlay && coop_runtime_phase() == COOP_RUNTIME_PLAYING) { close_window(); return; }
        leave_party();
        coop_lobby_shutdown();
        return;
    case 4:
        choose_character();
        break;
    case 5:
        if (session && !character_pending && coop_session_phase(session) == COOP_LOBBY && coop_runtime_phase() == COOP_RUNTIME_LOBBY) {
            const CoopPlayer* p = &coop_session_players(session)[coop_session_local_slot(session)];
            if (!coop_session_ready(session, !p->ready))
                snprintf(feedback, sizeof(feedback), coop_tr("Choose a character before readying up."));
            else
                feedback[0] = 0;
        }
        break;
    case 6:
        send_chat();
        break;
    case 7:
        if (coop_runtime_start()) feedback[0] = 0;
        break;
    case 8:
    case 9:
        if (session && !character_pending && coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY) {
            if (button == 9 && !coop_session_players(session)[coop_session_local_slot(session)].custom) break;
            if (coop_session_ready(session, false)) { character_requested = true; character_edit = button == 8; }
        }
        break;
    case 10:
        load_character();
        break;
    case 11:
        if (!coop_runtime_pause_toggle()) snprintf(feedback, sizeof(feedback), coop_tr("The party cannot change pause right now."));
        break;
    case 12:
        if (!coop_saves_ui_open(coop_runtime_phase() == COOP_RUNTIME_LOBBY ? COOP_SAVES_RESUME : COOP_SAVES_SAVE)) snprintf(feedback, sizeof(feedback), coop_tr("The party cannot open its saved campaigns right now."));
        break;
    case 13:
        if (coop_runtime_phase() == COOP_RUNTIME_LOBBY) coop_runtime_recover_checkpoint();
        else if (!coop_saves_ui_open(COOP_SAVES_LOAD)) snprintf(feedback, sizeof(feedback), coop_tr("The party cannot load right now."));
        break;
    case 14:
    case 15: {
        const CoopControlView* control = coop_controls_current(coop_runtime_controls());
        if (!control) break;
        if (control->phase == COOP_CONTROL_VOTING) coop_runtime_control(COOP_CONTROL_VOTE, button == 14, control->proposal, 0);
        else if (button == 15 && (control->phase == COOP_CONTROL_RESTING || control->phase == COOP_CONTROL_JOURNEY)) coop_runtime_control(COOP_CONTROL_CANCEL, 0, control->proposal, 0);
        else if (button == 14) coop_runtime_control(COOP_CONTROL_RESUME_ALL, 0, 0, 0);
        break;
    }
    }
}

static bool message_filter(TigMessage* message)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return false;
    if (message->type == TIG_MESSAGE_MOUSE) {
        if (message->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL) {
            int x = message->data.mouse.x - bounds.x, y = message->data.mouse.y - bounds.y;
            if (x >= 24 && x < 732 && y >= 410 && y < 486) {
                chat_scroll(message->data.mouse.dy > 0 ? -1 : 1);
                draw();
            }
            return true;
        }
        if ((library_open || equipment_open) && message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
            int x = message->data.mouse.x - bounds.x, y = message->data.mouse.y - bounds.y;
            if (x >= 20 && x < 736 && y >= 237 && y < 337) {
                unsigned index = (unsigned)(y - 237) / 25;
                if (library_open && library_page * 4 + index <= COOP_DRAFT_SLOTS) library_slot = library_page * 4 + index;
                if (equipment_open && equipment_page * 4 + index < equipment_count) equipment_selection = equipment_page * 4 + index;
                draw();
                return true;
            }
        }
        if (message->data.mouse.event != TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) return true;
        int x = message->data.mouse.x - bounds.x;
        int y = message->data.mouse.y - bounds.y;
        if (party_overlay && x >= 498 && x < 611 && y >= 237 && y < 337) {
            unsigned slot = (unsigned)(y - 237) / 25;
            int member = coop_gameplay_member_for_connection(coop_runtime_gameplay(), slot);
            if (!coop_session_is_host(visible_session())) snprintf(feedback, sizeof(feedback), coop_tr("Only the host can change immortality."));
            else if (member >= 0) {
                int64_t actor = coop_campaign_object((unsigned)member);
                bool enabled = coop_engine_options_immortal(actor);
                if (!enabled && actor && critter_is_dead(actor)) snprintf(feedback, sizeof(feedback), coop_tr("Enable immortality before death. Use resurrection for a dead character."));
                else if (coop_runtime_combat(COOP_COMBAT_IMMORTAL, (uint8_t)(((unsigned)member << 1) | !enabled), 0))
                    snprintf(feedback, sizeof(feedback), coop_tr("Immortality change requested. ON keeps HP at 1 or higher; fatigue still applies."));
                else snprintf(feedback, sizeof(feedback), coop_tr("Wait until the game is running and the previous action finishes."));
            }
            draw(); return true;
        }
        if (manage_lobby() && x >= 20 && x < 736 && y >= 237 && y < 337) {
            unsigned slot = (unsigned)(y - 237) / 25;
            if (slot && coop_session_players(visible_session())[slot].connected) {
                selected_player = (uint8_t)slot;
                memcpy(selected_owner, coop_session_players(visible_session())[slot].identity, COOP_IDENTITY_SIZE);
                remove_confirm_until = 0;
                snprintf(feedback, sizeof(feedback), coop_tr("Selected %s. Remove player asks for confirmation."), coop_session_players(visible_session())[slot].name);
            }
        }
        if (browsing && x>=20 && x<736 && y>=237 && y<337) {
            unsigned index=browser_page*4+(unsigned)(y-237)/25;
            if (index<coop_discovery_count(discovery)) { browser_selection=index;browser_instance=coop_discovery_party(discovery,index)->instance;browser_port=coop_discovery_party(discovery,index)->port; }
        }
        for (unsigned i = 0; i < 4; i++)
            if (field_enabled(i) && contains(&fields[i], x, y)) focus = i;
        for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
            if (!contains(&buttons[i], x, y)) continue;
            focus = i + 4;
            activate(i);
            break;
        }
        if (window != TIG_WINDOW_HANDLE_INVALID) draw();
        return true;
    }
    if (message->type == TIG_MESSAGE_KEYBOARD) {
        if (!message->data.keyboard.pressed) return true;
        switch (message->data.keyboard.scancode) {
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_PAGEDOWN:
            chat_scroll(message->data.keyboard.scancode == SDL_SCANCODE_PAGEUP ? -CHAT_VISIBLE_LINES : CHAT_VISIBLE_LINES);
            draw(); return true;
        case SDL_SCANCODE_END:
            if (!library_open && !equipment_open) { chat_live = true; draw(); }
            return true;
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_DOWN:
            if (focus == 23 && manage_lobby()) {
                unsigned start = selected_player < COOP_MAX_PLAYERS ? selected_player : 0;
                unsigned direction = message->data.keyboard.scancode == SDL_SCANCODE_UP ? 3 : 1;
                for (unsigned step = 1; step < COOP_MAX_PLAYERS; step++) {
                    unsigned slot = (start + step * direction) % COOP_MAX_PLAYERS;
                    if (!slot || !coop_session_players(visible_session())[slot].connected) continue;
                    selected_player = (uint8_t)slot; remove_confirm_until = 0;
                    memcpy(selected_owner, coop_session_players(visible_session())[slot].identity, COOP_IDENTITY_SIZE);
                    snprintf(feedback, sizeof(feedback), coop_tr("Selected %s. Remove player asks for confirmation."), coop_session_players(visible_session())[slot].name);
                    break;
                }
            }
            break;
        case SDL_SCANCODE_F7:
            if (party_overlay && coop_runtime_phase() == COOP_RUNTIME_PLAYING) {
                close_window();
                coop_documents_ui_open();
            }
            return true;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_F12:
            if (library_open || equipment_open)
                activate(5);
            else
                activate(3);
            return true;
        case SDL_SCANCODE_PAUSE:
            activate(11); return true;
        case SDL_SCANCODE_F9:
            if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) coop_runtime_control(COOP_CONTROL_SAVE, 0, 0, 0);
            else activate(12);
            return true;
        case SDL_SCANCODE_F10:
            if (coop_runtime_phase() == COOP_RUNTIME_PLAYING) coop_runtime_control(COOP_CONTROL_LOAD, 0, 0, 0);
            else activate(13);
            return true;
        case SDL_SCANCODE_TAB:
            advance_focus((SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (focus == 3) {
                activate(library_open ? 11 : 6);
            } else if (focus >= 4)
                activate(focus - 4);
            if (window == TIG_WINDOW_HANDLE_INVALID) return true;
            break;
        case SDL_SCANCODE_BACKSPACE: {
            if (!field_enabled(focus)) break;
            char* values[] = { player_name, address, port_text, library_open ? transfer_path : chat_text };
            size_t length = strlen(values[focus]);
            if (length) values[focus][length - 1] = 0;
            break;
        }
        default:
            break;
        }
        draw();
        return true;
    }
    if (message->type == TIG_MESSAGE_TEXT_INPUT) {
        if (!field_enabled(focus)) return true;
        char* values[] = { player_name, address, port_text, library_open ? transfer_path : chat_text };
        const size_t sizes[] = { sizeof(player_name), sizeof(address), sizeof(port_text), library_open ? sizeof(transfer_path) : sizeof(chat_text) };
        {
            size_t length = strlen(values[focus]);
            for (const unsigned char* p = (const unsigned char*)message->data.text.text; *p; p++) {
                if (*p >= 32 && *p <= 126 && length + 1 < sizes[focus] && (focus != 2 || (*p >= '0' && *p <= '9'))) {
                    values[focus][length++] = (char)*p;
                }
            }
            values[focus][length] = 0;
        }
        draw();
        return true;
    }
    return message->type != TIG_MESSAGE_QUIT && message->type != TIG_MESSAGE_REDRAW;
}

void coop_lobby_open(void)
{
    if (window != TIG_WINDOW_HANDLE_INVALID) return;
    content_read(browser_content);
    if (!settings_loaded) {
        settings_loaded=true; CoopLobbySettings settings;
        if (coop_lobby_settings_load("arcmp-lobby.bin",&settings)) {
            snprintf(player_name,sizeof(player_name),"%s",settings.name); snprintf(address,sizeof(address),"%s",settings.address);
            snprintf(port_text,sizeof(port_text),"%u",settings.port);
        }
    }
    bounds = (TigRect) { 20, 10, 760, 580 };
    hrp_apply(&bounds, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
    TigWindowData info = { 0 };
    info.rect = bounds;
    info.flags = TIG_WINDOW_MESSAGE_FILTER | TIG_WINDOW_ALWAYS_ON_TOP;
    info.background_color = tig_color_make(36, 26, 18);
    info.message_filter = message_filter;
    if (tig_window_create(&info, &window) != TIG_OK) {
        window = TIG_WINDOW_HANDLE_INVALID;
        return;
    }
    focus = 0;
    selected_player = COOP_NO_SLOT; remove_confirm_until = 0;
    party_overlay = coop_runtime_phase() == COOP_RUNTIME_PLAYING;
    SDL_Window* native_window;
    if (tig_video_window_get(&native_window) == TIG_OK) SDL_StartTextInput(native_window);
    draw();
    fprintf(stderr, "ARCMP lobby opened\n");
    fflush(stderr);
}
bool coop_lobby_is_open(void) { return window != TIG_WINDOW_HANDLE_INVALID; }

void coop_lobby_tick(void)
{
    if (window != TIG_WINDOW_HANDLE_INVALID && content_read(browser_content) && pending_connect >= 0) {
        bool host = pending_connect != 0;
        pending_connect = -1;
        connect_party(host);
    }
    if (session) coop_session_poll(session, SDL_GetTicks());
    coop_runtime_tick(SDL_GetTicks());
    coop_saves_ui_tick();
    CoopSession* active=visible_session();
    bool advertise=active && coop_session_is_host(active) && coop_session_phase(active)==COOP_LOBBY;
    if (discovery && ((advertise!=discovery_host) || (!advertise && !browsing))) { coop_discovery_destroy(discovery); discovery=NULL; }
    if (!discovery && (advertise || browsing)) { discovery=coop_discovery_create(advertise,COOP_DISCOVERY_PORT); discovery_host=advertise; content_read(browser_content); }
    if (discovery) {
        if (advertise) {
            CoopDiscoveryParty info={.port=coop_session_port(active),.protocol=COOP_PROTOCOL_VERSION,.locked=coop_session_locked(active)};
            memcpy(info.name,coop_session_players(active)[0].name,sizeof(info.name)); memcpy(info.content,browser_content,32);
            for (unsigned i=0;i<4;i++) info.players+=coop_session_players(active)[i].connected;
            coop_discovery_advertise(discovery,&info);
        }
        coop_discovery_poll(discovery,SDL_GetTicks(),getenv("ARCMP_DISCOVERY_TARGET"));
        if (browsing && browser_page*4>=coop_discovery_count(discovery)) browser_page=0;
        if (browsing && !browser_instance && coop_discovery_count(discovery)) { browser_instance=coop_discovery_party(discovery,0)->instance;browser_port=coop_discovery_party(discovery,0)->port; }
    } else if (browsing) snprintf(feedback,sizeof(feedback),coop_tr("LAN search is unavailable. You can still enter an address and Join."));
    CoopControls* controls = coop_runtime_controls();
    const CoopControlView* control = coop_controls_current(controls);
    if (control) {
        if (control->phase != COOP_CONTROL_IDLE && control->phase != previous_control_phase) feedback[0] = 0;
        if (control->phase == COOP_CONTROL_VOTING && control->proposal != control_proposal) {
            control_proposal = control->proposal;
            if (window == TIG_WINDOW_HANDLE_INVALID) coop_lobby_open();
            party_overlay = true;
        }
        if (control->paused != previous_pauses) {
            previous_pauses = control->paused;
            intgame_message_window_display_str(-1, control->paused ? coop_tr("The party is paused. Paused travellers are listed in Party (F12).") : coop_tr("The party has resumed."));
            if (control->paused && window == TIG_WINDOW_HANDLE_INVALID) { coop_lobby_open(); party_overlay = true; }
        }
        const char* errors[] = { "", coop_tr("That party operation is unavailable."), coop_tr("That character is not owned by this player."),
            coop_tr("The party is busy. Finish the current conversation or operation first."), coop_tr("The party declined the operation."), coop_tr("The party vote expired."),
            coop_tr("Enemies prevent that party operation."), coop_tr("Gather the party before continuing."), coop_tr("That party operation is unavailable at this location."),
            coop_tr("An active traveller must request that operation."), coop_tr("The checkpoint could not be saved or loaded. The previous checkpoint is preserved."),
            coop_tr("Save a party checkpoint first."), coop_tr("The checkpoint requires different game content."), coop_tr("Every saved owner must join with their original player profile."), coop_tr("The party operation was stopped."), coop_tr("That saved campaign changed. Refresh the save list and choose it again.") };
        unsigned result_status; uint32_t result = coop_controls_last_result(controls, &result_status);
        if (result != previous_control_result) {
            previous_control_result = result;
            if (result_status) { snprintf(feedback, sizeof(feedback), "%s", errors[result_status]); intgame_message_window_display_str(-1, feedback); }
        }
        if (previous_control_phase != COOP_CONTROL_IDLE && control->phase == COOP_CONTROL_IDLE) {
            if (control->notice) { snprintf(feedback, sizeof(feedback), "%s", errors[control->notice]); intgame_message_window_display_str(-1, feedback); }
            else if (control->action == COOP_CONTROL_SAVE || control->action == COOP_CONTROL_LOAD) {
                char message[180]; snprintf(message, sizeof(message), coop_tr("Party campaign %s: %s.%s"), control->action == COOP_CONTROL_SAVE ? coop_tr("saved") : coop_tr("loaded"),
                    control->description[0] ? control->description : coop_tr("Quick save"), control->paused ? coop_tr(" The party remains paused.") : "");
                intgame_message_window_display_str(-1, message);
            }
            else if (control->action == COOP_CONTROL_REST) {
                char message[120]; snprintf(message, sizeof(message), coop_tr("The party rested for %u hour(s).%s"), control->completed, control->paused ? coop_tr(" The party remains paused.") : "");
                intgame_message_window_display_str(-1, message);
            }
        }
        previous_control_phase = control->phase;
    } else { control_proposal = previous_control_result = 0; previous_control_phase = COOP_CONTROL_IDLE; previous_pauses = 0; }
    if (coop_runtime_phase() != COOP_RUNTIME_LOBBY && (library_open || equipment_open || equipment_requested)) coop_lobby_release_previews();
    if (equipment_requested && !character_pending && session && coop_runtime_phase() == COOP_RUNTIME_LOBBY && coop_session_phase(session) == COOP_LOBBY && !coop_session_locked(session)) {
        const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
        if (!local->ready) {
            equipment_requested = false;
            equipment_character = local->build;
            equipment_page = equipment_selection = 0;
            equipment_selling = false;
            equipment_quantity = 1;
            equipment_open = local->custom && equipment_rebuild();
            if (!equipment_open) snprintf(feedback, sizeof(feedback), coop_tr("Starting equipment is unavailable. Your current character is unchanged."));
        }
    }
    if (character_pending && coop_runtime_phase() != COOP_RUNTIME_LOBBY) character_pending = false;
    if (character_pending && coop_session_phase(session) == COOP_LOBBY) {
        const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
        uint8_t expected[COOP_CHARACTER_BYTES], actual[COOP_CHARACTER_BYTES];
        if (submitted_prototype && !local->custom && local->prototype == submitted_prototype
            && !strcmp(local->character, submitted_character.name)) character_pending = false;
        if (!submitted_prototype && local->custom && coop_character_encode(&submitted_character, expected) && coop_character_encode(&local->build, actual)
            && !memcmp(expected, actual, sizeof(expected))) character_pending = false;
    }
    if (character_requested && coop_runtime_phase() != COOP_RUNTIME_LOBBY) character_requested = false;
    if (character_open && coop_runtime_phase() != COOP_RUNTIME_LOBBY) mainmenu_ui_cancel_party_character();
    if (character_requested && !character_pending && coop_session_phase(session) == COOP_LOBBY
        && !coop_session_players(session)[coop_session_local_slot(session)].ready) {
        character_requested = false;
        feedback[0] = 0;
        close_window();
        const CoopPlayer* local = &coop_session_players(session)[coop_session_local_slot(session)];
        character_open = mainmenu_ui_edit_party_character(accept_character, character_edit && local->custom ? &local->build : NULL);
        if (character_open) { fprintf(stderr, "ARCMP character creation opened\n"); fflush(stderr); }
        if (!character_open) coop_lobby_open();
    }
    if (character_open && !mainmenu_ui_party_creation_active()) {
        character_open = false;
        coop_lobby_open();
    }
    if (coop_runtime_phase() == COOP_RUNTIME_RECONNECTING || coop_runtime_phase() == COOP_RUNTIME_DRAINING || coop_runtime_phase() == COOP_RUNTIME_LOADING || coop_runtime_phase() == COOP_RUNTIME_RESTING || coop_runtime_phase() == COOP_RUNTIME_JOURNEY) {
        if (window == TIG_WINDOW_HANDLE_INVALID) coop_lobby_open();
        party_overlay = false;
    }
    if (coop_runtime_phase() == COOP_RUNTIME_PLAYING && !party_overlay) close_window();
    if (coop_runtime_phase() == COOP_RUNTIME_FAILED && window == TIG_WINDOW_HANDLE_INVALID) coop_lobby_open();
    if (session && (logged_revision != coop_session_revision(session) || logged_phase != coop_session_phase(session) || logged_chats != coop_session_chat_count(session))) {
        unsigned players = 0, ready = 0;
        const CoopPlayer* party = coop_session_players(session);
        for (unsigned i = 0; i < COOP_MAX_PLAYERS; i++) {
            players += party[i].connected;
            ready += party[i].ready;
        }
        logged_revision = coop_session_revision(session);
        logged_phase = coop_session_phase(session);
        logged_chats = coop_session_chat_count(session);
        fprintf(stderr, "ARCMP lobby phase=%u revision=%u players=%u ready=%u local=%u chats=%llu\n",
            (unsigned)logged_phase, logged_revision, players, ready, coop_session_local_slot(session), (unsigned long long)logged_chats);
        fflush(stderr);
    }
    if (window != TIG_WINDOW_HANDLE_INVALID && (SDL_GetTicks() - last_draw >= 100 || (control && control->revision != drawn_control_revision))) {
        draw();
        last_draw = SDL_GetTicks();
        drawn_control_revision = control ? control->revision : 0;
    }
}

void coop_lobby_shutdown(void)
{
    coop_content_shutdown();
    coop_discovery_destroy(discovery); discovery=NULL; browsing=false;
    mainmenu_ui_cancel_party_character();
    character_requested = false;
    character_pending = false;
    character_open = false;
    coop_runtime_detach();
    if (session) {
        coop_session_destroy(session);
        session = NULL;
    }
    logged_revision = UINT32_MAX;
    logged_chats = UINT64_MAX;
    chat_live = true; chat_layout_revision = UINT64_MAX;
    close_window();
}
int coop_lobby_probe_party(uint16_t tcp_port)
{
    if (!browsing) return -1;
    for (unsigned i=0;i<4;i++) {
        const CoopDiscoveryParty* p=coop_discovery_party(discovery,browser_page*4+i);
        if (p && p->port==tcp_port && p->protocol==COOP_PROTOCOL_VERSION && !memcmp(p->content,browser_content,32)) return (int)i;
    }
    return -1;
}

static void preparation_pump(void)
{
    uint64_t until = SDL_GetTicks() + 40;
    do {
        tig_ping();
        coop_lobby_tick();
        TigMessage message;
        while (tig_message_dequeue(&message) == TIG_OK)
            if (message.type == TIG_MESSAGE_REDRAW) gamelib_redraw();
        tig_window_display();
        SDL_Delay(2);
    } while (SDL_GetTicks() < until);
}
static void preparation_click(int x, int y)
{
    TigMessage m = { .type = TIG_MESSAGE_MOUSE };
    m.data.mouse.x = x + bounds.x;
    m.data.mouse.y = y + bounds.y;
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;
    tig_message_enqueue(&m);
    preparation_pump();
    m.data.mouse.event = TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;
    tig_message_enqueue(&m);
    preparation_pump();
}
static void preparation_button(unsigned index) { preparation_click(buttons[index].x + 10, buttons[index].y + 10); }
bool coop_lobby_dropin_probe(bool framebuffer)
{
    coop_lobby_open();
    snprintf(port_text, sizeof(port_text), "%u", 41000 + (unsigned)(SDL_GetTicks() % 4096));
    preparation_button(0);
    preparation_button(2);
    if (pending_connect != -1) return false;
    preparation_button(0);
    if (framebuffer) tig_video_screenshot_save("arcmp-dropin-checking.bmp");
    uint64_t started = SDL_GetTicks();
    while ((!session || coop_session_phase(session) != COOP_LOBBY) && SDL_GetTicks() - started < 120000)
        preparation_pump();
    uint8_t id[32];
    bool good = session && coop_session_phase(session) == COOP_LOBBY && content_read(id) && pending_connect == -1;
    if (good && framebuffer) good = tig_video_screenshot_save("arcmp-dropin-lobby.bmp") == TIG_OK;
    if (good) {
        /* Exercise the native chat view, including an incoming message while
         * the reader is looking back. No remote users are present here. */
        for (unsigned i = 0; i < 7; i++) {
            char message[COOP_CHAT_SIZE];
            snprintf(message, sizeof(message), coop_tr("Message %u: Gather at the entrance after reading the journal. We can save the campaign, compare the clues, and decide together which route to take. Keep the earlier instructions visible while new party messages arrive."), i + 1);
            good &= coop_session_chat(session, message);
        }
        draw();
        unsigned latest = chat_layout();
        TigMessage key = { .type = TIG_MESSAGE_KEYBOARD };
        key.data.keyboard.pressed = true; key.data.keyboard.scancode = SDL_SCANCODE_PAGEUP;
        message_filter(&key);
        unsigned older = chat_layout();
        good &= !chat_live && older < latest && chat_line_count > 7;
        uint64_t anchor = chat_lines[older].ordinal; unsigned part = chat_lines[older].part;
        good &= coop_session_chat(session, coop_tr("New arrival: please keep reading; your place in chat history should stay put."));
        draw();
        good &= chat_lines[chat_layout()].ordinal == anchor && chat_lines[chat_layout()].part == part;
        preparation_pump();
        if (framebuffer) good &= tig_video_screenshot_save("arcmp-dropin-chat-history.bmp") == TIG_OK;
        key.data.keyboard.scancode = SDL_SCANCODE_END; message_filter(&key);
        good &= chat_live && chat_layout() + CHAT_VISIBLE_LINES == chat_line_count;
        preparation_pump();
        if (framebuffer) good &= tig_video_screenshot_save("arcmp-dropin-chat-latest.bmp") == TIG_OK;
        fprintf(stderr, "ARCMP native chat wrapping/history %s lines=%u\n", good ? "PASS" : "FAIL", chat_line_count);
    }
    fprintf(stderr, "ARCMP drop-in lobby %s: content ready, native Host action completed, cancellation checked\n", good ? "PASS" : "FAIL");
    leave_party();
    coop_lobby_shutdown();
    return good;
}

bool coop_lobby_preparation_probe(unsigned player, bool framebuffer, CoopCharacter* result)
{
    CoopCharacter c;
    if (!coop_character_ui_probe(player, false, &c) || !coop_starting_probe(false)) return false;
    coop_lobby_open();
    snprintf(port_text, sizeof(port_text), "%u", 41000 + player * 4096 + (unsigned)(SDL_GetTicks() % 4096));
    preparation_button(0);
    bool good = session && coop_session_phase(session) == COOP_LOBBY && store_character(&c, true);
    preparation_button(17);
    good &= library_open;
    if (framebuffer) good &= tig_video_screenshot_save("arcmp-preparation-library.bmp") == TIG_OK;
    unsigned fixture_slot = library_slot;
    preparation_button(12);
    CoopCharacter imported;
    good &= coop_draft_import(transfer_path, draft_content, &imported) == COOP_DRAFT_OK;
    preparation_button(13);
    good &= library_read(fixture_slot, &imported) == COOP_DRAFT_MISSING;
    preparation_button(11);
    good &= library_read(library_slot, &imported) == COOP_DRAFT_OK;
    preparation_button(9);
    good &= !library_open;
    preparation_button(18);
    good &= equipment_open && equipment_count;
    unsigned chosen = equipment_count;
    for (unsigned i = 0; i < equipment_count; i++) {
        CoopStartingItem* item = &equipment_items[i];
        if (item->price > 0 && item->price <= coop_starting_gold(equipment_shop)) {
            CoopCharacter test = equipment_character;
            test.purchases[test.trades++] = (CoopStartingTrade) { item->prototype, 1 };
            if (coop_starting_validate(&test)) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == equipment_count) good = false;
    if (good) {
        equipment_page = chosen / 4;
        preparation_click(80, 242 + (int)(chosen % 4) * 25);
        preparation_button(11);
        good &= equipment_character.trades == 1;
        if (framebuffer) good &= tig_video_screenshot_save("arcmp-preparation-equipment.bmp") == TIG_OK;
        preparation_button(5);
        good &= !equipment_open && !coop_session_players(session)[0].build.trades;
        preparation_button(18);
        equipment_page = chosen / 4;
        preparation_click(80, 242 + (int)(chosen % 4) * 25);
        preparation_button(11);
        preparation_button(9);
        good &= !equipment_open && coop_session_players(session)[0].build.trades == 1;
    }
    if (good && result) *result = coop_session_players(session)[0].build;
    if (good) {
        uint8_t bytes[COOP_CHARACTER_BYTES];
        good = coop_character_encode(&coop_session_players(session)[0].build, bytes);
        FILE* file = good ? fopen("arcmp-prepared-character.bin", "wb") : NULL;
        if (!file) good = false;
        if (file) {
            good &= fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
            if (fclose(file)) good = false;
        }
    }
    FILE* report = fopen("arcmp-preparation-result.json", "wb");
    if (!report) good = false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"library_export_import_remove\":%s,\"equipment_cancel_accept\":%s,\"trades\":%u}\n", good ? "true" : "false", good ? "true" : "false", good ? "true" : "false", session ? coop_session_players(session)[0].build.trades : 0);
        if (fclose(report)) good = false;
    }
    fprintf(stderr, "ARCMP preparation native UI %s\n", good ? "PASS" : "FAIL");
    leave_party();
    coop_lobby_shutdown();
    return good;
}
