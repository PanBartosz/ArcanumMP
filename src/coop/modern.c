#include "coop/localization.h"
#include "coop/modern.h"
#include "coop/hud.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/display.h"
#include "coop/play_feedback.h"
#include "coop/party_ui.h"
#include "coop/spell_sounds.h"
#include "coop/play_data.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/map.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/townmap.h"
#include "ui/intgame.h"
#include "ui/textedit_ui.h"
#include "game/tc.h"
#include "game/skill.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HISTORY = COOP_PLAY_HISTORY, EVENT_BYTES = COOP_PLAY_EVENT_BYTES };
static PlayEvent history[HISTORY];
static uint32_t next = 1, first = 1;
static bool prefs_loaded;
static int prefs[PLAY_PREF_COUNT] = {100, 0, 1, 0, 1, 1, 0, 550, 0, 5, 1, 20, 1, 0};
static tig_font_handle_t fonts[MM_COLOR_COUNT], chrome_fonts[MM_COLOR_COUNT];
static CoopSession* network;
static uint32_t network_epoch, cursors[4], sent_policy[4];
static unsigned pause_policy, turn_radius = 20;
static unsigned autosave_minutes = 5;
static bool autosave_travel = true;
static bool valid_autosave_minutes(int value)
{
    return value == 0 || value == 2 || value == 5 || value == 10 || value == 15 || value == 30;
}
static uint64_t ping_at[4];
static bool map_ping_add(unsigned member,unsigned context,unsigned x,unsigned y);
typedef struct Binding { const char* name; SDL_Scancode original, assigned; } Binding;
static Binding bindings[] = {
    {"Combat log", SDL_SCANCODE_F11, SDL_SCANCODE_F11},
    {"Play tools", SDL_SCANCODE_B, SDL_SCANCODE_B},
    {"Highlight interactables", SDL_SCANCODE_LALT, SDL_SCANCODE_LALT},
    {"Ping at pointer", SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_BACKSLASH},
    {"Extra action bar", SDL_SCANCODE_GRAVE, SDL_SCANCODE_GRAVE},
    {"Turn-based / real-time", SDL_SCANCODE_PAUSE, SDL_SCANCODE_PAUSE},
    {"Inventory", SDL_SCANCODE_I, SDL_SCANCODE_I},
    {"Character", SDL_SCANCODE_C, SDL_SCANCODE_C},
    {"Map", SDL_SCANCODE_W, SDL_SCANCODE_W},
    {"Logbook", SDL_SCANCODE_L, SDL_SCANCODE_L},
    {"Spells", SDL_SCANCODE_M, SDL_SCANCODE_M},
    {"Skills", SDL_SCANCODE_K, SDL_SCANCODE_K},
    {"Combat mode", SDL_SCANCODE_R, SDL_SCANCODE_R},
    {"Combat timing", SDL_SCANCODE_SPACE, SDL_SCANCODE_SPACE},
    {"Options", SDL_SCANCODE_O, SDL_SCANCODE_O},
    {"Documents / quick save (solo)", SDL_SCANCODE_F7, SDL_SCANCODE_F7},
    {"Conversations / quick load (solo)", SDL_SCANCODE_F8, SDL_SCANCODE_F8},
    {"Shared save", SDL_SCANCODE_F9, SDL_SCANCODE_F9},
    {"Shared load / hide UI (solo)", SDL_SCANCODE_F10, SDL_SCANCODE_F10},
    {"Party", SDL_SCANCODE_F12, SDL_SCANCODE_F12},
    {"Rest", SDL_SCANCODE_S, SDL_SCANCODE_S},
    {"Schematics", SDL_SCANCODE_T, SDL_SCANCODE_T},
    {"Fate", SDL_SCANCODE_F, SDL_SCANCODE_F},
    {"End turn", SDL_SCANCODE_E, SDL_SCANCODE_E},
    {"Quick slot 1", SDL_SCANCODE_1, SDL_SCANCODE_1},
    {"Quick slot 2", SDL_SCANCODE_2, SDL_SCANCODE_2},
    {"Quick slot 3", SDL_SCANCODE_3, SDL_SCANCODE_3},
    {"Quick slot 4", SDL_SCANCODE_4, SDL_SCANCODE_4},
    {"Quick slot 5", SDL_SCANCODE_5, SDL_SCANCODE_5},
    {"Quick slot 6", SDL_SCANCODE_6, SDL_SCANCODE_6},
    {"Quick slot 7", SDL_SCANCODE_7, SDL_SCANCODE_7},
    {"Quick slot 8", SDL_SCANCODE_8, SDL_SCANCODE_8},
    {"Quick slot 9", SDL_SCANCODE_9, SDL_SCANCODE_9},
    {"Quick slot 10", SDL_SCANCODE_0, SDL_SCANCODE_0},
    {"Companion order 1", SDL_SCANCODE_F1, SDL_SCANCODE_F1},
    {"Companion order 2", SDL_SCANCODE_F2, SDL_SCANCODE_F2},
    {"Companion order 3", SDL_SCANCODE_F3, SDL_SCANCODE_F3},
    {"Companion order 4", SDL_SCANCODE_F4, SDL_SCANCODE_F4},
    {"Companion order 5", SDL_SCANCODE_F5, SDL_SCANCODE_F5},
    {"Companion order 6", SDL_SCANCODE_F6, SDL_SCANCODE_F6},
};
static uint32_t get32(const uint8_t* p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t* p, uint32_t n)
{ for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(24-i*8)); }
static void prefs_load(void)
{
    if (prefs_loaded) return;
    prefs_loaded = true;
    FILE* file=fopen("arcmp-play.cfg","rt");
    if (!file) return;
    char line[160]; unsigned i; int n;
    while (fgets(line,sizeof(line),file)) {
        if (sscanf(line,"pref%u=%d",&i,&n)==2 && i<PLAY_PREF_COUNT) {
            if ((i==PLAY_PREF_TEXT && n>=100 && n<=150 && n%25==0)
                || (i==PLAY_PREF_TIP_DELAY && n>=0 && n<=1500)
                || (i==PLAY_PREF_AUTOPAUSE && n>=0 && n<=15)
                || (i==PLAY_PREF_AUTOSAVE_MINUTES && valid_autosave_minutes(n))
                || (i==PLAY_PREF_TURN_RADIUS && n>=5 && n<=40 && n%5==0)
                || (i!=PLAY_PREF_TEXT && i!=PLAY_PREF_TIP_DELAY && i!=PLAY_PREF_AUTOPAUSE && i!=PLAY_PREF_AUTOSAVE_MINUTES && i!=PLAY_PREF_TURN_RADIUS && (n==0 || n==1))) prefs[i]=n;
        } else if (sscanf(line,"key%u=%d",&i,&n)==2 && i<SDL_arraysize(bindings)
            && n>SDL_SCANCODE_UNKNOWN && n<SDL_SCANCODE_COUNT && n!=SDL_SCANCODE_ESCAPE) bindings[i].assigned=(SDL_Scancode)n;
    }
    fclose(file);
    /* A hand-edited config must never make two commands claim the same key. */
    for (unsigned i=0;i<SDL_arraysize(bindings);i++) for (unsigned j=0;j<i;j++)
        if (bindings[i].assigned==bindings[j].assigned) {
            for (unsigned k=0;k<SDL_arraysize(bindings);k++) bindings[k].assigned=bindings[k].original;
            return;
        }
}
static bool prefs_save(void)
{
    FILE* f=fopen("arcmp-play.cfg.tmp","wt"); if (!f) return false;
    bool good=true;
    for (unsigned i=0;i<PLAY_PREF_COUNT;i++) good &= fprintf(f,"pref%u=%d\n",i,prefs[i])>0;
    for (unsigned i=0;i<SDL_arraysize(bindings);i++) good &= fprintf(f,"key%u=%d\n",i,bindings[i].assigned)>0;
    good=fclose(f)==0 && good;
    return good && SDL_RenamePath("arcmp-play.cfg.tmp","arcmp-play.cfg");
}
int coop_play_pref(unsigned i) { prefs_load(); return i<PLAY_PREF_COUNT ? prefs[i] : 0; }
bool coop_play_spell_sound_muted(const char* path, int loops)
{ return coop_play_pref(PLAY_PREF_MUTE_SPELL_LOOPS) && coop_spell_sound_repeats(path, loops); }
bool coop_play_pref_set(unsigned i, int value)
{
    prefs_load();
    if (i>=PLAY_PREF_COUNT || (i==PLAY_PREF_TEXT && (value<100 || value>150 || value%25))
        || (i==PLAY_PREF_TIP_DELAY && (value<0 || value>1500))
        || (i==PLAY_PREF_AUTOPAUSE && (value<0 || value>15))
        || (i==PLAY_PREF_AUTOSAVE_MINUTES && !valid_autosave_minutes(value))
        || (i==PLAY_PREF_TURN_RADIUS && (value<5 || value>40 || value%5))
        || (i!=PLAY_PREF_TEXT && i!=PLAY_PREF_TIP_DELAY && i!=PLAY_PREF_AUTOPAUSE && i!=PLAY_PREF_AUTOSAVE_MINUTES && i!=PLAY_PREF_TURN_RADIUS && value!=0 && value!=1)) return false;
    if ((i==PLAY_PREF_AUTOPAUSE || i==PLAY_PREF_TURN_RADIUS || i==PLAY_PREF_AUTOSAVE_MINUTES || i==PLAY_PREF_AUTOSAVE_TRAVEL)
        && coop_campaign_active() && !coop_authority_can_simulate()) return false;
    int old=prefs[i]; prefs[i]=value;
    if (!prefs_save()) { prefs[i]=old; return false; }
    if (i==PLAY_PREF_TEXT || i==PLAY_PREF_CONTRAST) for (unsigned n=0;n<MM_COLOR_COUNT;n++) {
        if (fonts[n]) tig_font_destroy(fonts[n]); fonts[n]=0;
        if (chrome_fonts[n]) tig_font_destroy(chrome_fonts[n]); chrome_fonts[n]=0;
    }
    if (i==PLAY_PREF_AUTOPAUSE) pause_policy=(unsigned)value;
    if (i==PLAY_PREF_CRIT_SHAKE && !value) coop_view_effect_reset();
    if (i==PLAY_PREF_MUTE_SPELL_LOOPS) tig_sound_refresh_effect_filter();
    if (i==PLAY_PREF_TEXT) tc_readability_changed();
    if (i==PLAY_PREF_TEXT || i==PLAY_PREF_CONTRAST) coop_hud_refresh();
    return true;
}
unsigned coop_play_binding_count(void) { return SDL_arraysize(bindings); }
const char* coop_play_binding_name(unsigned i) { return i<SDL_arraysize(bindings) ? coop_tr(bindings[i].name) : ""; }
SDL_Scancode coop_play_binding_key(unsigned i) { prefs_load(); return i<SDL_arraysize(bindings) ? bindings[i].assigned : SDL_SCANCODE_UNKNOWN; }
SDL_Scancode coop_play_binding_original(unsigned i) { return i<SDL_arraysize(bindings) ? bindings[i].original : SDL_SCANCODE_UNKNOWN; }
bool coop_play_binding_set(unsigned i, SDL_Scancode key)
{
    prefs_load();
    if (i>=SDL_arraysize(bindings) || key<=SDL_SCANCODE_UNKNOWN || key>=SDL_SCANCODE_COUNT || key==SDL_SCANCODE_ESCAPE) return false;
    Binding before[SDL_arraysize(bindings)]; memcpy(before,bindings,sizeof(bindings));
    for (unsigned j=0;j<SDL_arraysize(bindings);j++) if (j!=i && bindings[j].assigned==key) bindings[j].assigned=bindings[i].assigned;
    bindings[i].assigned=key;
    if (prefs_save()) return true;
    memcpy(bindings,before,sizeof(bindings)); return false;
}
void coop_play_binding_reset(void)
{
    prefs_load(); Binding before[SDL_arraysize(bindings)]; memcpy(before,bindings,sizeof(bindings));
    for (unsigned i=0;i<SDL_arraysize(bindings);i++) bindings[i].assigned=bindings[i].original;
    if (!prefs_save()) memcpy(bindings,before,sizeof(bindings));
}
void coop_play_shortcuts(const char* source, char* text, size_t size)
{
    prefs_load();
    if (!size) return;
    size_t used = 0;
    while (*source && used + 1 < size) {
        if (*source == '[' || *source == '(') {
            char close = *source == '[' ? ']' : ')';
            const char* end = strchr(source + 1, close);
            if (end && end - source > 1 && end - source < 32) {
                char key[32];
                size_t length = (size_t)(end - source - 1);
                memcpy(key, source + 1, length); key[length] = 0;
                SDL_Scancode original = SDL_GetScancodeFromName(key);
                for (unsigned i = 0; original != SDL_SCANCODE_UNKNOWN && i < SDL_arraysize(bindings); i++) {
                    if (bindings[i].original != original) continue;
                    const char* name = SDL_GetScancodeName(bindings[i].assigned);
                    length = strlen(name);
                    if (used + length + 2 >= size) { text[used] = 0; return; }
                    text[used++] = *source;
                    memcpy(text + used, name, length); used += length; text[used++] = close;
                    source = end + 1;
                    goto next;
                }
            }
        }
        text[used++] = *source++;
next:;
    }
    text[used] = 0;
}
static tig_font_handle_t play_font(MainMenuColor color, bool chrome)
{
    prefs_load(); if (color<0 || color>=MM_COLOR_COUNT) color=MM_COLOR_WHITE;
    tig_font_handle_t* handles=chrome ? chrome_fonts : fonts;
    if (!handles[color]) {
        static const uint8_t colors[MM_COLOR_COUNT][3]={{245,239,220},{255,125,125},{140,180,255},{255,217,120},{210,180,140},{150,235,160},{220,160,255},{150,220,255},{255,190,110},{190,190,190}};
        TigFont f={.flags=TIG_FONT_SCALE | TIG_FONT_SHADOW,.scale=chrome ? 1.0f : (float)prefs[PLAY_PREF_TEXT]/100.0f};
        tig_art_interface_id_create(229,0,0,0,&f.art_id);
        f.color= prefs[PLAY_PREF_CONTRAST] && color==MM_COLOR_GRAY ? tig_color_make(235,235,235)
            : tig_color_make(colors[color][0],colors[color][1],colors[color][2]);
        tig_font_create(&f,&handles[color]);
    }
    return handles[color];
}
tig_font_handle_t coop_play_font(MainMenuColor color) { return play_font(color,false); }
tig_font_handle_t coop_play_chrome_font(MainMenuColor color) { return play_font(color,true); }
int coop_play_line_height(void) { return (18*coop_play_pref(PLAY_PREF_TEXT)+99)/100; }
void coop_play_name(int64_t obj, char* out, size_t size)
{
    if (!obj || !obj_handle_is_valid(obj)) { snprintf(out,size,"Environment"); return; }
    char value[MAX_STRING]; object_examine(obj,player_get_local_pc_obj(),value);
    snprintf(out,size,"%s",*value ? value : "Unknown");
}
/* Event payloads retain original resource names, independently of the host's
 * presentation language. */
static void canonical_name(int64_t obj,char* out,size_t size)
{
    coop_language_canonical_begin();
    coop_play_name(obj,out,size);
    coop_language_canonical_end();
}
unsigned coop_play_members(int64_t obj)
{
    if (!obj || !obj_handle_is_valid(obj)) return 0;
    int member=coop_campaign_slot_for_object(obj);
    if (member<0 && obj_type_is_critter(obj_field_int32_get(obj,OBJ_F_TYPE))) member=coop_campaign_slot_for_object(critter_pc_leader_get(obj));
    if (member<0 && !coop_campaign_active()) {
        int64_t pc=player_get_local_pc_obj();
        if (obj==pc || (obj_type_is_critter(obj_field_int32_get(obj,OBJ_F_TYPE)) && critter_pc_leader_get(obj)==pc)) member=0;
    }
    return member>=0 && member<4 ? 1u<<member : 0;
}
uint32_t coop_play_event_first(void) { return first; }
uint32_t coop_play_event_end(void) { return next; }
const PlayEvent* coop_play_event_at(uint32_t n) { return n>=first && n<next ? &history[n%HISTORY] : NULL; }
static void append(PlayEvent e)
{
    if (!e.sequence || e.sequence<next) return;
    if (e.sequence>next) first=next=e.sequence;
    history[e.sequence%HISTORY]=e;
    next=e.sequence+1;
    if (next-first>HISTORY) first=next-HISTORY;
}
void coop_play_event(unsigned kind, int64_t actor, int64_t target, const char* summary, const char* details)
{
    if (!coop_authority_can_simulate() || kind>=PLAY_EVENT_KINDS || !summary || !*summary || next==UINT32_MAX) return;
    PlayEvent e={.sequence=next,.map=(uint32_t)map_current_map(),.time=(uint32_t)SDL_GetTicks(),.kind=(uint8_t)kind,
        .members=(uint8_t)(coop_play_members(actor)|coop_play_members(target))};
    snprintf(e.summary,sizeof(e.summary),"%s",summary);
    snprintf(e.details,sizeof(e.details),"%s",details ? details : "");
    append(e);
}
void coop_play_float(int64_t obj, const char* text)
{
    if (!obj || !text || !*text || !coop_authority_can_simulate()) return;
    char name[96], message[192]; canonical_name(obj,name,sizeof(name));
    snprintf(message,sizeof(message),"%s: %.90s",name,text);
    coop_play_event(PLAY_EFFECT,obj,0,message,"Native combat or status feedback.");
}
void coop_play_status_change(int64_t obj,int field,unsigned before,unsigned after)
{
    if (!coop_authority_can_simulate() || before==after) return;
    int64_t pc=player_get_local_pc_obj();
    if (!pc || !obj_handle_is_valid(pc) || !obj_handle_is_valid(obj) || obj_is_proto(obj)) return;
    /* Moving/animating critters update flags frequently. Resolve a name only
     * for a change that can actually produce a visible status entry. */
    const unsigned critter_mask=OCF_STUNNED|OCF_PARALYZED|OCF_BLINDED|OCF_CRIPPLED_ARMS_ONE
        |OCF_CRIPPLED_ARMS_BOTH|OCF_CRIPPLED_LEGS_BOTH|OCF_SLEEPING
        |OCF_IS_CONCEALED|OCF_MOVING_SILENTLY|OCF_FLEEING|OCF_MUTE|OCF_SURRENDERED|OCF_SPELL_FLEE;
    if (field!=OBJ_F_SPELL_FLAGS && !((before^after)&critter_mask)) return;
    static const char* spell_names[]={"Invisible","Floating","Body of Air","Body of Earth","Body of Fire","Body of Water","Detecting magic","Detecting alignment",
        "Detecting traps","Detecting invisibility","Shielded","Anti-magic shell","Bonds of Magic","Reflection","Summoned","Illusion","Petrified","Polymorphed",
        "Mirrored","Shrunk","Passwall","Water walking","Magnetic inversion","Charmed","Entangled","Spoken with dead","Tempus Fugit","Mind control","Drunk","Enshrouded","Familiar","Hardened Hands"};
    static const unsigned critter_bits[]={OCF_STUNNED,OCF_PARALYZED,OCF_BLINDED,OCF_CRIPPLED_ARMS_ONE,OCF_CRIPPLED_ARMS_BOTH,OCF_CRIPPLED_LEGS_BOTH,OCF_SLEEPING,OCF_IS_CONCEALED,OCF_MOVING_SILENTLY,OCF_FLEEING,OCF_MUTE,OCF_SURRENDERED,OCF_SPELL_FLEE};
    static const char* critter_names[]={"Stunned","Paralyzed","Blinded","One arm crippled","Both arms crippled","Legs crippled","Sleeping","Concealed","Moving silently","Fleeing","Silenced","Surrendered","Magical fear"};
    char name[80]; canonical_name(obj,name,sizeof(name));
    unsigned count=field==OBJ_F_SPELL_FLAGS ? 32 : SDL_arraysize(critter_bits);
    for (unsigned i=0;i<count;i++) {
        unsigned bit=field==OBJ_F_SPELL_FLAGS ? 1u<<i : critter_bits[i];
        const char* status=field==OBJ_F_SPELL_FLAGS ? spell_names[i] : critter_names[i];
        if (!status || !((before^after)&bit)) continue;
        char line[192]; snprintf(line,sizeof(line),"%s: %s %s",name,status,after&bit ? "applied" : "ended");
        coop_play_status_feedback(obj,status,(after&bit)!=0);
        coop_play_event(PLAY_EFFECT,obj,0,line,"Native status flags changed. Includes effects on enemies as well as party members.");
    }
}
void coop_play_attack_miss(int64_t actor,int64_t target,bool critical)
{
    char a[72],b[72],line[192]; canonical_name(actor,a,sizeof(a)); canonical_name(target,b,sizeof(b));
    snprintf(line,sizeof(line),"%s %s %s",a,critical ? "critically misses" : "misses",b);
    coop_play_event(PLAY_COMBAT,actor,target,line,critical ? "The native attack roll produced a critical failure. Consequences appear in subsequent entries." : "The native attack missed or was dodged. No damage was applied by this attack.");
}
void coop_play_damage(int64_t actor,int64_t target,int hp,int fatigue,unsigned flags,unsigned effects)
{
    char a[64],b[64],line[192],detail[384]; canonical_name(actor,a,sizeof(a)); canonical_name(target,b,sizeof(b));
    snprintf(line,sizeof(line),"%s %s %s: %d HP, %d fatigue",a,flags&CF_CRITICAL ? "critically hits" : "hits",b,hp,fatigue);
    snprintf(detail,sizeof(detail),"Actual health and fatigue lost after native resistance, armor, immunity and script processing. Damage effects:%s%s%s%s%s%s%s",
        effects&CDF_STUN ? " stunned" : "",effects&CDF_KNOCKDOWN ? " knocked down" : "",effects&CDF_KNOCKOUT ? " unconscious" : "",
        effects&CDF_BLIND ? " blinded" : "",effects&(CDF_CRIPPLE_ARM|CDF_CRIPPLE_LEG) ? " crippled" : "",
        effects&CDF_DAMAGE_WEAPON ? " weapon damaged" : "",effects&CDF_DESTROY_WEAPON ? " weapon destroyed" : "");
    coop_play_event(PLAY_COMBAT,actor,target,line,detail);
    if ((flags&CF_CRITICAL) && (hp>0 || fatigue>0)) coop_play_critical_feedback(target);
}
void coop_play_roll(int64_t actor,int64_t target,int skill,int effectiveness,int difficulty,int roll,int critical_roll,unsigned flags)
{
    if (skill!=SKILL_BOW && skill!=SKILL_MELEE && skill!=SKILL_THROWING && skill!=SKILL_FIREARMS && skill!=SKILL_DODGE) return;
    char name[80],line[192],details[384]; canonical_name(actor,name,sizeof(name));
    const char* skill_name=coop_language_original(skill<BASIC_SKILL_COUNT ? basic_skill_name(skill) : tech_skill_name(skill-BASIC_SKILL_COUNT));
    snprintf(line,sizeof(line),"%s: %s %s%s",name,skill_name,flags&SKILL_INVOCATION_SUCCESS ? "succeeds" : "fails",flags&SKILL_INVOCATION_CRITICAL ? " (critical)" : "");
    snprintf(details,sizeof(details),"Native roll %d + difficulty %d versus effectiveness %d. Base success chance: %d%%. Critical roll: %d.%s\nPenalties:%s%s%s%s%s%s%s%s\nFinal outcome includes native Dodge, Fate and critical rules.",
        roll,difficulty,effectiveness,SDL_clamp(effectiveness-difficulty,0,100),critical_roll,flags&SKILL_INVOCATION_FORCED ? " Forced success." : "",
        flags&SKILL_INVOCATION_PENALTY_MSR ? " strength" : "",flags&SKILL_INVOCATION_PENALTY_RANGE ? " range" : "",flags&SKILL_INVOCATION_PENALTY_PERCEPTION ? " perception" : "",
        flags&SKILL_INVOCATION_PENALTY_COVER ? " cover" : "",flags&SKILL_INVOCATION_PENALTY_LIGHT ? " light" : "",flags&SKILL_INVOCATION_PENALTY_INJURY ? " injury" : "",
        flags&SKILL_INVOCATION_BLOCKED_SHOT ? " blocked shot" : "",flags&SKILL_INVOCATION_MAGIC_TECH_PENALTY ? " aptitude" : "");
    coop_play_event(PLAY_COMBAT,actor,target,line,details);
}
static void encode(uint8_t* p,uint32_t epoch,const PlayEvent* e)
{
    memset(p,0,EVENT_BYTES); put32(p,epoch); put32(p+4,e->sequence); put32(p+8,e->map); put32(p+12,e->time);
    if (epoch && (e->kind==PLAY_PING || e->kind==PLAY_MAP_PING)) {
        uint32_t age=(uint32_t)(SDL_GetTicks()-e->time); put32(p+12,age>=10000 ? 0 : 10000-age);
    }
    put32(p+16,e->x); put32(p+20,e->y); p[24]=e->kind; p[25]=e->members;
    memcpy(p+28,e->summary,sizeof(e->summary)); memcpy(p+220,e->details,sizeof(e->details));
}
static bool decode(const uint8_t* p,size_t size,PlayEvent* e)
{
    if (!coop_play_event_record_valid(p,size)) return false;
    *e=(PlayEvent){.sequence=get32(p+4),.map=get32(p+8),.time=get32(p+12),.x=get32(p+16),.y=get32(p+20),.kind=p[24],.members=p[25]};
    memcpy(e->summary,p+28,192); memcpy(e->details,p+220,384); return true;
}
unsigned coop_play_pause_policy(void)
{ return coop_campaign_active() && !coop_authority_can_simulate() ? pause_policy : (unsigned)coop_play_pref(PLAY_PREF_AUTOPAUSE); }
unsigned coop_play_turn_radius(void)
{ return coop_campaign_active() && !coop_authority_can_simulate() ? turn_radius : (unsigned)coop_play_pref(PLAY_PREF_TURN_RADIUS); }
unsigned coop_play_autosave_minutes(void)
{ return coop_campaign_active() && !coop_authority_can_simulate() ? autosave_minutes : (unsigned)coop_play_pref(PLAY_PREF_AUTOSAVE_MINUTES); }
bool coop_play_autosave_travel(void)
{ return coop_campaign_active() && !coop_authority_can_simulate() ? autosave_travel : coop_play_pref(PLAY_PREF_AUTOSAVE_TRAVEL) != 0; }
void coop_play_network_tick(CoopSession* s,uint32_t epoch,unsigned ready)
{
    if (network!=s || network_epoch!=epoch) {
        network=s; network_epoch=epoch; memset(cursors,0,sizeof(cursors));
        for (unsigned i=0;i<4;i++) sent_policy[i]=UINT32_MAX;
    }
    coop_feedback_network(s,epoch,ready);
    if (!coop_session_is_host(s)) return;
    pause_policy=coop_play_pause_policy();
    unsigned policy=pause_policy | (coop_play_autosave_minutes()<<4) | (coop_play_autosave_travel() ? 1u<<10 : 0) | (coop_play_turn_radius()<<11);
    for (unsigned i=1;i<4;i++) if (ready&(1u<<i)) {
        if (sent_policy[i]!=policy) {
            uint8_t p[8]; put32(p,epoch); put32(p+4,policy);
            if (coop_session_send(s,(uint8_t)i,COOP_PLAY_POLICY,p,sizeof(p))) sent_policy[i]=policy;
        }
        uint32_t cursor=cursors[i]<first ? first : cursors[i];
        for (unsigned budget=0;cursor<next && budget<8;budget++,cursor++) {
            uint8_t p[EVENT_BYTES]; encode(p,epoch,coop_play_event_at(cursor));
            if (!coop_session_send(s,(uint8_t)i,COOP_PLAY_EVENT,p,sizeof(p))) break;
        }
        cursors[i]=cursor;
    }
}
static bool ping_add(unsigned member,int64_t location)
{
    if (member>=4 || !location || next==UINT32_MAX) return false;
    uint64_t now=SDL_GetTicks();
    if (ping_at[member] && now-ping_at[member]<750) return false;
    int64_t mx,my; location_limits_get(&mx,&my);
    if (location_get_x(location)<0 || location_get_y(location)<0 || location_get_x(location)>=mx || location_get_y(location)>=my) return false;
    ping_at[member]=now;
    char name[80],line[192]; canonical_name(coop_campaign_object(member),name,sizeof(name));
    snprintf(line,sizeof(line),"%s pinged a location",name);
    PlayEvent e={.sequence=next,.map=(uint32_t)map_current_map(),.time=(uint32_t)now,.kind=PLAY_PING,.members=(uint8_t)(1u<<member),
        .x=(uint32_t)location_get_x(location),.y=(uint32_t)location_get_y(location)};
    snprintf(e.summary,sizeof(e.summary),"%s",line); snprintf(e.details,sizeof(e.details),"Map %u, tile %u, %u",e.map,e.x,e.y);
    append(e); return true;
}
bool coop_play_network_message(CoopSession* s,uint32_t epoch,uint8_t sender,int member,uint8_t type,const uint8_t* p,size_t size)
{
    if (size<4 || get32(p)!=epoch) return false;
    if (type==COOP_PLAY_FEEDBACK) return !coop_session_is_host(s) && coop_feedback_receive(sender,p,size);
    if (type==COOP_PLAY_EVENT) {
        PlayEvent e;
        if (coop_session_is_host(s) || sender || !decode(p,size,&e)) return false;
        /* Packet timestamps belong to the host clock; transient markers use
         * receipt time so different process uptimes never change their lifetime. */
        if (e.kind==PLAY_PING || e.kind==PLAY_MAP_PING) {
            if (e.time>10000) return false;
            e.time=(uint32_t)SDL_GetTicks()-(10000-e.time);
        }
        append(e); return true;
    }
    if (type==COOP_PLAY_POLICY) {
        if (coop_session_is_host(s) || sender || size!=8) return false;
        unsigned policy=get32(p+4), minutes=(policy>>4)&63, radius=(policy>>11)&63;
        if ((policy&~131071u) || !valid_autosave_minutes((int)minutes) || radius<5 || radius>40 || radius%5) return false;
        pause_policy=policy&15; turn_radius=radius; autosave_minutes=minutes; autosave_travel=(policy&(1u<<10))!=0; return true;
    }
    if (type==COOP_PLAY_PING) {
        if (!coop_session_is_host(s) || size!=16 || get32(p+4)!=(uint32_t)map_current_map()) return false;
        ping_add((unsigned)member,location_make(get32(p+8),get32(p+12))); return true;
    }
    if (type==COOP_PLAY_MAP_PING) {
        if (!coop_session_is_host(s) || size!=16) return false;
        map_ping_add((unsigned)member,get32(p+4),get32(p+8),get32(p+12)); return true;
    }
    return false;
}
bool coop_play_ping(int64_t location)
{
    if (!coop_campaign_active() || !network || coop_runtime_phase()!=COOP_RUNTIME_PLAYING) return false;
    if (coop_session_is_host(network)) return ping_add((unsigned)coop_campaign_slot_for_object(player_get_local_pc_obj()),location);
    uint8_t p[16]; put32(p,network_epoch); put32(p+4,(uint32_t)map_current_map());
    put32(p+8,(uint32_t)location_get_x(location)); put32(p+12,(uint32_t)location_get_y(location));
    return coop_session_send(network,0,COOP_PLAY_PING,p,sizeof(p));
}
static bool map_ping_add(unsigned member,unsigned context,unsigned x,unsigned y)
{
    if (member>=4 || next==UINT32_MAX) return false;
    if (!context) { if (x>=2000 || y>=2000) return false; }
    else {
        TownMapInfo info;
        if (context>INT32_MAX || !townmap_info((int)context,&info) || info.scale<=0
            || x>=info.width*(double)info.scale || y>=info.height*(double)info.scale) return false;
    }
    uint64_t now=SDL_GetTicks(); if (ping_at[member] && now-ping_at[member]<750) return false;
    ping_at[member]=now;
    PlayEvent e={.sequence=next,.map=context,.time=(uint32_t)now,.x=x,.y=y,.kind=PLAY_MAP_PING,.members=(uint8_t)(1u<<member)};
    char name[96]; canonical_name(coop_campaign_object(member),name,sizeof(name));
    snprintf(e.summary,sizeof(e.summary),"%s pinged the %s map",name,context ? "local" : "world");
    snprintf(e.details,sizeof(e.details),"Map context %u, coordinates %u, %u. This marker expires after ten seconds.",context,x,y);
    append(e); return true;
}
bool coop_play_map_ping(unsigned context,unsigned x,unsigned y)
{
    if (!coop_campaign_active() || !network || coop_runtime_phase()!=COOP_RUNTIME_PLAYING) return false;
    if (coop_session_is_host(network)) return map_ping_add((unsigned)coop_campaign_slot_for_object(player_get_local_pc_obj()),context,x,y);
    uint8_t p[16]; put32(p,network_epoch); put32(p+4,context); put32(p+8,x); put32(p+12,y);
    return coop_session_send(network,0,COOP_PLAY_MAP_PING,p,sizeof(p));
}
void coop_play_pause_toggle(void)
{
    if (coop_campaign_active()) {
        if (!coop_authority_can_simulate()) { intgame_message_window_display_str(-1, coop_tr("Only the host can change combat timing.")); return; }
        coop_runtime_combat(COOP_COMBAT_POLICY, combat_is_turn_based() ? 0 : 1, 0);
    } else sub_4B6C90(!combat_is_turn_based());
}
void coop_play_reset(void)
{
    memset(history,0,sizeof(history)); next=first=1;
    coop_play_ui_reset(); coop_play_world_reset();
    coop_play_data_reset();
    network=NULL; network_epoch=0; memset(ping_at,0,sizeof(ping_at));
    coop_feedback_reset();
}
bool coop_play_save(void)
{
    TigFile* f=tig_file_fopen("Save\\Current\\arcmp-events.bin","wb"); if (!f) return false;
    uint8_t head[16]={ 'M','P','E','V','0','0','0','1' }; put32(head+8,first); put32(head+12,next);
    bool good=tig_file_fwrite(head,1,sizeof(head),f)==sizeof(head);
    for (uint32_t n=first;good && n<next;n++) {
        uint8_t p[EVENT_BYTES]; encode(p,0,coop_play_event_at(n));
        good=tig_file_fwrite(p,1,sizeof(p),f)==sizeof(p);
    }
    return tig_file_fclose(f)==0 && good;
}
bool coop_play_load(void)
{
    coop_play_reset();
    const char* path="Save\\Current\\arcmp-events.bin";
    if (!tig_file_exists(path,NULL)) return true;
    TigFile* f=tig_file_fopen(path,"rb"); if (!f) return false;
    uint8_t head[16]; bool good=tig_file_fread(head,1,sizeof(head),f)==sizeof(head);
    uint32_t begin=good ? get32(head+8) : 0,end=good ? get32(head+12) : 0;
    good=good && !memcmp(head,"MPEV0001",8) && begin && end>=begin && end<UINT32_MAX && end-begin<=HISTORY
        && tig_file_filelength(f)==16+(int)(end-begin)*EVENT_BYTES;
    if (good) first=next=begin;
    for (uint32_t n=begin;good && n<end;n++) {
        uint8_t p[EVENT_BYTES]; PlayEvent e;
        good=tig_file_fread(p,1,sizeof(p),f)==sizeof(p) && decode(p,sizeof(p),&e) && e.sequence==n;
        if (good) { if (e.kind==PLAY_PING || e.kind==PLAY_MAP_PING) e.time=(uint32_t)SDL_GetTicks()-10000; append(e); }
    }
    return tig_file_fclose(f)==0 && good;
}
