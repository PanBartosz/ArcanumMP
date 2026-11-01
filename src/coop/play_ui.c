#include "coop/localization.h"
#include "coop/modern.h"
#include "coop/hud.h"
#include "coop/play_data.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/object.h"
#include "game/player.h"
#include "game/quest.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/logbook_ui.h"
#include "ui/schematic_ui.h"
#include "ui/wmap_ui.h"
#include <stdio.h>
#include <string.h>

static tig_window_handle_t window=TIG_WINDOW_HANDLE_INVALID,bar=TIG_WINDOW_HANDLE_INVALID,tracker=TIG_WINDOW_HANDLE_INVALID,settings_link=TIG_WINDOW_HANDLE_INVALID,log_button=TIG_WINDOW_HANDLE_INVALID;
static TigRect bounds,bar_rect,tracker_rect,log_button_rect,settings_link_rect;
static unsigned window_element=HUD_TOOLS;
static bool preserve_log_state,restore_log=true;
static unsigned tab,first,selected,count,filter,assign_slot;
static bool compact,wide,search_focus,note_focus,filter_craft,recent;
static int capturing=-1;
static SDL_Scancode closing_key=SDL_SCANCODE_UNKNOWN;
static char query[128],note_edit[241],feedback[256];
static PlayRow rows[PLAY_ROWS];
static uint64_t refreshed,aux_refreshed;
static int detail_scroll;
static bool log_follow=true;
static uint32_t refreshed_log_end;
typedef struct Button { unsigned id; TigRect rect; } Button;
static Button buttons[16]; static unsigned button_count;
static void text_input(bool active)
{ SDL_Window* native; if (tig_video_window_get(&native)==TIG_OK) { if (active) SDL_StartTextInput(native); else SDL_StopTextInput(native); } }
static void refresh(void);
static bool input(TigMessage* msg);
static int row_height(void) { return compact ? coop_play_line_height()*2+4 : coop_play_line_height()+8; }
static int list_top(void) { return compact ? coop_play_line_height()+24 : tab==PLAY_TAB_SETTINGS ? 82 : 112; }
static int list_bottom(void) { return bounds.height-(compact ? coop_play_line_height()+20 : 68); }
static unsigned visible(void) { int n=(list_bottom()-list_top())/row_height(); return n>0 ? (unsigned)n : 1; }
static int compact_log_width(void) { return SDL_clamp((hrp_iso_window_width_get()-224)/2,320,420); }
static TigRect action_defaults(void)
{
    int sw=hrp_iso_window_width_get(),sh=hrp_iso_window_height_get();
    bool narrow=sw<1024 || sh<680;
    int width=narrow ? SDL_min(600,sw-compact_log_width()-128) : SDL_min(sw-208,840+(coop_play_pref(PLAY_PREF_TEXT)-100)*2);
    int height=2*(coop_play_line_height()+8),margin=combat_turn_based_is_active() ? 194 : 166;
    return (TigRect){narrow ? 104 : (sw-width)/2,sh-margin-height,width,height};
}
static int split(void) { return bounds.width*47/100; }
static void write_at(tig_window_handle_t w,const char* text,TigRect rect,MainMenuColor color)
{
    tig_font_push(coop_play_font(color)); tig_window_text_write(w,text,&rect); tig_font_pop();
}
static void text(const char* value,int x,int y,int width,MainMenuColor color)
{ write_at(window,value,(TigRect){x,y,width,coop_play_line_height()+2},color); }
static void detail_text(const char* value,TigRect rect)
{
    rect.width-=14;
    TigFont measure={.str=value,.width=rect.width};
    tig_font_push(coop_play_font(MM_COLOR_WHITE)); tig_font_measure(&measure); tig_font_pop();
    int height=SDL_max(rect.height,SDL_min(4096,measure.height+4));
    detail_scroll=SDL_clamp(detail_scroll,0,SDL_max(0,height-rect.height));
    tig_window_handle_t buffer;
    TigWindowData data={.flags=TIG_WINDOW_HIDDEN,.rect={0,0,rect.width,height},.background_color=tig_color_make(24,22,18)};
    if (tig_window_create(&data,&buffer)!=TIG_OK) return;
    write_at(buffer,value,(TigRect){0,0,rect.width,height},MM_COLOR_WHITE);
    TigRect source={0,detail_scroll,rect.width,rect.height};
    TigWindowBlitInfo blit={.type=TIG_WINDOW_BLIT_WINDOW_TO_WINDOW,.src_window_handle=buffer,.src_rect=&source,.dst_window_handle=window,.dst_rect=&rect};
    tig_window_blit(&blit); tig_window_destroy(buffer);
    if (height>rect.height) {
        TigRect track={rect.x+rect.width+5,rect.y,6,rect.height}; tig_window_fill(window,&track,tig_color_make(45,42,34));
        int thumb=SDL_max(15,rect.height*rect.height/height);
        TigRect handle={track.x,track.y+(rect.height-thumb)*detail_scroll/(height-rect.height),6,thumb};
        tig_window_fill(window,&handle,tig_color_make(190,162,95));
    }
}
static void box(tig_window_handle_t w,TigRect rect,bool active)
{
    coop_hud_frame(w,rect,active);
}
static void button(unsigned id,const char* label,int x,int y,int width)
{
    TigRect r={x,y,width,id>=10 && id<=13 ? 26 : coop_play_line_height()+8}; coop_hud_surface(window,HUD_ART_BUTTON,r,false);
    if (id>=10 && id<=13) {
        unsigned art=id==10 ? compact ? HUD_ART_EXPAND : HUD_ART_COMPACT
            : id==11 ? HUD_ART_WIDE : id==12 ? HUD_ART_CLOSE : HUD_ART_OPACITY;
        bool icon_only=width<=30;
        int size=18,icon_x=icon_only ? x+(width-size)/2 : x+5;
        coop_hud_art(window,art,(TigRect){icon_x,y+(r.height-size)/2,size,size});
        if (!icon_only) {
            TigRect caption={x+27,y+(r.height-18)/2,width-30,18};
            tig_font_push(coop_play_chrome_font(MM_COLOR_GOLD));
            tig_window_text_write(window,label,&caption); tig_font_pop();
        }
    } else text(label,x+6,y+3,width-12,MM_COLOR_GOLD);
    if (button_count<SDL_arraysize(buttons)) buttons[button_count++]=(Button){id,r};
}
static bool inside(TigRect r,int x,int y) { return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height; }
bool coop_play_ui_active(void) { return window!=TIG_WINDOW_HANDLE_INVALID; }
bool coop_play_ui_blocking(void) { return coop_play_ui_active() && !compact; }
void coop_play_ui_close(void)
{
    if (window!=TIG_WINDOW_HANDLE_INVALID) {
        if (!preserve_log_state && tab==PLAY_TAB_LOG) coop_hud_log_state(false,compact);
        coop_hud_detach(window_element); tig_window_destroy(window);
    }
    window=TIG_WINDOW_HANDLE_INVALID; search_focus=note_focus=false; capturing=-1; closing_key=SDL_SCANCODE_UNKNOWN; text_input(false);
}
void coop_play_ui_reset(void)
{
    preserve_log_state=true; coop_play_ui_close(); preserve_log_state=false; restore_log=true;
    coop_hud_detach(HUD_ACTIONS); coop_hud_detach(HUD_QUESTS); coop_hud_detach(HUD_LOG_BUTTON);
    tig_window_handle_t* windows[]={&bar,&tracker,&settings_link,&log_button};
    for (unsigned i=0;i<SDL_arraysize(windows);i++) {
        if (*windows[i]!=TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(*windows[i]);
        *windows[i]=TIG_WINDOW_HANDLE_INVALID;
    }
}
static bool create_window(void)
{
    int sw=hrp_iso_window_width_get(),sh=hrp_iso_window_height_get();
    int width=SDL_min(sw-40,wide ? 1120 : 760),height=SDL_min(sh-115,570);
    window_element=compact ? HUD_LOG_COMPACT : tab==PLAY_TAB_LOG ? HUD_LOG : HUD_TOOLS;
    TigRect fallback={(sw-width)/2,(sh-height)/2,width,height};
    if (compact) {
        int line=coop_play_line_height();
        int margin=combat_turn_based_is_active() ? 194 : 166;
        width=compact_log_width();
        TigRect actions=bar!=TIG_WINDOW_HANDLE_INVALID ? bar_rect : action_defaults();
        bool overlaps=actions.x+actions.width>sw-width-16 && actions.x<sw-4;
        int bottom=sh-margin-(coop_play_pref(PLAY_PREF_BAR) && overlaps ? actions.height : 0)-8;
        int top=tracker!=TIG_WINDOW_HANDLE_INVALID ? SDL_max(114,tracker_rect.y+tracker_rect.height+8) : 114;
        int chrome=2*line+44,events=SDL_clamp((bottom-top-chrome)/row_height(),1,3);
        height=chrome+events*row_height();
        fallback=(TigRect){sw-width-12,SDL_max(80,bottom-height),width,height};
    }
    bounds=coop_hud_rect(window_element,fallback,compact ? 320 : 760,compact ? 2*coop_play_line_height()+44+row_height() : 320);
    TigWindowData d={.flags=TIG_WINDOW_MESSAGE_FILTER|TIG_WINDOW_ALWAYS_ON_TOP,.rect=bounds,.message_filter=input,.background_color=tig_color_make(24,22,18)};
    if (tig_window_create(&d,&window)!=TIG_OK) { window=TIG_WINDOW_HANDLE_INVALID; return false; }
    coop_hud_attach(window_element,window);
    return true;
}
bool coop_play_ui_open(unsigned requested)
{
    if (requested>=PLAY_TABS || (!player_get_local_pc_obj() && requested!=PLAY_TAB_SETTINGS)) return false;
    if (window!=TIG_WINDOW_HANDLE_INVALID && tab==requested) { coop_play_ui_close(); return true; }
    coop_play_ui_close(); tab=requested; first=selected=filter=0; detail_scroll=0; log_follow=true;
    compact=requested==PLAY_TAB_LOG && coop_hud_log_compact(); *query=0; *feedback=0; count=0;
    coop_play_data_bind(); snprintf(note_edit,sizeof(note_edit),"%s",coop_play_note());
    if (!create_window()) return false;
    if (tab==PLAY_TAB_LOG) coop_hud_log_state(true,compact);
    refresh(); return true;
}
static void settings_rows(void)
{
    const char* names[]={coop_tr("Text size"),coop_tr("High contrast"),coop_tr("Toggle Alt labels"),coop_tr("Hide empty containers"),coop_tr("Relationship symbols"),coop_tr("Combat previews"),coop_tr("Extra action bar"),coop_tr("Tooltip delay")};
    count=0;
    for (unsigned i=0;i<SDL_arraysize(names);i++) {
        PlayRow row={.id=(int)i,.available=true}; int value=coop_play_pref(i);
        if (i==PLAY_PREF_TEXT) snprintf(row.name,sizeof(row.name),"%s: %d%%",names[i],value);
        else if (i==PLAY_PREF_TIP_DELAY) snprintf(row.name,sizeof(row.name),"%s: %d ms",names[i],value);
        else snprintf(row.name,sizeof(row.name),"%s: %s",names[i],value ? coop_tr("On") : coop_tr("Off"));
        const char* descriptions[]={coop_tr("Dialogue, tooltip and play-tool text scale independently of the whole UI. Takes effect immediately."),
            coop_tr("Brighter text against dark panels. Color remains supplemented by labels and symbols."),
            coop_tr("Tap your highlight key to show or hide labels. When disabled, hold the key to highlight."),
            coop_tr("Hide labels for empty containers and looted bodies. Hidden traps and invisible objects stay hidden."),
            coop_tr("Hover a character to read [+] Ally, [=] Neutral or [!] Enemy alongside its circle color."),
            coop_tr("Preview movement and attack AP, range and line of sight before committing. Spell targeting also shows area boundaries."),
            coop_tr("Two extra rows of six personal action slots. Assign spells, skills or items from the Actions or Items tabs."),
            coop_tr("Delay before hover help appears. Choose immediate, 300, 550 or 1000 milliseconds.")};
        snprintf(row.detail,sizeof(row.detail),coop_tr("%s\n\nClick or press Enter to change."),descriptions[i]); rows[count++]=row;
    }
    const char* reasons[]={coop_tr("Enemy spotted"),coop_tr("Critical health"),coop_tr("Party member down"),coop_tr("Weapon broken")};
    for (unsigned i=0;i<4;i++) {
        PlayRow row={.id=100+(int)i,.available=true};
        snprintf(row.name,sizeof(row.name),coop_tr("Auto-pause: %s [%s]"),reasons[i],coop_play_pause_policy()&(1u<<i) ? coop_tr("On") : coop_tr("Off"));
        snprintf(row.detail,sizeof(row.detail),coop_tr("Pause automatically: %s.\n\nMultiplayer: the host selects the session policy and all clients see it. Ask the host before enabling automatic pauses. Each player can release their own pause using Pause or the visible banner.\n\nIn single-player this is a personal setting."),reasons[i]); rows[count++]=row;
    }
    PlayRow layout={.id=400,.available=true};
    snprintf(layout.name,sizeof(layout.name),coop_tr("Edit HUD layout: %s"),coop_hud_editing() ? coop_tr("On") : coop_tr("Off"));
    snprintf(layout.detail,sizeof(layout.detail),coop_tr("Show move and resize handles on custom HUD panels. Close this panel, drag a title strip to move, or drag a bottom-right grip to resize. Click UI beside the combat-log icon to finish. Positions and sizes are saved across launches.")); rows[count++]=layout;
    layout.id=401; snprintf(layout.name,sizeof(layout.name),coop_tr("Reset HUD positions and sizes"));
    snprintf(layout.detail,sizeof(layout.detail),coop_tr("Restore default panel positions and sizes. Transparency and your other settings are preserved.")); rows[count++]=layout;
    layout.id=402;
    snprintf(layout.name,sizeof(layout.name),coop_tr("Language: %s (restart required)"),coop_tr(coop_language_name(coop_language_pending())));
    snprintf(layout.detail,sizeof(layout.detail),"%s",coop_tr("Language is personal. Each player can use a different translation in the same campaign. Restart the game after changing it."));rows[count++]=layout;
    for (unsigned i=0;i<HUD_COUNT;i++) {
        PlayRow row={.id=300+(int)i,.available=true};
        snprintf(row.name,sizeof(row.name),coop_tr("%s: %d%% transparent"),coop_hud_name(i),100-coop_hud_opacity(i));
        snprintf(row.detail,sizeof(row.detail),coop_tr("Individual transparency for %s. Click to cycle 0%%, 25%%, 50%% or 75%% transparency. Saved across game launches. Combat logs also have a percentage button in their title bar."),coop_hud_name(i)); rows[count++]=row;
    }
    for (unsigned i=0;i<coop_play_binding_count();i++) {
        PlayRow row={.id=200+(int)i,.available=true};
        snprintf(row.name,sizeof(row.name),"%s: %s",coop_play_binding_name(i),SDL_GetScancodeName(coop_play_binding_key(i)));
        snprintf(row.detail,sizeof(row.detail),coop_tr("%s\nCurrent key: %s\n\nClick or press Enter, then press a replacement key. Escape cancels. Conflicting assignments swap so every command remains reachable. Text entry keeps ordinary typing."),coop_play_binding_name(i),SDL_GetScancodeName(coop_play_binding_key(i))); rows[count++]=row;
    }
}
static void log_rows(void)
{
    int anchor=!log_follow && count && selected<count ? rows[selected].id : 0;
    count=0; int64_t pc=player_get_local_pc_obj(); unsigned mine=coop_play_members(pc);
    for (uint32_t n=coop_play_event_end();n>coop_play_event_first() && count<PLAY_ROWS;n--) {
        const PlayEvent* e=coop_play_event_at(n-1); if (!e) continue;
        if ((filter==1 && mine && !(e->members&mine)) || (filter==2 && e->kind!=PLAY_COMBAT && e->kind!=PLAY_DEATH)
            || (filter==3 && e->kind!=PLAY_EFFECT)) continue;
        if (*query && !SDL_strcasestr(coop_tr_rendered(e->summary),query) && !SDL_strcasestr(coop_tr_rendered(e->details),query)) continue;
        PlayRow row={.id=(int)e->sequence,.available=true}; snprintf(row.name,sizeof(row.name),"%s",coop_tr_rendered(e->summary));
        snprintf(row.detail,sizeof(row.detail),coop_tr("%s\n\n%s\n\nEvent %u | Map %u"),coop_tr_rendered(e->summary),coop_tr_rendered(e->details),e->sequence,e->map); rows[count++]=row;
    }
    if (anchor) for (unsigned i=0;i<count;i++) if (rows[i].id==anchor) { selected=i; break; }
    if (log_follow) first=selected=0;
}
static void refresh(void)
{
    if (window==TIG_WINDOW_HANDLE_INVALID) return;
    switch (tab) {
    case PLAY_TAB_LOG: log_rows(); break;
    case PLAY_TAB_INVENTORY: count=coop_play_inventory(rows,query,filter); break;
    case PLAY_TAB_ACTIONS: count=coop_play_actions(rows,query); break;
    case PLAY_TAB_QUESTS: count=coop_play_quests(rows,query,recent); break;
    case PLAY_TAB_CRAFT: count=coop_play_recipes(rows,query,filter_craft); break;
    default: settings_rows(); break;
    }
    if (selected>=count) selected=count ? count-1 : 0;
    if (first>selected) first=selected;
    if (selected>=first+visible()) first=selected-visible()+1;
    box(window,(TigRect){0,0,bounds.width,bounds.height},false); button_count=0;
    const char* titles[]={coop_tr("Combat log"),coop_tr("Inventory"),coop_tr("Abilities"),coop_tr("Quest notes"),coop_tr("Workshop"),coop_tr("Settings")};
    int title_width=bounds.width-(compact ? 144 : 180);
    text(titles[tab],30,8,title_width-34,MM_COLOR_GOLD);
    button(10,"",bounds.width-(compact ? 138 : 174),4,26);
    if (!compact) button(11,"",bounds.width-138,4,26);
    char opacity[32]; snprintf(opacity,sizeof(opacity),"%d%%",coop_hud_opacity(window_element));
    button(13,opacity,bounds.width-100,4,64); button(12,"X",bounds.width-32,4,26);
    const char* tabs[]={coop_tr("Log"),coop_tr("Items"),coop_tr("Actions"),coop_tr("Quests"),coop_tr("Craft"),coop_tr("Settings")};
    if (!compact) {
        for (unsigned i=0;i<PLAY_TABS;i++) { int width=(bounds.width-24)/PLAY_TABS; int x=12+(int)i*width;
            coop_hud_surface(window,HUD_ART_BUTTON,(TigRect){x,39,width-3,28},i==tab); text(tabs[i],x+8,42,width-12,i==tab ? MM_COLOR_GOLD : MM_COLOR_WHITE); }
        char find[180]; snprintf(find,sizeof(find),"%s%s",search_focus ? "> " : coop_tr("Search: "),query);
        if (tab!=PLAY_TAB_SETTINGS) { box(window,(TigRect){12,76,bounds.width-268,29},search_focus); text(find,18,79,bounds.width-280,MM_COLOR_WHITE); }
        if (tab==PLAY_TAB_LOG) { const char* f[]={coop_tr("All events"),coop_tr("My character"),coop_tr("Combat"),coop_tr("Effects")}; button(1,f[filter],bounds.width-248,76,232); }
        if (tab==PLAY_TAB_INVENTORY) { const char* f[]={coop_tr("All items"),coop_tr("Equipment"),coop_tr("Consumables"),coop_tr("Miscellaneous"),coop_tr("Marked junk")}; button(1,f[filter],bounds.width-248,76,232); }
        if (tab==PLAY_TAB_QUESTS) button(1,recent ? coop_tr("Recent changes") : coop_tr("All known quests"),bounds.width-248,76,232);
        if (tab==PLAY_TAB_CRAFT) button(1,filter_craft ? coop_tr("Craftable now") : coop_tr("All known recipes"),bounds.width-248,76,232);
    }
    int y=list_top();
    unsigned shown=visible();
    int list_width=compact ? bounds.width-24 : split()-24;
    for (unsigned i=0;i<shown && first+i<count;i++) {
        unsigned index=first+i; TigRect r={12,y+(int)i*row_height(),list_width,row_height()-2};
        if (index==selected && (!compact || !log_follow)) {
            tig_window_fill(window,&r,tig_color_make(39,33,23));
            TigRect accent={r.x,r.y,2,r.height}; tig_window_fill(window,&accent,tig_color_make(184,145,77));
        }
        char value[192]; snprintf(value,sizeof(value),"%s%s",rows[index].marked ? "* " : "",rows[index].name);
        if (compact) {
            TigRect dot={18,r.y+9,3,3}; tig_window_fill(window,&dot,tig_color_make(176,141,82));
            write_at(window,value,(TigRect){27,r.y+3,list_width-21,r.height-5},MM_COLOR_WHITE);
        } else {
            coop_hud_text_fit(value,sizeof(value),list_width-15,coop_play_font(MM_COLOR_WHITE));
            text(value,21,r.y+3,list_width-15,rows[index].available ? MM_COLOR_WHITE : MM_COLOR_GRAY);
        }
    }
    if (!count) text(tab==PLAY_TAB_LOG ? coop_tr("No matching events yet.") : coop_tr("No matching entries."),18,y,list_width,MM_COLOR_GRAY);
    if (!compact && count) {
        TigRect inset={split()+3,list_top()-2,bounds.width-split()-15,list_bottom()-list_top()-8};
        tig_window_fill(window,&inset,tig_color_make(24,22,18));
        tig_window_box(window,&inset,tig_color_make(76,61,39));
        TigRect detail={split()+14,list_top()+8,bounds.width-split()-37,list_bottom()-list_top()-30};
        detail_text(capturing>=0 ? coop_tr("Press a new key. Escape cancels.") : rows[selected].detail,detail);
    }
    int action_y=bounds.height-65;
    if (!compact) {
        if (tab==PLAY_TAB_INVENTORY) { button(2,coop_tr("Mark / unmark"),12,action_y,165); button(3,coop_tr("Sell junk"),183,action_y,125); button(4,coop_tr("Use / equip"),314,action_y,148); char slot[48]; snprintf(slot,sizeof(slot),coop_tr("Slot %u >"),assign_slot+1); button(5,slot,468,action_y,110); button(6,coop_tr("Assign"),584,action_y,120); }
        if (tab==PLAY_TAB_ACTIONS) { button(2,coop_tr("Use action"),12,action_y,155); char s[80]; snprintf(s,sizeof(s),coop_tr("Slot %u >"),assign_slot+1); button(3,s,173,action_y,130); button(4,coop_tr("Assign to slot"),309,action_y,165); }
        if (tab==PLAY_TAB_QUESTS) { button(2,coop_tr("Pin / unpin"),12,action_y,150); button(3,coop_tr("Next-step note"),168,action_y,170); button(4,coop_tr("Mark reviewed"),344,action_y,165); button(5,coop_tr("Map"),515,action_y,90); button(6,coop_tr("Journal"),611,action_y,130); }
        if (tab==PLAY_TAB_CRAFT) button(2,coop_tr("Craft selected"),12,action_y,190);
        if (tab==PLAY_TAB_SETTINGS) { button(2,coop_tr("Change selected"),12,action_y,210); button(3,coop_tr("Reset key bindings"),228,action_y,235); }
        if (tab==PLAY_TAB_LOG) { button(2,coop_tr("Latest"),12,action_y,130); button(3,coop_tr("Export log"),148,action_y,160); }
    }
    if (note_focus) { char n[290]; snprintf(n,sizeof(n),coop_tr("Next step: %s_"),note_edit); text(n,12,bounds.height-33,bounds.width-24,MM_COLOR_GOLD); }
    else {
        const char* status=*feedback ? feedback : tab==PLAY_TAB_INVENTORY && *coop_play_bulk_status() ? coop_play_bulk_status()
            : compact ? coop_tr("Wheel: history  |  Click: full entry") : coop_tr("Wheel / arrows: browse  |  [F11] / [B] / Esc: close");
        char bound[512]; coop_play_shortcuts(status,bound,sizeof(bound));
        text(bound,12,bounds.height-30,bounds.width-38,MM_COLOR_GRAY);
    }
    coop_hud_handles(window_element,window,&bounds,title_width,true);
    refreshed=SDL_GetTicks();
    if (tab==PLAY_TAB_LOG) refreshed_log_end=coop_play_event_end();
}
static void change_setting(void)
{
    if (!count) return; int id=rows[selected].id;
    if(id==402) {
        unsigned next=(coop_language_pending()+1)%coop_language_count();
        if(!coop_language_available(next)){snprintf(feedback,sizeof(feedback),"%s",coop_tr("Install the selected language pack before choosing it."));return;}
        bool good=coop_language_select(next);
        snprintf(feedback,sizeof(feedback),"%s",coop_tr(good?"Setting saved.":"Could not save language choice. Check that the game folder is writable."));return;
    }
    if (id==400) { coop_hud_edit_toggle(); snprintf(feedback,sizeof(feedback),"%s",coop_hud_editing() ? coop_tr("Layout editing on. Close tools, then drag panel handles; click UI beside the log icon to finish.") : coop_tr("Layout editing off.")); return; }
    if (id==401) {
        coop_hud_reset_layout(); coop_hud_detach(window_element); tig_window_destroy(window); window=TIG_WINDOW_HANDLE_INVALID;
        create_window(); snprintf(feedback,sizeof(feedback),coop_tr("HUD positions and sizes reset.")); return;
    }
    if (id>=300) { unsigned element=(unsigned)(id-300); int opacity=coop_hud_opacity(element); coop_hud_opacity_set(element,opacity==25 ? 100 : opacity-25); return; }
    if (id>=200) { capturing=id-200; search_focus=note_focus=false; return; }
    bool good=false;
    if (id>=100) {
        unsigned bit=1u<<(id-100); good=coop_play_pref_set(PLAY_PREF_AUTOPAUSE,(int)(coop_play_pause_policy()^bit));
    } else {
        int value=coop_play_pref((unsigned)id);
        if (id==PLAY_PREF_TEXT) value=value==150 ? 100 : value+25;
        else if (id==PLAY_PREF_TIP_DELAY) value=value==0 ? 300 : value==300 ? 550 : value==550 ? 1000 : 0;
        else value=!value;
        good=coop_play_pref_set((unsigned)id,value);
    }
    snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Setting saved.") : coop_tr("Could not change this setting. Multiplayer pause policy is controlled by the host."));
}
static void export_log(void)
{
    FILE* f=fopen("arcmp-combat-log.txt","wt"); if (!f) { snprintf(feedback,sizeof(feedback),coop_tr("Could not write combat log.")); return; }
    bool good=true;
    for (uint32_t n=coop_play_event_first();n<coop_play_event_end();n++) {
        const PlayEvent* e=coop_play_event_at(n); if (e) good &= fprintf(f,"%u | %s\n%s\n\n",e->sequence,coop_tr_rendered(e->summary),coop_tr_rendered(e->details))>0;
    }
    good=fclose(f)==0 && good; snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Saved arcmp-combat-log.txt beside the executable.") : coop_tr("Could not finish writing combat log."));
}
static void activate(unsigned id)
{
    if (id==12) { coop_play_ui_close(); return; }
    if (id==13) { int opacity=coop_hud_opacity(window_element); coop_hud_opacity_set(window_element,opacity==25 ? 100 : opacity-25); refresh(); return; }
    if (id==10 || id==11) {
        coop_hud_detach(window_element);
        if (id==10) { compact=!compact; tab=PLAY_TAB_LOG; coop_hud_log_state(true,compact); }
        else { wide=!wide; TigRect r=bounds; r.width=SDL_min(hrp_iso_window_width_get()-40,wide ? 1120 : 760); coop_hud_store(window_element,&r); }
        tig_window_destroy(window); window=TIG_WINDOW_HANDLE_INVALID; if (create_window()) refresh(); return;
    }
    if (id==1) {
        first=selected=0;
        if (tab==PLAY_TAB_LOG) filter=(filter+1)%4;
        if (tab==PLAY_TAB_INVENTORY) filter=(filter+1)%5;
        if (tab==PLAY_TAB_QUESTS) recent=!recent;
        if (tab==PLAY_TAB_CRAFT) filter_craft=!filter_craft;
    } else if (tab==PLAY_TAB_SETTINGS) {
        if (id==2) change_setting(); if (id==3) coop_play_binding_reset();
    } else if (tab==PLAY_TAB_LOG) {
        if (id==2) { first=selected=0; log_follow=true; } if (id==3) export_log();
    } else if (tab==PLAY_TAB_INVENTORY) {
        if (id==2 && count) coop_play_junk(rows[selected].object,true);
        if (id==3) coop_play_bulk_start();
        if (id==4 && count) { PlayRow row=rows[selected]; coop_play_ui_close(); coop_play_action_activate(&row); return; }
        if (id==5) assign_slot=(assign_slot+1)%PLAY_EXTRA_SLOTS;
        if (id==6 && count && coop_play_action_assign(assign_slot,&rows[selected])) { coop_play_pref_set(PLAY_PREF_BAR,1); snprintf(feedback,sizeof(feedback),coop_tr("Item assigned to the extra action bar.")); }
    } else if (tab==PLAY_TAB_ACTIONS) {
        if (id==2 && count) { PlayRow row=rows[selected]; coop_play_ui_close(); coop_play_action_activate(&row); return; }
        if (id==3) assign_slot=(assign_slot+1)%PLAY_EXTRA_SLOTS;
        if (id==4 && count) {
            bool good=coop_play_action_assign(assign_slot,&rows[selected]);
            if (good) coop_play_pref_set(PLAY_PREF_BAR,1);
            snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Action assigned to the extra bar.") : coop_tr("Could not save this action."));
        }
    } else if (tab==PLAY_TAB_QUESTS) {
        if (id==2 && count) { bool good=coop_play_pin(rows[selected].id,true); snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Objective pinned.") : coop_tr("Objective unpinned, or five objectives are already pinned.")); }
        if (id==3) { note_focus=true; search_focus=false; text_input(true); }
        if (id==4) coop_play_quests_seen();
        if (id==5) { coop_play_ui_close(); wmap_ui_open(); return; }
        if (id==6) { coop_play_ui_close(); logbook_ui_open_quests(player_get_local_pc_obj()); return; }
    } else if (tab==PLAY_TAB_CRAFT && id==2 && count) {
        if (!rows[selected].available) snprintf(feedback,sizeof(feedback),coop_tr("Missing ingredients or expertise; see the selected recipe."));
        else {
            bool good=coop_runtime_gameplay() ? coop_runtime_craft((unsigned)rows[selected].id,player_get_local_pc_obj()) : schematic_ui_process(rows[selected].id,player_get_local_pc_obj(),player_get_local_pc_obj());
            snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Craft request submitted using the original recipe.") : coop_tr("Crafting could not start."));
        }
    }
    refresh();
}
static bool input(TigMessage* msg)
{
    if (coop_hud_input(window_element,window,&bounds,msg,bounds.width-(compact ? 144 : 180),compact ? 320 : 760,compact ? 2*coop_play_line_height()+44+row_height() : 320,true)) {
        if (coop_hud_changed(window_element)) refresh(); return true;
    }
    if (msg->type==TIG_MESSAGE_KEYBOARD) {
        SDL_Scancode key=msg->data.keyboard.scancode;
        if (!msg->data.keyboard.pressed) {
            if (key==closing_key) { coop_play_ui_close(); return true; }
            return !compact;
        }
        if (capturing>=0) {
            if (key!=SDL_SCANCODE_ESCAPE) {
                bool good=coop_play_binding_set((unsigned)capturing,key); snprintf(feedback,sizeof(feedback),"%s",good ? coop_tr("Binding saved.") : coop_tr("This key could not be assigned."));
            }
            capturing=-1; refresh(); return true;
        }
        if (key==coop_play_binding_key(5) && player_get_local_pc_obj()) { coop_play_pause_toggle(); refresh(); return true; }
        if ((search_focus || note_focus) && (key==SDL_SCANCODE_RETURN || key==SDL_SCANCODE_ESCAPE)) {
            if (note_focus && key==SDL_SCANCODE_RETURN) coop_play_note_set(note_edit);
            note_focus=search_focus=false; text_input(false); refresh(); return true;
        }
        if (key==SDL_SCANCODE_ESCAPE || key==coop_play_binding_key(0) || key==coop_play_binding_key(1)) { closing_key=key; return true; }
        if ((search_focus || note_focus) && key==SDL_SCANCODE_BACKSPACE) {
            char* field=note_focus ? note_edit : query; size_t n=strlen(field); if (n) field[n-1]=0; first=selected=0; refresh(); return true;
        }
        unsigned previous=selected;
        if (key==SDL_SCANCODE_UP || key==SDL_SCANCODE_PAGEUP) selected=selected>0 ? selected-1 : 0;
        if ((key==SDL_SCANCODE_DOWN || key==SDL_SCANCODE_PAGEDOWN) && selected+1<count) selected++;
        if (selected!=previous) { detail_scroll=0; log_follow=false; }
        if (key==SDL_SCANCODE_RETURN && !search_focus && !note_focus) activate(tab==PLAY_TAB_SETTINGS ? 2 : 2);
        bool handled=key==SDL_SCANCODE_UP || key==SDL_SCANCODE_DOWN || key==SDL_SCANCODE_PAGEUP || key==SDL_SCANCODE_PAGEDOWN || key==SDL_SCANCODE_RETURN;
        if (handled || !compact) refresh();
        return handled || !compact;
    }
    if (msg->type==TIG_MESSAGE_TEXT_INPUT && (search_focus || note_focus)) {
        char* field=note_focus ? note_edit : query; size_t size=note_focus ? sizeof(note_edit) : sizeof(query),length=strlen(field);
        for (const unsigned char* p=(const unsigned char*)msg->data.text.text;*p && length+1<size;p++) if (*p>=32 && *p!=127) field[length++]=(char)*p;
        field[length]=0; first=selected=0; refresh(); return true;
    }
    if (msg->type==TIG_MESSAGE_MOUSE) {
        int x=msg->data.mouse.x-bounds.x,y=msg->data.mouse.y-bounds.y;
        if (x<0 || y<0 || x>=bounds.width || y>=bounds.height) return !compact;
        if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_WHEEL) {
            if (!compact && x>split()) {
                detail_scroll=SDL_max(0,detail_scroll+(msg->data.mouse.dy>0 ? -1 : 1)*coop_play_line_height()*3);
                log_follow=false; refresh(); return true;
            }
            detail_scroll=0; log_follow=false;
            if (msg->data.mouse.dy>0) selected=selected>0 ? selected-1 : 0;
            else if (selected+1<count) selected++;
            refresh(); return true;
        }
        if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
            for (unsigned i=0;i<button_count;i++) if (inside(buttons[i].rect,x,y)) { activate(buttons[i].id); return true; }
            if (!compact && y>=39 && y<69) {
                unsigned next_tab=(unsigned)SDL_max(0,x-12)/(unsigned)((bounds.width-24)/PLAY_TABS);
                if (next_tab<PLAY_TABS && (player_get_local_pc_obj() || next_tab==PLAY_TAB_SETTINGS)) {
                    tab=next_tab; selected=first=filter=0; *query=0; *feedback=0; search_focus=note_focus=false; text_input(false); refresh();
                }
                return true;
            }
            if (!compact && tab!=PLAY_TAB_SETTINGS && y>=76 && y<=105 && x<bounds.width-260) { search_focus=true; note_focus=false; text_input(true); refresh(); return true; }
            int top=list_top();
            if (y>=top && y<list_bottom() && x<(compact ? bounds.width : split())) {
                unsigned index=first+(unsigned)(y-top)/(unsigned)row_height();
                if (index<count) {
                    selected=index; detail_scroll=0; log_follow=false;
                    if (compact) { activate(10); return true; }
                    if (tab==PLAY_TAB_SETTINGS) change_setting(); refresh();
                }
            }
        }
        return true;
    }
    return false;
}
static bool bar_input(TigMessage* msg)
{
    if (coop_hud_input(HUD_ACTIONS,bar,&bar_rect,msg,bar_rect.width-20,288,2*(coop_play_line_height()+8),false)) return true;
    if (msg->type!=TIG_MESSAGE_MOUSE) return false;
    if (!inside(bar_rect,msg->data.mouse.x,msg->data.mouse.y)) return false;
    if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
        int x=msg->data.mouse.x-bar_rect.x,y=msg->data.mouse.y-bar_rect.y;
        if (x<0 || y<0 || x>=bar_rect.width || y>=bar_rect.height) return false;
        unsigned slot=(unsigned)SDL_min(y/(bar_rect.height/2),1)*6+(unsigned)SDL_min(x/(bar_rect.width/6),5); PlayRow row;
        if (coop_play_action_slot(slot,&row)) coop_play_action_activate(&row);
        else coop_play_ui_open(PLAY_TAB_ACTIONS);
        return true;
    }
    return false;
}
bool coop_play_control_hint(int x,int y,char* text,size_t size)
{
    if (window!=TIG_WINDOW_HANDLE_INVALID && inside(bounds,x,y)) {
        int local_x=x-bounds.x,local_y=y-bounds.y;
        for (unsigned i=0;i<button_count;i++) if (buttons[i].id>=10 && inside(buttons[i].rect,local_x,local_y)) {
            const char* hint=buttons[i].id==10 ? coop_tr("Combat log view\nSwitch between the compact log and expanded tools.")
                : buttons[i].id==11 ? coop_tr("Panel width\nChoose the wide or narrow preset. Drag the corner for a custom size.")
                : buttons[i].id==12 ? coop_tr("Close panel\n[F11], [B] or Escape also closes it.")
                : coop_tr("Transparency\nCycle this panel's opacity. 100% is solid; each panel remembers its own setting.");
            snprintf(text,size,"%s",hint); return true;
        }
    }
    if (log_button!=TIG_WINDOW_HANDLE_INVALID && inside(log_button_rect,x,y)) {
        snprintf(text,size,coop_tr("Combat log: click the journal to show/hide it (F11). Click UI to %s moving/resizing HUD panels. Drag title strips to move; bottom-right grips resize. Each panel has saved transparency in Settings."),coop_hud_editing() ? coop_tr("finish") : coop_tr("start")); return true;
    }
    if (coop_play_ui_blocking()) return false;
    if (bar!=TIG_WINDOW_HANDLE_INVALID && inside(bar_rect,x,y)) {
        unsigned slot=(unsigned)SDL_min((y-bar_rect.y)/(bar_rect.height/2),1)*6+(unsigned)SDL_min((x-bar_rect.x)/(bar_rect.width/6),5);
        PlayRow row;
        if (coop_play_action_slot(slot,&row)) snprintf(text,size,coop_tr("%s\n%.200s\nClick to use. Change assignments in B, Actions or Items."),row.name,row.detail);
        else snprintf(text,size,coop_tr("Empty action slot\nClick to open Actions, then choose an action, select a slot and Assign."));
        return true;
    }
    if (tracker!=TIG_WINDOW_HANDLE_INVALID && inside(tracker_rect,x,y)) {
        snprintf(text,size,coop_tr("Objectives\nClick to read full descriptions, change pins or review recent quest updates.")); return true;
    }
    return false;
}
static bool tracker_input(TigMessage* msg)
{
    if (coop_hud_input(HUD_QUESTS,tracker,&tracker_rect,msg,tracker_rect.width-20,170,60,false)) return true;
    if (msg->type==TIG_MESSAGE_MOUSE && inside(tracker_rect,msg->data.mouse.x,msg->data.mouse.y)) {
        if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) coop_play_ui_open(PLAY_TAB_QUESTS);
        return true;
    }
    return false;
}
static bool log_button_input(TigMessage* msg)
{
    if (msg->type!=TIG_MESSAGE_MOUSE) return false;
    int x=msg->data.mouse.x-log_button_rect.x,y=msg->data.mouse.y-log_button_rect.y;
    if (x>=log_button_rect.width-24 && x<log_button_rect.width && y>=0 && y<log_button_rect.height
        && !(coop_hud_editing() && x>=log_button_rect.width-18 && y>=log_button_rect.height-18)
        && msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) { coop_hud_edit_toggle(); return true; }
    if (coop_hud_input(HUD_LOG_BUTTON,log_button,&log_button_rect,msg,log_button_rect.width-25,64,36,false)) return true;
    if (!inside(log_button_rect,msg->data.mouse.x,msg->data.mouse.y)) return false;
    if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) coop_play_ui_open(PLAY_TAB_LOG);
    return true;
}
void coop_play_ui_tick(uint64_t now)
{
    coop_hud_tick();
    coop_play_bulk_tick();
    if (window!=TIG_WINDOW_HANDLE_INVALID) {
        bool changed=coop_hud_changed(window_element);
        if (changed || (now-refreshed>=750 && (tab!=PLAY_TAB_LOG || refreshed_log_end!=coop_play_event_end()))) refresh();
    }
    IntgameMode mode=intgame_mode_get();
    bool world=player_get_local_pc_obj() && intgame_interface_is_visible() && !mainmenu_ui_is_active() && !coop_lobby_is_open()
        && (mode==INTGAME_MODE_MAIN || mode==INTGAME_MODE_SPELL || mode==INTGAME_MODE_SKILL || mode==INTGAME_MODE_ITEM);
    if (world && restore_log) { restore_log=false; if (coop_hud_log_visible() && window==TIG_WINDOW_HANDLE_INVALID) coop_play_ui_open(PLAY_TAB_LOG); }
    if (!world) { if (log_button!=TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_LOG_BUTTON); tig_window_destroy(log_button); } log_button=TIG_WINDOW_HANDLE_INVALID; }
    else {
        TigRect actions=action_defaults();
        int x=SDL_min(hrp_iso_window_width_get()-80,SDL_max(hrp_iso_window_width_get()/2+410,actions.x+actions.width+12));
        TigRect r=coop_hud_rect(HUD_LOG_BUTTON,(TigRect){x,hrp_iso_window_height_get()-244,74,44},64,36);
        if (log_button!=TIG_WINDOW_HANDLE_INVALID && memcmp(&r,&log_button_rect,sizeof(r))) { coop_hud_detach(HUD_LOG_BUTTON); tig_window_destroy(log_button); log_button=TIG_WINDOW_HANDLE_INVALID; }
        bool created=log_button==TIG_WINDOW_HANDLE_INVALID;
        if (created) { TigWindowData d={.flags=TIG_WINDOW_MESSAGE_FILTER,.rect=r,.message_filter=log_button_input}; if (tig_window_create(&d,&log_button)==TIG_OK) { log_button_rect=r; coop_hud_attach(HUD_LOG_BUTTON,log_button); } }
        static bool was_open,was_editing;
        bool open=window!=TIG_WINDOW_HANDLE_INVALID && tab==PLAY_TAB_LOG;
        if (log_button!=TIG_WINDOW_HANDLE_INVALID && (created || coop_hud_changed(HUD_LOG_BUTTON) || was_open!=open || was_editing!=coop_hud_editing())) {
            box(log_button,(TigRect){0,0,r.width,r.height},open);
            int size=SDL_min(r.height-4,r.width-26);
            coop_hud_icon(log_button,(TigRect){2,(r.height-size)/2,size,size},open);
            TigRect control={r.width-23,(r.height-18)/2,22,20};
            tig_font_push(coop_play_chrome_font(MM_COLOR_GOLD));
            tig_window_text_write(log_button,coop_hud_editing() ? coop_tr("OK") : coop_tr("UI"),&control); tig_font_pop();
            coop_hud_handles(HUD_LOG_BUTTON,log_button,&r,r.width-25,false);
            was_open=open; was_editing=coop_hud_editing();
        }
    }
    if (!world || !coop_play_pref(PLAY_PREF_BAR)) { if (bar!=TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_ACTIONS); tig_window_destroy(bar); } bar=TIG_WINDOW_HANDLE_INVALID; }
    else {
        TigRect r=coop_hud_rect(HUD_ACTIONS,action_defaults(),288,2*(coop_play_line_height()+8));
        if (bar!=TIG_WINDOW_HANDLE_INVALID && memcmp(&r,&bar_rect,sizeof(r))) { coop_hud_detach(HUD_ACTIONS); tig_window_destroy(bar); bar=TIG_WINDOW_HANDLE_INVALID; }
        bool created=bar==TIG_WINDOW_HANDLE_INVALID;
        if (created) { TigWindowData d={.flags=TIG_WINDOW_MESSAGE_FILTER,.rect=r,.message_filter=bar_input}; if (tig_window_create(&d,&bar)==TIG_OK) { bar_rect=r; coop_hud_attach(HUD_ACTIONS,bar); } }
        bool changed=coop_hud_changed(HUD_ACTIONS);
        if (bar!=TIG_WINDOW_HANDLE_INVALID && (created || changed || now-aux_refreshed>=300)) {
          static char cached[PLAY_EXTRA_SLOTS][180]; static bool available[PLAY_EXTRA_SLOTS];
          static int kinds[PLAY_EXTRA_SLOTS],ids[PLAY_EXTRA_SLOTS]; static int64_t objects[PLAY_EXTRA_SLOTS];
          for (unsigned i=0;i<PLAY_EXTRA_SLOTS;i++) {
            PlayRow row; bool present=coop_play_action_slot(i,&row); int width=r.width/6; TigRect slot={(int)(i%6)*width,(int)(i/6)*(r.height/2),width-2,r.height/2-2};
            char value[180];
            if (present) snprintf(value,sizeof(value),"%u  %.100s",i+1,row.name);
            else snprintf(value,sizeof(value),"%u  +",i+1);
            bool usable=present && row.available;
            if (!created && !changed && !strcmp(cached[i],value) && available[i]==usable
                && kinds[i]==(present ? row.kind : 0) && ids[i]==(present ? row.id : 0) && objects[i]==(present ? row.object : 0)) continue;
            coop_hud_surface(bar,HUD_ART_ACTION_SLOT,slot,false);
            int icon=SDL_min(slot.height-6,24);
            bool icon_only=!present || slot.width<112;
            int icon_x=icon_only ? slot.x+slot.width-icon-8 : slot.x+4;
            bool pictured=present && coop_hud_action_icon(bar,&row,(TigRect){icon_x,slot.y+(slot.height-icon)/2,icon,icon});
            if (icon_only) {
                char number[8]; snprintf(number,sizeof(number),"%u",i+1);
                TigRect caption={slot.x+4,slot.y+3,20,18};
                tig_font_push(coop_play_chrome_font(MM_COLOR_BROWN)); tig_window_text_write(bar,number,&caption); tig_font_pop();
                if (!pictured) {
                    caption=(TigRect){slot.x+(slot.width-10)/2,slot.y+(slot.height-18)/2,12,18};
                    tig_font_push(coop_play_chrome_font(MM_COLOR_BROWN)); tig_window_text_write(bar,present ? "?" : "+",&caption); tig_font_pop();
                }
            } else {
                int inset=pictured ? icon+9 : 7;
                slot.x+=inset; slot.y+=3; slot.width-=inset+5; slot.height-=6;
                char caption[180]; snprintf(caption,sizeof(caption),"%s",value);
                coop_hud_text_fit(caption,sizeof(caption),slot.width,coop_play_font(MM_COLOR_WHITE));
                write_at(bar,caption,slot,present ? usable ? MM_COLOR_WHITE : MM_COLOR_GRAY : MM_COLOR_BROWN);
            }
            snprintf(cached[i],sizeof(cached[i]),"%s",value); available[i]=usable;
            kinds[i]=present ? row.kind : 0; ids[i]=present ? row.id : 0; objects[i]=present ? row.object : 0;
          }
          coop_hud_handles(HUD_ACTIONS,bar,&r,r.width-20,false);
        }
    }
    unsigned pins=0; for (unsigned i=0;i<5;i++) if (coop_play_pin_at(i)) pins++;
    if (!world || (!pins && !*coop_play_note())) { if (tracker!=TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_QUESTS); tig_window_destroy(tracker); } tracker=TIG_WINDOW_HANDLE_INVALID; }
    else {
        int margin=combat_turn_based_is_active() ? 194 : 166;
        int line=coop_play_line_height(),maximum=hrp_iso_window_height_get()-margin-122-(coop_play_pref(PLAY_PREF_BAR) ? 2*(line+8)+4 : 0);
        int lines=(int)pins*(line*2+6)+(*coop_play_note() ? line*2+6 : 0)+line+24>maximum ? 1 : 2;
        int height=(int)pins*(line*lines+6)+(*coop_play_note() ? line*lines+6 : 0)+line+24;
        if (!pins && *coop_play_note()) height=coop_hud_card_height(coop_tr("Objectives"),coop_play_note(),270);
        TigRect r=coop_hud_rect(HUD_QUESTS,(TigRect){hrp_iso_window_width_get()-278,122,270,SDL_min(maximum,height)},170,60);
        unsigned sections=pins+(*coop_play_note()!=0);
        if (sections) lines=SDL_clamp(((r.height-line-24)/(int)sections-6)/line,1,4);
        if (!pins) lines=4;
        if (tracker!=TIG_WINDOW_HANDLE_INVALID && memcmp(&r,&tracker_rect,sizeof(r))) { coop_hud_detach(HUD_QUESTS); tig_window_destroy(tracker); tracker=TIG_WINDOW_HANDLE_INVALID; }
        bool created=tracker==TIG_WINDOW_HANDLE_INVALID;
        if (created) { TigWindowData d={.flags=TIG_WINDOW_MESSAGE_FILTER,.rect=r,.message_filter=tracker_input}; if (tig_window_create(&d,&tracker)==TIG_OK) { tracker_rect=r; coop_hud_attach(HUD_QUESTS,tracker); } }
        if (tracker!=TIG_WINDOW_HANDLE_INVALID && (created || coop_hud_changed(HUD_QUESTS) || now-aux_refreshed>=300)) {
            box(tracker,(TigRect){0,0,r.width,r.height},false); write_at(tracker,coop_tr("Objectives"),(TigRect){12,10,r.width-24,line+2},MM_COLOR_GOLD);
            int heading=coop_hud_text_height(coop_tr("Objectives"),r.width-24),y=heading+21;
            TigLine rule={12,heading+14,r.width-13,heading+14}; tig_window_line(tracker,&rule,tig_color_make(91,73,46));
            for (unsigned i=0;i<5;i++) { unsigned id=coop_play_pin_at(i); if (!id || y>=r.height-8) continue; char value[600]={0}; quest_copy_description(player_get_local_pc_obj(),(int)id,value,sizeof(value)); write_at(tracker,value,(TigRect){12,y,r.width-24,SDL_min(line*lines,r.height-y-8)},MM_COLOR_WHITE); y+=line*lines+6; }
            if (*coop_play_note() && y<r.height-8) write_at(tracker,coop_play_note(),(TigRect){12,y,r.width-24,SDL_min(line*lines,r.height-y-8)},MM_COLOR_WHITE);
            coop_hud_handles(HUD_QUESTS,tracker,&r,r.width-20,false);
        }
    }
    if (now-aux_refreshed>=300) aux_refreshed=now;
}
static bool settings_link_input(TigMessage* msg)
{
    if (msg->type==TIG_MESSAGE_MOUSE && inside(settings_link_rect,msg->data.mouse.x,msg->data.mouse.y)) {
        if (msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) coop_play_ui_open(PLAY_TAB_SETTINGS);
        return true;
    }
    return false;
}
void coop_play_settings_link(bool show)
{
    if (settings_link!=TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(settings_link);
    settings_link=TIG_WINDOW_HANDLE_INVALID;
    if (!show) { if (mainmenu_ui_is_active()) coop_play_ui_close(); return; }
    TigRect r={(hrp_iso_window_width_get()-560)/2,hrp_iso_window_height_get()/2+137,560,34};
    TigWindowData d={.flags=TIG_WINDOW_MESSAGE_FILTER|TIG_WINDOW_ALWAYS_ON_TOP,.rect=r,.message_filter=settings_link_input,.background_color=tig_color_make(22,20,16)};
    if (tig_window_create(&d,&settings_link)!=TIG_OK) { settings_link=TIG_WINDOW_HANDLE_INVALID; return; }
    settings_link_rect=r;
    box(settings_link,(TigRect){0,0,r.width,r.height},true);
    write_at(settings_link,coop_tr("Playability settings: text, controls, combat and labels"),(TigRect){8,5,r.width-16,28},MM_COLOR_GOLD);
}
