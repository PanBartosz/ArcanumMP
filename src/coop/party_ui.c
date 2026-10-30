#include "coop/modern.h"
#include "coop/hud.h"
#include "coop/party_ui.h"
#include "coop/campaign.h"
#include "coop/engine_options.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/hrp.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/target.h"
#include "game/stat.h"
#include "ui/intgame.h"
#include "ui/mainmenu_ui.h"
#include <stdio.h>
#include <string.h>
#include <tig/tig.h>

enum { ROW_HEIGHT = 96, PARTY_WIDTH = 80, CELL_WIDTH = 94 };
static tig_window_handle_t party = TIG_WINDOW_HANDLE_INVALID, turns = TIG_WINDOW_HANDLE_INVALID, tip = TIG_WINDOW_HANDLE_INVALID;
static TigRect party_rect, turn_rect;
static unsigned members[4], member_count, selected = 4, page;
static uint32_t generation;
static uint64_t drawn, hover_since;
static int mouse_x = -1, mouse_y = -1;
static char hover_text[512];
static bool hovered_party, hovered_turns;
static const char* status_names[] = { "Dead", "Sleeping / unconscious", "Poisoned", "Low fatigue",
    "Stunned", "Paralyzed", "Blinded", "Crippled", "Encumbered" };

unsigned coop_party_ui_statuses(int64_t actor)
{
    if (!actor) return 0;
    if (critter_is_dead(actor)) return COOP_STATUS_DEAD;
    unsigned flags = obj_field_int32_get(actor, OBJ_F_CRITTER_FLAGS), status = 0;
    if (critter_is_unconscious(actor) || (flags & OCF_SLEEPING)) status |= COOP_STATUS_UNCONSCIOUS;
    if (stat_level_get(actor, STAT_POISON_LEVEL) > 0) status |= COOP_STATUS_POISONED;
    if (!(status & COOP_STATUS_UNCONSCIOUS) && critter_fatigue_max(actor) > 0
        && critter_fatigue_current(actor) * 4 <= critter_fatigue_max(actor)) status |= COOP_STATUS_TIRED;
    if (flags & OCF_STUNNED) status |= COOP_STATUS_STUNNED;
    if (flags & OCF_PARALYZED) status |= COOP_STATUS_PARALYZED;
    if (flags & OCF_BLINDED) status |= COOP_STATUS_BLINDED;
    if (flags & OCF_CRIPPLED) status |= COOP_STATUS_INJURED;
    if (critter_encumbrance_level_get(actor) >= ENCUMBRANCE_LEVEL_SIGNIFICANT) status |= COOP_STATUS_ENCUMBERED;
    return status;
}

static void status_icon(unsigned index, int x, int y)
{
    TigRect badge={x,y,14,14};
    if (index<SDL_arraysize(status_names) && coop_hud_art(party,HUD_ART_DEAD+index,badge)) return;
    /* A readable fallback remains available if the renderer cannot allocate
     * an art buffer. Normal play uses the embedded condition artwork. */
    tig_window_fill(party,&badge,tig_color_make(16,15,14));
    tig_window_box(party,&badge,tig_color_make(230,190,110));
}

static void label(tig_window_handle_t window, const char* value, int x, int y, int width, int height, MainMenuColor color)
{
    TigRect rect = { x, y, width, height };
    tig_font_push(coop_play_chrome_font(color));
    tig_window_text_write(window, value, &rect);
    tig_font_pop();
}
static const char* party_shortcut(void)
{
    static char text[64];
    coop_play_shortcuts("Party [F12]",text,sizeof(text));
    return text;
}
static void frame(tig_window_handle_t window, TigRect rect, bool active)
{
    coop_hud_surface(window,rect.height<30 ? HUD_ART_BUTTON : HUD_ART_PANEL,rect,active);
}
static bool inside(TigRect rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}
static void actor_name(int64_t actor, char* out, size_t size)
{
    int member = coop_campaign_slot_for_object(actor);
    const CoopCampaignMember* human = member < 0 ? NULL : coop_campaign_member((unsigned)member);
    if (human) {
        char* name = NULL;
        obj_field_string_get(actor, OBJ_F_PC_PLAYER_NAME, &name);
        snprintf(out, size, "%s", name && *name ? name : human->character);
        if (name) FREE(name);
    } else if (actor) {
        char name[MAX_STRING];
        object_examine(actor, player_get_local_pc_obj(), name);
        snprintf(out, size, "%s", name);
    } else snprintf(out, size, "Unknown");
}
static void actor_portrait(tig_window_handle_t window, int64_t actor, int x, int y, int size)
{
    coop_hud_surface(window,HUD_ART_PORTRAIT,(TigRect){x-3,y-3,size+6,size+6},false);
    int portrait;
    if (intgame_examine_portrait(player_get_local_pc_obj(), actor, &portrait)) {
        portrait_draw_scaled(actor, portrait, window, x, y, size);
    } else {
        /* Animals and unnamed NPCs use native interface silhouettes. Do not
         * give an unknown wolf the fallback human player portrait. */
        tig_art_id_t art;
        TigArtFrameData frame;
        if (tig_art_interface_id_create(portrait, 0, 0, 0, &art) != TIG_OK || tig_art_frame_data(art, &frame) != TIG_OK) return;
        TigRect src = { 0, 0, frame.width, frame.height }, dst = { x, y, size, size };
        TigArtBlitInfo blit = { .art_id = art, .src_rect = &src, .dst_rect = &dst };
        tig_window_blit_art(window, &blit);
    }
}
bool coop_party_ui_target_actor(int64_t actor, bool attack_enemy)
{
    if (!actor || !obj_handle_is_valid(actor) || (obj_field_int32_get(actor, OBJ_F_FLAGS) & (OF_OFF | OF_DESTROYED | OF_EXTINCT))) return false;
    IntgameMode mode = intgame_mode_get();
    bool ability = mode == INTGAME_MODE_SPELL || mode == INTGAME_MODE_SKILL || mode == INTGAME_MODE_ITEM;
    bool enemy = attack_enemy && mode == INTGAME_MODE_MAIN && !critter_is_dead(actor)
        && object_relation_color(actor) == tig_color_make(245,55,45);
    if (ability || enemy) {
        if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get() != player_get_local_pc_obj()) {
            intgame_message_window_display_str(-1, "Wait for your turn before using that ability.");
            return false;
        }
        TargetDescriptor target;
        target_descriptor_set_obj(&target, actor);
        if (enemy && coop_runtime_gameplay()) {
            int aim = tig_kb_is_key_pressed(SDL_SCANCODE_COMMA) ? HIT_LOC_HEAD
                : tig_kb_is_key_pressed(SDL_SCANCODE_PERIOD) ? HIT_LOC_LEG
                : tig_kb_is_key_pressed(SDL_SCANCODE_SLASH) ? HIT_LOC_ARM : HIT_LOC_TORSO;
            CoopAttackMode attack = tig_kb_get_modifier(SDL_KMOD_SHIFT) ? COOP_ATTACK_STAND
                : combat_auto_attack_get(player_get_local_pc_obj()) ? COOP_ATTACK_REPEAT : COOP_ATTACK_ONCE;
            return coop_runtime_combat(COOP_COMBAT_ATTACK,(uint8_t)((aim << COOP_ATTACK_AIM_SHIFT) | (attack << COOP_ATTACK_MODE_SHIFT)),actor);
        }
        if (enemy && !combat_critter_is_combat_mode_active(player_get_local_pc_obj())) intgame_combat_mode_toggle();
        sub_54EA80(&target);
    } else {
        location_origin_set(obj_field_int64_get(actor, OBJ_F_LOCATION));
        char name[160], text[240];
        actor_name(actor, name, sizeof(name));
        snprintf(text, sizeof(text), "%s: %d/%d HP, %d/%d fatigue%s", name, object_hp_current(actor), object_hp_max(actor),
            critter_fatigue_current(actor), critter_fatigue_max(actor), coop_engine_options_immortal(actor) ? " (Immortal)" : "");
        intgame_message_window_display_str(-1, text);
    }
    drawn = 0;
    return true;
}
bool coop_party_ui_target(unsigned member)
{
    if (coop_runtime_phase()!=COOP_RUNTIME_PLAYING || member>=4) return false;
    bool accepted=coop_party_ui_target_actor(coop_campaign_object(member),false);
    if (accepted) selected=member;
    return accepted;
}
static int party_row_height(void) { return member_count ? party_rect.height/(int)member_count : ROW_HEIGHT; }
static unsigned visible_cells(void)
{
    return (unsigned)(turn_rect.width - 112) / CELL_WIDTH;
}
static bool party_events(TigMessage* message)
{
    if (coop_hud_input(HUD_PARTY,party,&party_rect,message,party_rect.width-4,64,SDL_max(64,(int)member_count*72),false)) return true;
    if (message->type != TIG_MESSAGE_MOUSE || !inside(party_rect, message->data.mouse.x, message->data.mouse.y)) return false;
    unsigned row = (unsigned)(message->data.mouse.y - party_rect.y) / (unsigned)party_row_height();
    if (message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP && row < member_count) coop_party_ui_target(members[row]);
    return true;
}
static bool turn_events(TigMessage* message)
{
    if (coop_hud_input(HUD_TURNS,turns,&turn_rect,message,turn_rect.width-112,360,combat_turn_based_is_active() ? 69 : 28,false)) return true;
    if (message->type != TIG_MESSAGE_MOUSE || !inside(turn_rect, message->data.mouse.x, message->data.mouse.y)) return false;
    CombatCampaignState state;
    if (!combat_campaign_state_get(&state)) return true;
    unsigned cells = visible_cells();
    int x = message->data.mouse.x - turn_rect.x, y = message->data.mouse.y - turn_rect.y;
    if (!state.active) {
        if (message->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
            if (x >= turn_rect.width - 108) coop_runtime_combat(COOP_COMBAT_POLICY, !state.turn_based, 0);
            else if (x >= turn_rect.width - 216) coop_lobby_open();
        }
        return true;
    }
    if (message->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL && state.order_count > cells) {
        unsigned pages = (state.order_count + cells - 1) / cells;
        page = message->data.mouse.dy > 0 ? (page + pages - 1) % pages : (page + 1) % pages;
        drawn = 0;
    }
    if (message->data.mouse.event != TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP && message->data.mouse.event != TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP) return true;
    bool right=message->data.mouse.event==TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP;
    if (x >= turn_rect.width - 108) {
        if (right) return true;
        if (y < 23) coop_lobby_open();
        else if (y < 46 && state.active) coop_runtime_combat(COOP_COMBAT_END_TURN, 0, 0);
        else if (y >= 46) coop_runtime_combat(COOP_COMBAT_POLICY, !state.turn_based, 0);
    } else if (y >= 24 && cells) {
        unsigned index = page * cells + (unsigned)x / CELL_WIDTH;
        if (index < state.order_count && state.order[index]) {
            if (right) location_origin_set(obj_field_int64_get(state.order[index],OBJ_F_LOCATION));
            else coop_party_ui_target_actor(state.order[index],true);
        }
    }
    drawn = 0;
    return true;
}
void coop_party_ui_close(void)
{
    coop_hud_detach(HUD_TOOLTIP); coop_hud_detach(HUD_TURNS); coop_hud_detach(HUD_PARTY);
    if (tip != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(tip);
    if (turns != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(turns);
    if (party != TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(party);
    tip = turns = party = TIG_WINDOW_HANDLE_INVALID;
    member_count = page = 0; selected = 4; drawn = hover_since = 0;
    mouse_x = mouse_y = -1; hover_text[0] = 0;
}
static void draw_party(const CombatCampaignState* state)
{
    TigRect clear = { 0, 0, party_rect.width, party_rect.height };
    tig_window_fill(party, &clear, tig_color_make(14, 12, 10));
    for (unsigned row = 0; row < member_count; row++) {
        int64_t actor = coop_campaign_object(members[row]);
        if (!actor) continue;
        int row_height=party_row_height(),y = (int)row * row_height;
        bool active = state->active && state->actor == actor;
        frame(party, (TigRect) { 0, y, party_rect.width, row_height }, active || selected == members[row]);
        int portrait_size=SDL_min(party_rect.width-18,row_height-36);
        actor_portrait(party, actor, (party_rect.width-portrait_size)/2, y + 6, portrait_size);
        char name[128]; actor_name(actor, name, sizeof(name));
        coop_hud_text_fit(name,sizeof(name),party_rect.width-14,coop_play_chrome_font(MM_COLOR_WHITE));
        label(party, name, 7, y+row_height-30, party_rect.width-14, 17, actor == player_get_local_pc_obj() ? MM_COLOR_GOLD : MM_COLOR_WHITE);
        int hp_max = object_hp_max(actor), fp_max = critter_fatigue_max(actor);
        int hp = object_hp_current(actor), fp = critter_fatigue_current(actor);
        if (hp < 0) hp = 0; if (hp > hp_max) hp = hp_max;
        if (fp < 0) fp = 0; if (fp > fp_max) fp = fp_max;
        int bar_width=party_rect.width-14;
        TigRect track={6,y+row_height-13,bar_width+2,5};
        tig_window_fill(party,&track,tig_color_make(12,11,10));
        tig_window_box(party,&track,tig_color_make(100,78,47));
        track.y+=6;
        tig_window_fill(party,&track,tig_color_make(12,11,10));
        tig_window_box(party,&track,tig_color_make(100,78,47));
        TigRect bar = { 7, y + row_height-12, hp_max > 0 ? hp * bar_width / hp_max : 0, 3 };
        if (hp > 0 && bar.width < 1) bar.width = 1;
        tig_window_fill(party, &bar, tig_color_make(185, 53, 43));
        bar.y += 6; bar.width = fp_max > 0 ? fp * bar_width / fp_max : 0;
        tig_window_fill(party, &bar, tig_color_make(66, 116, 170));
        if (coop_engine_options_immortal(actor)) label(party, "I", party_rect.width-16, y + 8, 12, 18, MM_COLOR_GOLD);
        unsigned statuses = coop_party_ui_statuses(actor), badge = 0;
        for (unsigned i=0; i<SDL_arraysize(status_names); i++) if (statuses & (1u<<i)) {
            status_icon(i, 10 + (int)(badge%4)*14, y + portrait_size-8 - (int)(badge/4)*14); badge++;
        }
    }
    coop_hud_handles(HUD_PARTY,party,&party_rect,party_rect.width-4,false);
}
static void draw_turns(const CombatCampaignState* state)
{
    frame(turns, (TigRect) { 0, 0, turn_rect.width, turn_rect.height }, false);
    char title[256], name[128];
    unsigned cells=visible_cells(),pages=cells ? (state->order_count+cells-1)/cells : 0;
    actor_name(state->actor, name, sizeof(name));
    if (state->active) snprintf(title, sizeof(title), "Round %u  |  %s%s  |  %d AP", state->round,
        state->actor == player_get_local_pc_obj() ? "Your turn: " : "", name, state->points);
    else snprintf(title, sizeof(title), "%s", state->turn_based ? "Turn-based ready" : "Real-time combat");
    int title_width=turn_rect.width-(state->active ? pages>1 ? 234 : 124 : 230);
    coop_hud_text_fit(title,sizeof(title),title_width,coop_play_chrome_font(MM_COLOR_GOLD));
    label(turns, title, 8, 4, title_width, 19, MM_COLOR_GOLD);
    if (!state->active) {
        int x = turn_rect.width - 215;
        frame(turns, (TigRect) { x, 2, 104, 23 }, false);
        label(turns, party_shortcut(), x + 4, 5, 98, 19, MM_COLOR_GOLD);
        x += 108; frame(turns, (TigRect) { x, 2, 104, 23 }, false);
        label(turns, state->turn_based ? "Real time" : "Turn based", x + 4, 5, 98, 19, MM_COLOR_GOLD);
        coop_hud_handles(HUD_TURNS,turns,&turn_rect,turn_rect.width-216,false);
        return;
    }
    if (page >= pages) page = 0;
    for (unsigned cell = 0; cell < cells && page * cells + cell < state->order_count; cell++) {
        unsigned index = page * cells + cell;
        int64_t actor = state->order[index];
        int x = (int)cell * CELL_WIDTH + 3;
        coop_hud_surface(turns,index ? HUD_ART_TURN : HUD_ART_TURN_ACTIVE,
            (TigRect){x,25,CELL_WIDTH-4,turn_rect.height-29},!index);
        actor_portrait(turns, actor, x + 3, 28, 32);
        actor_name(actor, name, sizeof(name));
        coop_hud_text_fit(name,sizeof(name),47,coop_play_chrome_font(MM_COLOR_WHITE));
        char order[20];
        if (index) snprintf(order, sizeof(order), "%u", index + 1);
        else snprintf(order, sizeof(order), "NOW");
        label(turns, order, x + 40, 26, 47, 18, index ? MM_COLOR_BROWN : MM_COLOR_GOLD);
        label(turns, name, x + 40, 42, 47, 17, MM_COLOR_WHITE);
    }
    if (pages > 1) {
        char paging[64]; snprintf(paging, sizeof(paging), "%u/%u (wheel)", page + 1, pages);
        label(turns, paging, turn_rect.width - 222, 4, 108, 17, MM_COLOR_GRAY);
    }
    int x = turn_rect.width - 107;
    frame(turns, (TigRect) { x, 1, 105, 21 }, false); label(turns, party_shortcut(), x + 4, 3, 100, 18, MM_COLOR_GOLD);
    if (state->active) {
        frame(turns, (TigRect) { x, 24, 105, 21 }, false);
        label(turns, "End turn", x + 4, 26, 100, 18, state->actor == player_get_local_pc_obj() ? MM_COLOR_GOLD : MM_COLOR_GRAY);
    }
    frame(turns, (TigRect) { x, 47, 105, 20 }, false);
    label(turns, state->turn_based ? "Real time" : "Turn based", x + 4, 48, 100, 18, MM_COLOR_GOLD);
    coop_hud_handles(HUD_TURNS,turns,&turn_rect,turn_rect.width-112,false);
}
static void hint_at(int x, int y, char out[512])
{
    out[0] = 0;
    if (hovered_party) {
        unsigned row = (unsigned)(y - party_rect.y) / (unsigned)party_row_height();
        if (row >= member_count) return;
        int64_t actor = coop_campaign_object(members[row]);
        if (!actor) return;
        char name[128]; actor_name(actor, name, sizeof(name));
        char statuses[200] = ""; unsigned mask = coop_party_ui_statuses(actor);
        for (unsigned i=0; i<SDL_arraysize(status_names); i++) if (mask & (1u<<i)) {
            size_t used = strlen(statuses);
            snprintf(statuses+used,sizeof(statuses)-used,"%s%s",used ? ", " : "Status: ",status_names[i]);
        }
        snprintf(out, 512, "%s\nHealth %d / %d\nFatigue %d / %d\n%s%s%s%s", name, object_hp_current(actor), object_hp_max(actor),
            critter_fatigue_current(actor), critter_fatigue_max(actor),
            statuses, *statuses ? "\n" : "",
            "Click to focus. With an ability selected, click to target.",
            coop_engine_options_immortal(actor) ? "\nImmortal: minimum 1 HP." : "");
        return;
    }
    if (hovered_turns) {
        int local_x = x - turn_rect.x, local_y = y - turn_rect.y;
        if (!combat_turn_based_is_active()) {
            snprintf(out, 512, "%s", local_x >= turn_rect.width - 108
                ? "Host control [Space]: switch between real-time and turn-based combat. Works even when the host is dead."
                : local_x >= turn_rect.width - 216 ? "Party [F12]: chat, pause, rest, saves and the host's per-character immortality switches."
                : "Red portrait bar: health. Blue bar: fatigue. A gold name marks your own character. Select a spell, skill or item, then click a portrait to target an ally.");
            return;
        }
        if (local_x >= turn_rect.width - 108) snprintf(out, 512, "%s", local_y < 23
            ? "Party [F12]: chat, pause, rest, saves and the host's per-character immortality switches."
            : local_y < 46 ? "End your current turn. Unspent action points are lost; the next character acts."
            : "Host control [Space]: switch between real-time and turn-based combat. Works even when the host is dead.");
        else {
            CombatCampaignState state;
            unsigned index = page * visible_cells() + (unsigned)local_x / CELL_WIDTH;
            if (local_y >= 24 && combat_campaign_state_get(&state) && index < state.order_count) {
                int64_t actor = state.order[index]; char name[128]; actor_name(actor, name, sizeof(name));
                snprintf(out, 512, "%s\n%s  |  Health %d / %d\nClick an enemy to attack, or select an ability to target. Right-click to focus.\nWheel: more combatants.",
                    name,index ? "Upcoming turn" : "Current turn",object_hp_current(actor), object_hp_max(actor));
            } else snprintf(out, 512, "Turn order runs left to right. NOW highlights the current actor; AP are their remaining action points. Click an entry to center the view. Scroll to see more combatants. Order may change as actors join or become incapacitated.");
        }
        return;
    }
    if (coop_play_control_hint(x,y,out,512)) return;
    const char* hint = intgame_control_hint(tig_button_get_at_position(x, y));
    if (hint) snprintf(out, 512, "%s", hint);
}
static void update_tooltip(uint64_t now)
{
    if (intgame_map_pan_active() || coop_hud_editing()) {
        if (tip != TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_TOOLTIP); tig_window_destroy(tip); tip = TIG_WINDOW_HANDLE_INVALID; }
        hover_since = now; return;
    }
    TigMouseState mouse;
    if (tig_mouse_get_state(&mouse) != TIG_OK) return;
    tig_window_handle_t top;
    if (tig_window_get_at_position(mouse.x, mouse.y, &top) != TIG_OK) return;
    hovered_party = top == party; hovered_turns = top == turns;
    char text[512], original[512]; hint_at(mouse.x, mouse.y, original);
    if (hovered_party) snprintf(text,sizeof(text),"%s",original);
    else coop_play_shortcuts(original,text,sizeof(text));
    if (mouse.x != mouse_x || mouse.y != mouse_y || strcmp(text, hover_text)) {
        mouse_x = mouse.x; mouse_y = mouse.y; hover_since = now;
        snprintf(hover_text, sizeof(hover_text), "%s", text);
        if (tip != TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_TOOLTIP); tig_window_destroy(tip); tip = TIG_WINDOW_HANDLE_INVALID; }
    }
    if (!*text || now - hover_since < (uint64_t)coop_play_pref(PLAY_PREF_TIP_DELAY) || tip != TIG_WINDOW_HANDLE_INVALID) return;
    char title[160]="Help",body[512];
    const char* separator=strchr(text,'\n');
    int skip=1;
    if (!separator || separator-text>128) { separator=strstr(text,": "); skip=2; }
    if (separator && separator-text<128) {
        snprintf(title,sizeof(title),"%.*s",(int)(separator-text),text);
        snprintf(body,sizeof(body),"%s",separator+skip);
    } else snprintf(body,sizeof(body),"%s",text);
    int maximum=SDL_min(320+(coop_play_pref(PLAY_PREF_TEXT)-100),hrp_iso_window_width_get()-12);
    int width=SDL_clamp(SDL_max(coop_hud_text_width(title),coop_hud_text_width(body))+24,220,maximum);
    int height=SDL_min(coop_hud_card_height(title,body,width),hrp_iso_window_height_get()-12);
    int x = mouse.x + 18, y = mouse.y + 23;
    if (hovered_party) {
        x=party_rect.x+party_rect.width+10;
        y=SDL_max(turn_rect.y+turn_rect.height+8,party_rect.y+(mouse.y-party_rect.y)/party_row_height()*party_row_height()+6);
        y=SDL_min(y,hrp_iso_window_height_get()-166-height-8);
    } else if (hovered_turns) y=turn_rect.y+turn_rect.height+8;
    if (x + width > hrp_iso_window_width_get() - 6) x = hrp_iso_window_width_get() - width - 6;
    /* Keep hover help out of the bottom description area, including when
     * hovering a spell, skill or quickbar button in that area. */
    if (y + height > hrp_iso_window_height_get() - 159) y = mouse.y - height - 12;
    x = SDL_clamp(x,6,hrp_iso_window_width_get()-width-6);
    y = SDL_clamp(y,6,hrp_iso_window_height_get()-height-6);
    if (mouse.x>=x && mouse.x<x+width && mouse.y>=y && mouse.y<y+height) {
        x = mouse.x<hrp_iso_window_width_get()/2 ? mouse.x+8 : mouse.x-width-8;
        x = SDL_clamp(x,6,hrp_iso_window_width_get()-width-6);
    }
    TigWindowData data = { .flags = TIG_WINDOW_ALWAYS_ON_TOP, .rect = { x, y, width, height }, .background_color = tig_color_make(24, 20, 16) };
    if (tig_window_create(&data, &tip) != TIG_OK) { tip = TIG_WINDOW_HANDLE_INVALID; return; }
    coop_hud_attach(HUD_TOOLTIP,tip);
    coop_hud_card(tip,(TigRect){0,0,width,height},title,body,false);
}
void coop_party_ui_tick(uint64_t now)
{
    IntgameMode mode = intgame_mode_get();
    if (coop_runtime_phase()==COOP_RUNTIME_OFF && player_get_local_pc_obj() && intgame_interface_is_visible()
        && !mainmenu_ui_is_active() && (mode==INTGAME_MODE_MAIN || mode==INTGAME_MODE_SPELL || mode==INTGAME_MODE_SKILL || mode==INTGAME_MODE_ITEM)) {
        if (party!=TIG_WINDOW_HANDLE_INVALID || turns!=TIG_WINDOW_HANDLE_INVALID) coop_party_ui_close();
        update_tooltip(now); return;
    }
    if (coop_runtime_phase() != COOP_RUNTIME_PLAYING || !intgame_interface_is_visible() || mainmenu_ui_is_active() || coop_lobby_is_open()
        || (mode != INTGAME_MODE_MAIN && mode != INTGAME_MODE_SPELL && mode != INTGAME_MODE_SKILL && mode != INTGAME_MODE_ITEM && mode != INTGAME_MODE_FOLLOWER)) {
        coop_party_ui_close(); return;
    }
    unsigned count = 0, roster[4];
    for (unsigned i = coop_campaign_next_slot(0); i < 4; i = coop_campaign_next_slot(i + 1))
        if (coop_campaign_object(i)) roster[count++] = i;
    if (!count) { coop_party_ui_close(); return; }
    CombatCampaignState state;
    if (!combat_campaign_state_get(&state)) return;
    int turn_height = combat_turn_based_is_active() ? 69 : 28;
    /* The original character/map/inventory toolbar occupies y=0..40. Keep
     * both panels in the world area below it, beside rather than below one
     * another so four portraits also fit above the 600px layout's bottom HUD. */
    TigRect next_party = coop_hud_rect(HUD_PARTY,(TigRect){6,44,PARTY_WIDTH,(int)count*ROW_HEIGHT},64,(int)count*72);
    int turn_width=state.active ? 116+CELL_WIDTH*(int)SDL_clamp(state.order_count,3,6) : 440;
    TigRect next_turns = coop_hud_rect(HUD_TURNS,(TigRect){94,44,SDL_min(hrp_iso_window_width_get()-104,turn_width),turn_height},360,turn_height);
    if (party != TIG_WINDOW_HANDLE_INVALID && (memcmp(&party_rect, &next_party, sizeof(party_rect)) || memcmp(&turn_rect, &next_turns, sizeof(turn_rect)))) coop_party_ui_close();
    member_count = count; memcpy(members, roster, count * sizeof(*members));
    if (party == TIG_WINDOW_HANDLE_INVALID) {
        party_rect = next_party; turn_rect = next_turns;
        TigWindowData data = { .flags = TIG_WINDOW_MESSAGE_FILTER, .rect = party_rect, .background_color = tig_color_make(14, 12, 10), .message_filter = party_events };
        if (tig_window_create(&data, &party) != TIG_OK) { party = TIG_WINDOW_HANDLE_INVALID; return; }
        data.rect = turn_rect; data.message_filter = turn_events;
        if (tig_window_create(&data, &turns) != TIG_OK) { turns = TIG_WINDOW_HANDLE_INVALID; coop_party_ui_close(); return; }
        coop_hud_attach(HUD_PARTY,party); coop_hud_attach(HUD_TURNS,turns);
    }
    if (state.generation != generation) { generation = state.generation; page = 0; drawn = 0; }
    if (coop_hud_changed(HUD_PARTY) || coop_hud_changed(HUD_TURNS)) drawn=0;
    if (!drawn || now - drawn >= 200) { draw_party(&state); draw_turns(&state); drawn = now; }
    update_tooltip(now);
}
