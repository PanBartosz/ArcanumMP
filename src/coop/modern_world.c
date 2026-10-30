#include "coop/modern.h"
#include "coop/hud.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/display.h"
#include "coop/lobby_ui.h"
#include "coop/party_ui.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/ai.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/object.h"
#include "game/obj_pool.h"
#include "game/path.h"
#include "game/player.h"
#include "ui/intgame.h"
#include "ui/spell_ui.h"
#include "ui/logbook_ui.h"
#include "ui/textedit_ui.h"
#include "ui/wmap_ui.h"
#include <stdio.h>
#include <string.h>

typedef struct Label { int64_t obj; TigRect rect; } Label;
static Label labels[96];
static unsigned label_count;
static bool labels_on, enemy_visible, enemy_before;
static int64_t seen_enemies[128];
static unsigned seen_enemy_count;
static TigRect erase[256];
static unsigned erase_count;
static bool collect_labels,collect_enemies;
static TigRect path_bounds;
static int observed_map=-1;
static uint64_t last_scan;
typedef struct Watch { int64_t actor, weapon; int hp,weapon_hp; unsigned status; } Watch;
static Watch watches[32];
static tig_window_handle_t banner=TIG_WINDOW_HANDLE_INVALID;
static TigRect banner_rect;
static tig_window_handle_t preview_window=TIG_WINDOW_HANDLE_INVALID;
static TigRect preview_rect;
static char preview_text[384],banner_text[256];
static int preview_min_height=68,banner_min_height=72;
static TigRect card_bounds(unsigned element,TigRect fallback,int min_width,const char* title,const char* body,int* minimum)
{
    TigRect r=coop_hud_rect(element,fallback,min_width,64);
    *minimum=SDL_min(coop_hud_card_height(title,body,r.width),hrp_iso_window_height_get()-4);
    if (r.height<*minimum) { r.y=SDL_max(2,r.y+r.height-*minimum); r.height=*minimum; }
    return r;
}
static void preview_close(void)
{
    if (preview_window!=TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_PREVIEW); tig_window_destroy(preview_window); }
    preview_window=TIG_WINDOW_HANDLE_INVALID; *preview_text=0;
}
static bool play_enemy(int64_t actor)
{
    return actor && obj_handle_is_valid(actor) && obj_field_int32_get(actor,OBJ_F_TYPE)==OBJ_TYPE_NPC
        && !critter_is_dead(actor) && object_relation_color(actor)==tig_color_make(245,55,45);
}
static bool notice_enemy(int64_t actor)
{
    for (unsigned i=0;i<SDL_min(seen_enemy_count,SDL_arraysize(seen_enemies));i++) if (seen_enemies[i]==actor) return false;
    seen_enemies[seen_enemy_count++%SDL_arraysize(seen_enemies)]=actor;
    return true;
}
void coop_play_world_reset(void)
{
    preview_close(); coop_hud_detach(HUD_PAUSE);
    if (banner!=TIG_WINDOW_HANDLE_INVALID) tig_window_destroy(banner);
    banner=TIG_WINDOW_HANDLE_INVALID; observed_map=-1; last_scan=0; labels_on=false; label_count=0;
    enemy_visible=enemy_before=false; memset(watches,0,sizeof(watches));
    seen_enemy_count=erase_count=0; collect_labels=collect_enemies=false;
}
static bool in_world(void)
{
    IntgameMode mode=intgame_mode_get();
    return player_get_local_pc_obj() && intgame_interface_is_visible() && !mainmenu_ui_is_active() && !coop_lobby_is_open()
        && (mode==INTGAME_MODE_MAIN || mode==INTGAME_MODE_SPELL || mode==INTGAME_MODE_SKILL || mode==INTGAME_MODE_ITEM || mode==INTGAME_MODE_FOLLOWER);
}
static void draw_text(tig_window_handle_t window,const char* value,TigRect rect,MainMenuColor color)
{
    tig_font_push(coop_play_font(color)); tig_window_text_write(window,value,&rect); tig_font_pop();
    if (window!=banner && window!=preview_window && erase_count<SDL_arraysize(erase)) erase[erase_count++]=rect;
}
static void pause_for(const char* reason)
{
    if (coop_play_paused() || coop_runtime_paused()) return;
    coop_play_pause_toggle();
    coop_play_pause_announce(reason);
}
static bool banner_input(TigMessage* msg)
{
    if (coop_hud_input(HUD_PAUSE,banner,&banner_rect,msg,banner_rect.width-20,280,banner_min_height,false)) return true;
    if (msg->type==TIG_MESSAGE_MOUSE && msg->data.mouse.x>=banner_rect.x && msg->data.mouse.x<banner_rect.x+banner_rect.width
        && msg->data.mouse.y>=banner_rect.y && msg->data.mouse.y<banner_rect.y+banner_rect.height
        && msg->data.mouse.event==TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN && !msg->data.mouse.repeat) {
        CoopSession* session=coop_runtime_session();
        const CoopControlView* state=coop_runtime_controls() ? coop_controls_current(coop_runtime_controls()) : NULL;
        if (session && state && !(state->paused&(1u<<coop_session_local_slot(session)))) return true;
        coop_play_pause_toggle(); return true;
    }
    return false;
}
void coop_play_tick(uint64_t now)
{
    coop_play_ui_tick(now);
    wmap_ui_play_tick(now);
    int64_t pc=player_get_local_pc_obj();
    if (!pc || !obj_handle_is_valid(pc)) { coop_play_world_reset(); return; }
    if (observed_map!=map_current_map()) {
        observed_map=map_current_map(); memset(watches,0,sizeof(watches)); enemy_before=enemy_visible=false; seen_enemy_count=0; last_scan=0;
    }
    if (!in_world() || !combat_turn_based_is_active() || !coop_play_pref(PLAY_PREF_PREVIEW) || coop_play_ui_blocking()) preview_close();
    if (coop_authority_can_simulate() && now-last_scan>=150) {
        unsigned policy=coop_play_pause_policy();
        if ((policy&PLAY_PAUSE_ENEMY) && enemy_visible) pause_for("Enemy spotted");
        enemy_visible=false;
        ObjectList party; object_list_party(pc,&party);
        unsigned i=0;
        for (ObjectNode* node=party.head;node && i<SDL_arraysize(watches);node=node->next,i++) {
            int64_t actor=node->obj; Watch* old=&watches[i];
            if (policy&PLAY_PAUSE_ENEMY) {
                ObjectList vicinity; object_list_vicinity(actor,OBJ_TM_NPC,&vicinity);
                for (ObjectNode* enemy=vicinity.head;enemy;enemy=enemy->next)
                    if (ai_is_fighting(enemy->obj) && play_enemy(enemy->obj) && notice_enemy(enemy->obj))
                        pause_for("Enemy engaged the party");
                object_list_destroy(&vicinity);
            }
            unsigned status=coop_party_ui_statuses(actor);
            int hp=object_hp_current(actor),maximum=object_hp_max(actor);
            int64_t weapon=item_wield_get(actor,ITEM_INV_LOC_WEAPON);
            if (old->actor==actor) {
                char name[96],why[192]; coop_play_name(actor,name,sizeof(name));
                if ((policy&PLAY_PAUSE_DOWN) && (status&(COOP_STATUS_DEAD|COOP_STATUS_UNCONSCIOUS)) && !(old->status&(COOP_STATUS_DEAD|COOP_STATUS_UNCONSCIOUS))) {
                    snprintf(why,sizeof(why),"%s is incapacitated",name); pause_for(why);
                } else if ((policy&PLAY_PAUSE_CRITICAL) && maximum>0 && hp>0 && (int64_t)hp*4<=maximum && (int64_t)old->hp*4>maximum) {
                    snprintf(why,sizeof(why),"%s has critical health",name); pause_for(why);
                }
                if ((policy&PLAY_PAUSE_BREAK) && old->weapon && old->weapon_hp>0 && obj_handle_is_valid(old->weapon) && object_hp_current(old->weapon)<=0) {
                    snprintf(why,sizeof(why),"%s's weapon broke",name); pause_for(why);
                }
                unsigned changed=status^old->status;
                if (changed) {
                    static const char* names[]={"Dead","Unconscious","Poisoned","Low fatigue","Stunned","Paralyzed","Blinded","Injured","Encumbered"};
                    for (unsigned bit=0;bit<SDL_arraysize(names);bit++) if (changed&(1u<<bit)) {
                        snprintf(why,sizeof(why),"%s: %s %s",name,names[bit],status&(1u<<bit) ? "applied" : "ended");
                        coop_play_event(PLAY_EFFECT,actor,0,why,"Party status changed in the native simulation.");
                    }
                }
            }
            *old=(Watch){.actor=actor,.weapon=weapon,.hp=hp,.weapon_hp=weapon ? object_hp_current(weapon) : 0,.status=status};
        }
        object_list_destroy(&party); last_scan=now;
    }
    bool paused=coop_play_paused() || coop_runtime_paused();
    if (!paused || !in_world()) {
        if (banner!=TIG_WINDOW_HANDLE_INVALID) { coop_hud_detach(HUD_PAUSE); tig_window_destroy(banner); }
        banner=TIG_WINDOW_HANDLE_INVALID; return;
    }
    CoopSession* session=coop_runtime_session();
    const CoopControlView* state=coop_runtime_controls() ? coop_controls_current(coop_runtime_controls()) : NULL;
    bool mine=!session || !state || (state->paused&(1u<<coop_session_local_slot(session)));
    char text[256]; snprintf(text,sizeof(text),"%s%s%s",coop_play_pause_reason(),*coop_play_pause_reason() ? "\n" : "",
        mine ? "Click to resume" : "Waiting for the pausing player");
    int width=SDL_min(380,hrp_iso_window_width_get()-208),height=coop_hud_card_height("Paused",text,width);
    TigRect r=card_bounds(HUD_PAUSE,(TigRect){(hrp_iso_window_width_get()-width)/2,122,width,height},280,"Paused",text,&banner_min_height);
    if (banner!=TIG_WINDOW_HANDLE_INVALID && memcmp(&r,&banner_rect,sizeof(r))) { coop_hud_detach(HUD_PAUSE); tig_window_destroy(banner); banner=TIG_WINDOW_HANDLE_INVALID; }
    if (banner==TIG_WINDOW_HANDLE_INVALID) {
        TigWindowData data={.flags=TIG_WINDOW_MESSAGE_FILTER,.rect=r,.background_color=tig_color_make(16,15,12),.message_filter=banner_input};
        if (tig_window_create(&data,&banner)!=TIG_OK) { banner=TIG_WINDOW_HANDLE_INVALID; return; }
        banner_rect=r; *banner_text=0; coop_hud_attach(HUD_PAUSE,banner);
    }
    if (coop_hud_changed(HUD_PAUSE) || strcmp(text,banner_text)) {
        coop_hud_card(banner,(TigRect){0,0,r.width,r.height},"Paused",text,true);
        coop_hud_handles(HUD_PAUSE,banner,&r,r.width-20,false); snprintf(banner_text,sizeof(banner_text),"%s",text);
    }
}
void coop_play_draw_begin(void)
{
    label_count=0;
    collect_labels=labels_on && in_world() && !coop_play_ui_blocking();
    collect_enemies=in_world() && coop_authority_can_simulate() && (coop_play_pause_policy()&PLAY_PAUSE_ENEMY);
    for (unsigned i=0;i<erase_count;i++) {
        TigRect r=erase[i]; r.x-=2; r.y-=2;
        int x=r.x+r.width+4,y=r.y+r.height+4;
        coop_view_point(&r.x,&r.y); coop_view_point(&x,&y); r.width=x-r.x+2; r.height=y-r.y+2;
        gamelib_invalidate_rect(&r);
    }
    erase_count=0;
    /* Restore only the previous overlays' rectangles. Full-world invalidation
     * here used to redraw every tile and NPC every frame in turn-based play. */
}
void coop_play_label_object(int64_t obj,const TigRect* rect)
{
    if (!obj || (!collect_labels && !collect_enemies)) return;
    int type=obj_field_int32_get(obj,OBJ_F_TYPE);
    unsigned flags=(unsigned)obj_field_int32_get(obj,OBJ_F_FLAGS);
    if (flags&(OF_OFF|OF_DESTROYED|OF_EXTINCT|OF_DONTDRAW|OF_INVENTORY)) return;
    if (collect_enemies && obj_type_is_critter(type) && play_enemy(obj)) {
        if (notice_enemy(obj)) enemy_visible=true;
    }
    if (!collect_labels || label_count>=SDL_arraysize(labels) || (flags&OF_CLICK_THROUGH)) return;
    bool corpse=obj_type_is_critter(type) && critter_is_dead(obj);
    if (!corpse && !obj_type_is_item(type) && type!=OBJ_TYPE_PORTAL && type!=OBJ_TYPE_CONTAINER) return;
    if (coop_play_pref(PLAY_PREF_EMPTY) && (corpse || type==OBJ_TYPE_CONTAINER)) {
        int count=obj_field_int32_get(obj,type==OBJ_TYPE_CONTAINER ? OBJ_F_CONTAINER_INVENTORY_NUM : OBJ_F_CRITTER_INVENTORY_NUM);
        if (!count) return;
    }
    for (unsigned i=0;i<label_count;i++) if (labels[i].obj==obj) return;
    TigRect native_rect;
    if (!rect) { object_get_rect(obj,0,&native_rect); rect=&native_rect; }
    labels[label_count++]=(Label){obj,*rect};
}
static void line(tig_window_handle_t window,int x1,int y1,int x2,int y2,tig_color_t color)
{ TigLine l={x1,y1,x2,y2}; tig_window_line(window,&l,color); }
static void preview_line(tig_window_handle_t window,int x1,int y1,int x2,int y2,tig_color_t color)
{
    TigRect r={SDL_min(x1,x2)-2,SDL_min(y1,y2)-2,SDL_abs(x2-x1)+5,SDL_abs(y2-y1)+5};
    if (!path_bounds.width) path_bounds=r;
    else {
        int right=SDL_max(path_bounds.x+path_bounds.width,r.x+r.width),bottom=SDL_max(path_bounds.y+path_bounds.height,r.y+r.height);
        path_bounds.x=SDL_min(path_bounds.x,r.x); path_bounds.y=SDL_min(path_bounds.y,r.y);
        path_bounds.width=right-path_bounds.x; path_bounds.height=bottom-path_bounds.y;
    }
    line(window,x1,y1,x2,y2,color);
}
static bool preview_input(TigMessage* msg)
{
    if (coop_hud_input(HUD_PREVIEW,preview_window,&preview_rect,msg,preview_rect.width-20,260,preview_min_height,false)) return true;
    return msg->type==TIG_MESSAGE_MOUSE && msg->data.mouse.x>=preview_rect.x && msg->data.mouse.x<preview_rect.x+preview_rect.width
        && msg->data.mouse.y>=preview_rect.y && msg->data.mouse.y<preview_rect.y+preview_rect.height;
}
static void preview_show(const char* value)
{
    static int last_width,last_height,last_line,last_bar;
    int screen_width=hrp_iso_window_width_get(),screen_height=hrp_iso_window_height_get();
    int line_height=coop_play_line_height(),bar_enabled=coop_play_pref(PLAY_PREF_BAR);
    bool changed=coop_hud_changed(HUD_PREVIEW);
    if (preview_window!=TIG_WINDOW_HANDLE_INVALID && !changed && !strcmp(value,preview_text)
        && last_width==screen_width && last_height==screen_height && last_line==line_height && last_bar==bar_enabled) return;
    last_width=screen_width; last_height=screen_height; last_line=line_height; last_bar=bar_enabled;
    char title[128]="Combat preview",body[384];
    const char* start=value;
    const char* separator=strstr(value,": ");
    if (!separator || separator-value>96) separator=strstr(value,". ");
    if (separator && separator-value<=96) {
        snprintf(title,sizeof(title),"%.*s",(int)(separator-value),value); start=separator+2;
    }
    size_t used=0;
    while (*start && used+1<sizeof(body)) {
        if (!strncmp(start," | ",3)) { body[used++]='\n'; start+=3; }
        else body[used++]=*start++;
    }
    body[used]=0;
    int width=SDL_clamp((hrp_iso_window_width_get()-224)/2,280,336+(coop_play_pref(PLAY_PREF_TEXT)-100)/2);
    int height=coop_hud_card_height(title,body,width);
    int bar_height=coop_play_pref(PLAY_PREF_BAR) ? 2*(coop_play_line_height()+8) : 0;
    TigRect r=card_bounds(HUD_PREVIEW,(TigRect){104,hrp_iso_window_height_get()-194-height-bar_height-8,width,height},260,title,body,&preview_min_height);
    if (preview_window!=TIG_WINDOW_HANDLE_INVALID && memcmp(&r,&preview_rect,sizeof(r))) preview_close();
    if (preview_window==TIG_WINDOW_HANDLE_INVALID) {
        TigWindowData data={.flags=TIG_WINDOW_MESSAGE_FILTER,.rect=r,.message_filter=preview_input};
        if (tig_window_create(&data,&preview_window)!=TIG_OK) { preview_window=TIG_WINDOW_HANDLE_INVALID; return; }
        preview_rect=r; coop_hud_attach(HUD_PREVIEW,preview_window); *preview_text=0;
    }
    if (changed || strcmp(value,preview_text)) {
        coop_hud_card(preview_window,(TigRect){0,0,r.width,r.height},title,body,false);
        coop_hud_handles(HUD_PREVIEW,preview_window,&r,r.width-20,false);
        snprintf(preview_text,sizeof(preview_text),"%s",value);
    }
}
static void point(int64_t loc,int* x,int* y)
{
    int64_t px,py; location_xy(loc,&px,&py); *x=(int)px+40; *y=(int)py+20; coop_view_screen_point(x,y);
}
static void marker(tig_window_handle_t window,int64_t loc,const char* text,MainMenuColor color)
{
    int x,y; point(loc,&x,&y);
    if (x<90 || x>hrp_iso_window_width_get()-90 || y<112 || y>hrp_iso_window_height_get()-170) return;
    tig_color_t gold=tig_color_make(255,220,100);
    if (erase_count<SDL_arraysize(erase)) erase[erase_count++]=(TigRect){x-14,y-9,29,19};
    line(window,x-12,y,x,y-7,gold); line(window,x,y-7,x+12,y,gold);
    line(window,x+12,y,x,y+7,gold); line(window,x,y+7,x-12,y,gold);
    draw_text(window,text,(TigRect){x-100,y+9,200,coop_play_line_height()},color);
}
static void preview(tig_window_handle_t window)
{
    if (!coop_play_pref(PLAY_PREF_PREVIEW) || !combat_turn_based_is_active()) return;
    int64_t pc=player_get_local_pc_obj(); TigMouseState mouse;
    const char* waiting="Waiting for your turn. Your portrait will be highlighted when you can act.";
    if (combat_turn_based_whos_turn_get()!=pc) { preview_show(waiting); return; }
    if (tig_mouse_get_state(&mouse)!=TIG_OK) return;
    tig_window_handle_t top;
    if (tig_window_get_at_position(mouse.x,mouse.y,&top)!=TIG_OK || top!=window) {
        if (!strcmp(preview_text,waiting)) preview_close();
        return;
    }
    int64_t loc; if (!coop_view_location_at(mouse.x,mouse.y,&loc)) return;
    int64_t from=obj_field_int64_get(pc,OBJ_F_LOCATION),target=object_hover_obj_get();
    int mode=intgame_mode_get(),spell=spell_ui_selected(),ap=combat_action_points_get();
    static int64_t cached_from,cached_loc,cached_target,path[128],area_center;
    static int cached_mode=-1,cached_spell=-1,cached_ap=-1,radius;
    static unsigned path_count;
    static uint64_t calculated;
    static char text[384];
    uint64_t now=SDL_GetTicks();
    if (from!=cached_from || loc!=cached_loc || target!=cached_target || mode!=cached_mode || spell!=cached_spell || ap!=cached_ap || now-calculated>=250) {
        cached_from=from; cached_loc=loc; cached_target=target; cached_mode=mode; cached_spell=spell; cached_ap=ap; calculated=now;
        path_count=0; radius=0;
        if (combat_turn_based_whos_turn_get()!=pc) snprintf(text,sizeof(text),"Waiting for your turn. Your portrait will be highlighted when you can act.");
        else {
            int cost=0; bool clear=true,check_sight=false,walking=false; const char* verb="Move";
            int64_t destination=loc;
            if (play_enemy(target) && mode==INTGAME_MODE_MAIN) {
                destination=obj_field_int64_get(target,OBJ_F_LOCATION); verb="Attack";
                int64_t weapon=item_wield_get(pc,ITEM_INV_LOC_WEAPON); int range=weapon ? item_weapon_range(weapon,pc) : 1;
                cost=combat_attack_cost(pc); check_sight=range>1;
                if (range<=1 && location_dist(from,destination)>1) cost+=combat_move_cost(pc,destination,true);
                clear=range<=1 || location_dist(from,destination)<=range;
                if (!clear) snprintf(text,sizeof(text),"Out of weapon range (%d tiles). Move closer before attacking.",range);
            } else if (mode==INTGAME_MODE_SPELL && spell>=0 && spell<MT_SPELL_COUNT) {
                verb="Cast"; cost=4; check_sight=true;
                int range=magictech_get_range(spell),fatigue=magictech_get_cost(spell);
                if (range>0 && location_dist(from,destination)>range) { clear=false; snprintf(text,sizeof(text),"Spell out of range (%d tiles). Cost: %d fatigue.",range,fatigue); }
                else if (!magictech_can_charge_spell_fatigue(pc,spell)) { clear=false; snprintf(text,sizeof(text),"Insufficient fatigue for %s (%d required).",magictech_spell_name(spell),fatigue); }
                radius=magictech_spells[spell].target_params[MAGICTECH_ACTION_BEGIN].radius;
                if (radius<0 || radius>30) radius=0;
                area_center=destination;
            } else { cost=combat_move_cost(pc,destination,false); walking=true; }
            if (clear && check_sight) {
                int8_t dirs[512]; int steps=path_make_straight(from,destination,dirs);
                if (steps>0 && steps<512) {
                    int64_t current=from;
                    for (int i=0;i<steps-1;i++) {
                        bool is_window=false;
                        if (object_traversal_is_blocked(pc,current,dirs[i],OBJ_TRAVERSAL_PROJECTILE|OBJ_TRAVERSAL_IGNORE_CRITTERS,&is_window)) {
                            clear=false; snprintf(text,sizeof(text),"Line of sight is blocked. Reposition before using this action."); break;
                        }
                        if (!location_in_dir(current,dirs[i],&current)) break;
                    }
                }
            }
            if (clear && walking && from!=destination) {
                int8_t dirs[128]; PathCreateInfo info={.obj=pc,.from=from,.to=destination,.max_rotations=128,.rotations=dirs};
                int steps=path_make(&info); int64_t current=from;
                if (steps<=0) { clear=false; snprintf(text,sizeof(text),"No reachable path to this tile."); }
                else for (int i=0;i<steps && i<128;i++) {
                    if (!location_in_dir(current,dirs[i],&current)) break;
                    path[path_count++]=current;
                }
            }
            if (clear) {
                if (cost<0 || cost>1000) snprintf(text,sizeof(text),"No reachable path to this tile.");
                else snprintf(text,sizeof(text),"%s: %d AP | %d AP remaining%s",verb,cost,SDL_max(0,ap-cost),cost>ap ? " | Excess AP uses fatigue" : "");
                if (radius && magictech_is_aggressive(spell)) {
                    ObjectList party; object_list_party(pc,&party); bool friendly=false;
                    for (ObjectNode* n=party.head;n;n=n->next) if (location_dist(obj_field_int64_get(n->obj,OBJ_F_LOCATION),area_center)<=radius) friendly=true;
                    object_list_destroy(&party);
                    if (friendly) { size_t length=strlen(text); snprintf(text+length,sizeof(text)-length," | Ally in spell area"); }
                }
            }
        }
    }
    int64_t previous=from;
    path_bounds=(TigRect){0};
    for (unsigned i=0;i<path_count;i++) {
        int x1,y1,x2,y2; point(previous,&x1,&y1); point(path[i],&x2,&y2);
        preview_line(window,x1,y1,x2,y2,tig_color_make(110,220,160)); previous=path[i];
    }
    if (radius) {
        int64_t mx,my; location_limits_get(&mx,&my);
        int64_t x=location_get_x(area_center),y=location_get_y(area_center);
        int64_t left=SDL_max(0,x-radius),right=SDL_min(mx-1,x+radius),top=SDL_max(0,y-radius),bottom=SDL_min(my-1,y+radius);
        int64_t around[]={location_make(left,top),location_make(right,top),location_make(right,bottom),location_make(left,bottom)};
        for (unsigned i=0;i<4;i++) { int x1,y1,x2,y2; point(around[i],&x1,&y1); point(around[(i+1)%4],&x2,&y2); preview_line(window,x1,y1,x2,y2,tig_color_make(235,180,70)); }
    }
    if (path_bounds.width && erase_count<SDL_arraysize(erase)) erase[erase_count++]=path_bounds;
    preview_show(text);
}

void coop_play_draw(tig_window_handle_t window)
{
    if (!in_world() || coop_play_ui_blocking()) return;
    TigRect placed[96]; unsigned count=0;
    for (unsigned i=0;i<label_count;i++) {
        int64_t obj=labels[i].obj; if (!obj_handle_is_valid(obj)) continue;
        int type=obj_field_int32_get(obj,OBJ_F_TYPE);
        char name[128],text[180]; coop_play_name(obj,name,sizeof(name));
        const char* category=type==OBJ_TYPE_PORTAL ? "Door" : type==OBJ_TYPE_CONTAINER ? "Container" : obj_type_is_critter(type) ? "Body" : "Loot";
        snprintf(text,sizeof(text),"[%s] %s",category,name);
        int x=labels[i].rect.x+labels[i].rect.width/2,y=labels[i].rect.y+labels[i].rect.height;
        coop_view_screen_point(&x,&y);
        tig_font_push(coop_play_font(MM_COLOR_GOLD)); TigFont measure={.str=text}; tig_font_measure(&measure); tig_font_pop();
        TigRect r={SDL_max(90,x-SDL_min(measure.width+10,260)/2),y,SDL_min(measure.width+10,260),coop_play_line_height()+4};
        if (r.x+r.width>hrp_iso_window_width_get()-90) r.x=hrp_iso_window_width_get()-90-r.width;
        for (unsigned j=0;j<count;j++) {
            TigRect overlap;
            if (tig_rect_intersection(&r,&placed[j],&overlap)==TIG_OK) { r.y=placed[j].y+placed[j].height+2; j=(unsigned)-1; }
        }
        if (r.y<114 || r.y+r.height>hrp_iso_window_height_get()-164) continue;
        placed[count++]=r;
        if (erase_count<SDL_arraysize(erase)) erase[erase_count++]=r;
        tig_window_fill(window,&r,tig_color_make(17,16,12)); tig_window_box(window,&r,tig_color_make(140,120,75));
        r.x+=5; r.y+=2; r.width-=10; draw_text(window,text,r,type==OBJ_TYPE_PORTAL ? MM_COLOR_LIGHTBLUE : MM_COLOR_GOLD);
    }
    uint32_t end=coop_play_event_end(); unsigned shown=0;
    for (uint32_t n=end;n>coop_play_event_first() && shown<4;n--) {
        const PlayEvent* e=coop_play_event_at(n-1);
        if (e && e->kind==PLAY_PING && e->map==(uint32_t)map_current_map() && (uint32_t)(SDL_GetTicks()-e->time)<10000) {
            marker(window,location_make(e->x,e->y),e->summary,MM_COLOR_GOLD); shown++;
        }
    }
    int64_t pc=player_get_local_pc_obj();
    for (unsigned i=coop_campaign_next_slot(0);i<4;i=coop_campaign_next_slot(i+1)) {
        int64_t actor=coop_campaign_object(i); if (!actor || actor==pc) continue;
        int x,y; point(obj_field_int64_get(actor,OBJ_F_LOCATION),&x,&y);
        int width=hrp_iso_window_width_get(),height=hrp_iso_window_height_get();
        if (x<90 || x>width-90 || y<115 || y>height-175) {
            int cx=SDL_clamp(x,95,width-200),cy=SDL_clamp(y,120,height-195); char name[80],text[128]; coop_play_name(actor,name,sizeof(name));
            snprintf(text,sizeof(text),"%s %s %.65s (%lld)",x<90 ? "<" : x>width-90 ? ">" : "",y<115 ? "^" : y>height-175 ? "v" : "",name,(long long)object_dist(pc,actor));
            draw_text(window,text,(TigRect){cx,cy,190,coop_play_line_height()},MM_COLOR_GREEN);
        }
    }
    if (coop_play_pref(PLAY_PREF_SYMBOLS)) {
        int64_t hover=object_hover_obj_get();
        if (hover && obj_handle_is_valid(hover) && obj_type_is_critter(obj_field_int32_get(hover,OBJ_F_TYPE))) {
            const char* relation=critter_party_same(pc,hover) ? "[+] Ally" : play_enemy(hover) ? "[!] Enemy" : "[=] Neutral";
            TigMouseState mouse; if (tig_mouse_get_state(&mouse)==TIG_OK) draw_text(window,relation,(TigRect){mouse.x+18,mouse.y-24,160,coop_play_line_height()},MM_COLOR_GOLD);
        }
    }
    preview(window);
}
bool coop_play_input(TigMessage* msg)
{
    if (textedit_ui_is_focused() || !player_get_local_pc_obj()) return false;
    if (iso_interface_window_get()==ROTWIN_TYPE_QUANTITY || intgame_mode_get()==INTGAME_MODE_DIALOG
        || intgame_mode_get()==INTGAME_MODE_QUANTITY || intgame_mode_get()==INTGAME_MODE_SLEEP) return false;
    if (msg->type!=TIG_MESSAGE_KEYBOARD) return false;
    SDL_Scancode key=msg->data.keyboard.scancode;
    int command=-1;
    for (unsigned i=0;i<coop_play_binding_count();i++) if (coop_play_binding_key(i)==key) { command=(int)i; break; }
    if (command<0) {
        for (unsigned i=0;i<coop_play_binding_count();i++) if (key==coop_play_binding_original(i) && coop_play_binding_key(i)!=key) return true;
        return false;
    }
    bool down=msg->data.keyboard.pressed;
    if (command==0) { if (down) coop_play_ui_open(PLAY_TAB_LOG); return true; }
    if (command==1) { if (down) coop_play_ui_open(PLAY_TAB_INVENTORY); return true; }
    if (command==2) {
        if (coop_play_pref(PLAY_PREF_LABELS)) { if (down) labels_on=!labels_on; } else labels_on=down;
        object_highlight_mode_set(labels_on); gamelib_invalidate_rect(NULL); return true;
    }
    if (command==3) {
        if (down) { TigMouseState mouse; int64_t loc; if (tig_mouse_get_state(&mouse)==TIG_OK && coop_view_location_at(mouse.x,mouse.y,&loc)) coop_play_ping(loc); }
        return true;
    }
    if (command==4) { if (down) coop_play_pref_set(PLAY_PREF_BAR,!coop_play_pref(PLAY_PREF_BAR)); return true; }
    if (command==5) { if (down) coop_play_pause_toggle(); return true; }
    if (command==9) { if (down) logbook_ui_open(player_get_local_pc_obj()); return true; }
    if (command==23) {
        if (down && combat_turn_based_is_active()) {
            if (coop_runtime_gameplay()) coop_runtime_combat(COOP_COMBAT_END_TURN,0,0);
            else combat_turn_based_end_critter_turn(player_get_local_pc_obj());
        }
        return true;
    }
    /* Translate native commands once, before the normal UI dispatcher. */
    msg->data.keyboard.scancode=coop_play_binding_original((unsigned)command);
    msg->data.keyboard.key=SDL_GetKeyFromScancode(msg->data.keyboard.scancode,SDL_KMOD_NONE,false);
    return false;
}
