#include "coop/play_data.h"
#include "coop/modern.h"
#include "coop/authority.h"
#include "coop/campaign.h"
#include "coop/engine_craft.h"
#include "coop/engine_object.h"
#include "coop/engine_service.h"
#include "coop/runtime.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/damage_type.h"
#include "game/item.h"
#include "game/magictech.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/quest.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/tech.h"
#include "ui/hotkey_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/schematic_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JUNK_LIMIT=256, PIN_LIMIT=5 };
typedef struct Extra { int kind,data; ObjectID item; } Extra;
static ObjectID bound,junk[JUNK_LIMIT];
static unsigned junk_count,pins[PIN_LIMIT];
static Extra extras[PLAY_EXTRA_SLOTS];
static uint8_t seen[2000];
static char note[241],path[120];
static bool loaded;
static unsigned bulk_phase,bulk_sold,bulk_skipped;
static ObjectID bulk_item;
static uint32_t bulk_result;
static uint64_t bulk_deadline;
static char bulk_status[192];
static bool matches(const char* value,const char* query)
{
    if (!query || !*query) return true;
    size_t n=strlen(query);
    for (;*value;value++) {
        size_t i=0; for (;i<n && value[i] && tolower((unsigned char)value[i])==tolower((unsigned char)query[i]);i++);
        if (i==n) return true;
    }
    return false;
}
static void safe_line(char* text)
{ for (char* p=text;*p;p++) if ((unsigned char)*p<32) *p=' '; }
static bool store(void)
{
    if (!*path) return false;
    char temporary[128]; snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    FILE* f=fopen(temporary,"wt"); if (!f) return false;
    bool good=fprintf(f,"note=%s\n",note)>0;
    for (unsigned i=0;i<junk_count;i++) { char id[OID_STR_LEN]; objid_id_to_str(id,junk[i]); good &= fprintf(f,"junk=%s\n",id)>0; }
    for (unsigned i=0;i<PIN_LIMIT;i++) good &= fprintf(f,"pin%u=%u\n",i,pins[i])>0;
    for (unsigned i=0;i<2000;i++) if (seen[i]) good &= fprintf(f,"seen%u=%u\n",i,seen[i])>0;
    for (unsigned i=0;i<PLAY_EXTRA_SLOTS;i++) {
        char id[OID_STR_LEN]; objid_id_to_str(id,extras[i].item);
        good &= fprintf(f,"bar%u=%d,%d,%s\n",i,extras[i].kind,extras[i].data,id)>0;
    }
    good=fclose(f)==0 && good;
    return good && SDL_RenamePath(temporary,path);
}
void coop_play_data_reset(void)
{
    loaded=false; bound=(ObjectID){0}; *path=0; junk_count=0; *note=0; memset(pins,0,sizeof(pins));
    memset(seen,0,sizeof(seen)); memset(extras,0,sizeof(extras)); bulk_phase=0; *bulk_status=0;
}
void coop_play_data_bind(void)
{
    int64_t pc=player_get_local_pc_obj(); if (!pc || !obj_handle_is_valid(pc)) return;
    ObjectID id=obj_get_id(pc);
    if (loaded && objid_is_equal(id,bound)) return;
    coop_play_data_reset(); bound=id; loaded=true;
    char key[OID_STR_LEN]; objid_id_to_str(key,id);
    for (char* p=key;*p;p++) if (!isalnum((unsigned char)*p)) *p='_';
    snprintf(path,sizeof(path),"arcmp-player-%s.cfg",key);
    FILE* f=fopen(path,"rt"); if (!f) return;
    char line[512]; unsigned i,n; int kind,data;
    while (fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;
        if (!strncmp(line,"note=",5)) { snprintf(note,sizeof(note),"%s",line+5); safe_line(note); }
        else if (!strncmp(line,"junk=",5) && junk_count<JUNK_LIMIT) { ObjectID item; if (objid_id_from_str(&item,line+5)) junk[junk_count++]=item; }
        else if (sscanf(line,"pin%u=%u",&i,&n)==2 && i<PIN_LIMIT && n<2000) pins[i]=n;
        else if (sscanf(line,"seen%u=%u",&i,&n)==2 && i<2000 && n<QUEST_STATE_COUNT) seen[i]=(uint8_t)n;
        else if (sscanf(line,"bar%u=%d,%d,",&i,&kind,&data)==3 && i<PLAY_EXTRA_SLOTS && kind>=HOTKEY_NONE && kind<=HOTKEY_ITEM_SPELL) {
            char* p=strchr(line,','); if (p) p=strchr(p+1,',');
            ObjectID item={0}; if (p && objid_id_from_str(&item,p+1)) extras[i]=(Extra){kind,data,item};
        }
    }
    fclose(f);
}
bool coop_play_junk(int64_t obj,bool toggle)
{
    coop_play_data_bind(); if (!obj || !obj_handle_is_valid(obj)) return false;
    ObjectID id=obj_get_id(obj);
    for (unsigned i=0;i<junk_count;i++) if (objid_is_equal(id,junk[i])) {
        if (toggle) { ObjectID saved=junk[i]; junk[i]=junk[--junk_count]; if (!store()) { junk[junk_count++]=junk[i]; junk[i]=saved; } }
        return !toggle;
    }
    if (!toggle || junk_count==JUNK_LIMIT) return false;
    /* Marking is explicit; sale still checks native ownership, merchant rules,
     * equipped state and protected item types on every item. */
    junk[junk_count++]=id; if (store()) return true; junk_count--; return false;
}
bool coop_play_pin(int quest,bool toggle)
{
    coop_play_data_bind(); if (quest<1000 || quest>=2000) return false;
    for (unsigned i=0;i<PIN_LIMIT;i++) if (pins[i]==(unsigned)quest) {
        if (toggle) { pins[i]=0; if (!store()) pins[i]=(unsigned)quest; }
        return !toggle;
    }
    if (toggle) for (unsigned i=0;i<PIN_LIMIT;i++) if (!pins[i]) { pins[i]=(unsigned)quest; if (store()) return true; pins[i]=0; return false; }
    return false;
}
unsigned coop_play_pin_at(unsigned index)
{
    coop_play_data_bind(); unsigned id=index<PIN_LIMIT ? pins[index] : 0;
    int64_t pc=player_get_local_pc_obj();
    return pc && id>=1000 && id<2000 && quest_state_get(pc,(int)id)!=QUEST_STATE_UNKNOWN ? id : 0;
}
const char* coop_play_note(void) { coop_play_data_bind(); return note; }
bool coop_play_note_set(const char* text)
{
    coop_play_data_bind(); char old[sizeof(note)]; memcpy(old,note,sizeof(note));
    snprintf(note,sizeof(note),"%s",text ? text : ""); safe_line(note);
    if (store()) return true; memcpy(note,old,sizeof(note)); return false;
}
static void item_description(int64_t pc,int64_t obj,char* text,size_t size)
{
    char name[128]; coop_play_name(obj,name,sizeof(name));
    int type=obj_field_int32_get(obj,OBJ_F_TYPE);
    if (type==OBJ_TYPE_WEAPON) {
        int low,high; item_damage_min_max(obj,DAMAGE_TYPE_NORMAL,&low,&high);
        snprintf(text,size,"%s\nHealth damage: %d-%d\nSpeed: %d | Range: %d\nMinimum strength: %d\nWeight: %d | Value: %d%s",name,low,high,
            item_weapon_magic_speed(obj,pc),item_weapon_range(obj,pc),item_weapon_min_strength(obj,pc),item_weight(obj,pc),item_worth(obj),item_is_identified(obj) ? "" : "\nUnidentified: magical properties are unknown.");
    } else if (type==OBJ_TYPE_ARMOR) snprintf(text,size,"%s\nArmor class adjustment: %d\nCoverage: %d\nWeight: %d | Value: %d%s",name,item_armor_ac_adj(obj,pc,false),item_armor_coverage(obj),item_weight(obj,pc),item_worth(obj),item_is_identified(obj) ? "" : "\nUnidentified: magical properties are unknown.");
    else snprintf(text,size,"%s\nWeight: %d | Value: %d%s",name,item_weight(obj,pc),item_worth(obj),item_is_identified(obj) ? "" : "\nUnidentified");
}
static unsigned recipes_known(int64_t pc,int* output,unsigned capacity)
{
    unsigned count=0;
    for (int tech=0;tech<TECH_COUNT;tech++) for (int degree=1;degree<DEGREE_COUNT && degree<=tech_degree_get(pc,tech);degree++) {
        int id=tech_schematic_get(tech,degree); if (schematic_ui_recipe_valid(id) && count<capacity) output[count++]=id;
    }
    int length=obj_arrayfield_length_get(pc,OBJ_F_PC_SCHEMATICS_FOUND_IDX);
    for (int i=0;i<length && count<capacity;i++) {
        int id=(int)obj_arrayfield_uint32_get(pc,OBJ_F_PC_SCHEMATICS_FOUND_IDX,i); bool duplicate=false;
        for (unsigned j=0;j<count;j++) duplicate|=output[j]==id;
        if (!duplicate && schematic_ui_recipe_valid(id)) output[count++]=id;
    }
    return count;
}
static void ingredient_uses(int64_t pc,int64_t item,char* text,size_t size)
{
    int recipes[512]; unsigned count=recipes_known(pc,recipes,SDL_arraysize(recipes)),shown=0;
    for (unsigned i=0;i<count;i++) {
        SchematicInfo info={0}; schematic_ui_info_get(recipes[i],&info);
        bool used=false;
        for (unsigned v=0;v<3;v++) for (unsigned part=0;part<2;part++) {
            int64_t prototype=sub_4685A0(part ? info.item2[v] : info.item1[v]);
            if (prototype && obj_field_handle_get(item,OBJ_F_PROTOTYPE_HANDLE)==prototype) used=true;
        }
        if (used) {
            size_t length=strlen(text); snprintf(text+length,size-length,"%s%s",shown ? ", " : "\nUsed in known recipes: ",schematic_ui_product_name(recipes[i]));
            if (++shown>=5) break;
        }
    }
}
unsigned coop_play_inventory(PlayRow* rows,const char* query,unsigned category)
{
    coop_play_data_bind(); int64_t pc=player_get_local_pc_obj(); if (!pc) return 0;
    int64_t* items=NULL; int count=item_list_get(pc,&items); unsigned used=0;
    for (int i=0;i<count && used<PLAY_ROWS;i++) {
        int64_t obj=items[i]; int type=obj_field_int32_get(obj,OBJ_F_TYPE); bool marked=coop_play_junk(obj,false);
        if ((category==1 && type!=OBJ_TYPE_WEAPON && type!=OBJ_TYPE_ARMOR) || (category==2 && type!=OBJ_TYPE_FOOD && type!=OBJ_TYPE_SCROLL)
            || (category==3 && type!=OBJ_TYPE_GENERIC) || (category==4 && !marked)) continue;
        PlayRow row={.object=obj,.kind=HOTKEY_ITEM,.available=true,.marked=marked}; coop_play_name(obj,row.name,sizeof(row.name));
        if (!matches(row.name,query)) continue;
        item_description(pc,obj,row.detail,sizeof(row.detail));
        if (type==OBJ_TYPE_WEAPON || type==OBJ_TYPE_ARMOR) {
            int slot=type==OBJ_TYPE_WEAPON ? ITEM_INV_LOC_WEAPON : item_location_get(obj);
            int64_t equipped=item_wield_get(pc,slot);
            if (equipped && equipped!=obj) {
                char compare[480]; item_description(pc,equipped,compare,sizeof(compare));
                size_t n=strlen(row.detail); snprintf(row.detail+n,sizeof(row.detail)-n,"\n\nEquipped in this slot:\n%s",compare);
            } else if (equipped==obj) { size_t n=strlen(row.detail); snprintf(row.detail+n,sizeof(row.detail)-n,"\nCurrently equipped."); }
        }
        ingredient_uses(pc,obj,row.detail,sizeof(row.detail)); rows[used++]=row;
    }
    if (items) item_list_free(items); return used;
}
static void action_detail(int64_t pc,PlayRow* row)
{
    if (combat_turn_based_is_active() && combat_turn_based_whos_turn_get()!=pc) {
        row->available=false;
        snprintf(row->detail,sizeof(row->detail),"%s\nWait for your turn to use this action.",row->name); return;
    }
    if (row->kind==HOTKEY_SPELL) {
        int cost=spell_cast_cost(row->id,pc);
        row->available=critter_is_active(pc) && spell_is_known(pc,row->id) && magictech_can_charge_spell_fatigue(pc,row->id);
        snprintf(row->detail,sizeof(row->detail),"%s\nCast: %d fatigue | Range: %d\n%s\n\n%s",row->name,cost,magictech_get_range(row->id),
            row->available ? "Ready to select a target." : "Unavailable: check consciousness, fatigue and spell knowledge.",spell_description(row->id));
    } else if (row->kind==HOTKEY_SKILL) {
        static const int skills[]={SKILL_PROWLING,SKILL_PICK_POCKET,SKILL_DISARM_TRAPS,SKILL_REPAIR};
        if (row->id<0 || row->id>=4) { row->available=false; return; }
        int skill=skills[row->id];
        int level=skill<BASIC_SKILL_COUNT ? basic_skill_level(pc,skill) : tech_skill_level(pc,skill-BASIC_SKILL_COUNT);
        row->available=critter_is_active(pc);
        snprintf(row->detail,sizeof(row->detail),"%s\nSkill level: %d\n%s\nSelect this skill, then choose its target in the world.",row->name,level,row->available ? "Ready" : "Unavailable: character is incapacitated.");
    }
}
unsigned coop_play_actions(PlayRow* rows,const char* query)
{
    coop_play_data_bind(); int64_t pc=player_get_local_pc_obj(); if (!pc) return 0; unsigned count=0;
    for (int i=0;i<SPELL_COUNT;i++) if (spell_is_known(pc,i) && matches(spell_name(i),query)) {
        PlayRow row={.id=i,.kind=HOTKEY_SPELL}; snprintf(row.name,sizeof(row.name),"%s",spell_name(i)); action_detail(pc,&row); rows[count++]=row;
    }
    /* Passive weapon/social skills stay in the native character sheet. These
     * are the native skill selector's actual invocable actions. */
    static const int skills[]={SKILL_PROWLING,SKILL_PICK_POCKET,SKILL_DISARM_TRAPS,SKILL_REPAIR};
    for (unsigned i=0;i<SDL_arraysize(skills);i++) {
        int s=skills[i]; const char* name=s<BASIC_SKILL_COUNT ? basic_skill_name(s) : tech_skill_name(s-BASIC_SKILL_COUNT);
        if (!matches(name,query)) continue;
        PlayRow row={.id=(int)i,.kind=HOTKEY_SKILL}; snprintf(row.name,sizeof(row.name),"%s",name); action_detail(pc,&row); rows[count++]=row;
    }
    return count;
}
void coop_play_action_activate(const PlayRow* row)
{
    if (!row || !row->available) return;
    Hotkey key={.slot=-1,.type=row->kind,.data=row->id,.art_id=TIG_ART_ID_INVALID};
    if (row->kind==HOTKEY_ITEM) { if (!row->object || !obj_handle_is_valid(row->object)) return; sub_4440E0(row->object,&key.item_obj); }
    intgame_hotkey_activate(&key);
}
bool coop_play_action_assign(unsigned slot,const PlayRow* row)
{
    coop_play_data_bind(); if (slot>=PLAY_EXTRA_SLOTS || !row) return false;
    Extra old=extras[slot]; extras[slot]=(Extra){.kind=row->kind,.data=row->id,.item=row->object ? obj_get_id(row->object) : (ObjectID){0}};
    if (store()) return true; extras[slot]=old; return false;
}
bool coop_play_action_slot(unsigned slot,PlayRow* row)
{
    coop_play_data_bind(); if (slot>=PLAY_EXTRA_SLOTS || !extras[slot].kind) return false;
    Extra e=extras[slot]; *row=(PlayRow){.kind=e.kind,.id=e.data,.available=true}; int64_t pc=player_get_local_pc_obj();
    if (e.kind==HOTKEY_SPELL && e.data>=0 && e.data<SPELL_COUNT && spell_is_known(pc,e.data)) snprintf(row->name,sizeof(row->name),"%s",spell_name(e.data));
    else if (e.kind==HOTKEY_SKILL && e.data>=0 && e.data<4) {
        static const int skills[]={SKILL_PROWLING,SKILL_PICK_POCKET,SKILL_DISARM_TRAPS,SKILL_REPAIR};
        int skill=skills[e.data]; snprintf(row->name,sizeof(row->name),"%s",skill<BASIC_SKILL_COUNT ? basic_skill_name(skill) : tech_skill_name(skill-BASIC_SKILL_COUNT));
    }
    else if (e.kind==HOTKEY_ITEM) {
        row->object=obj_pool_perm_lookup(e.item); int64_t parent=0;
        if (!row->object || !item_is_item(row->object) || !item_parent(row->object,&parent) || parent!=pc) return false;
        coop_play_name(row->object,row->name,sizeof(row->name));
    } else return false;
    action_detail(pc,row); return true;
}
static const char* quest_status(int state)
{
    static const char* names[]={"Unknown","Mentioned","Accepted","Achieved","Completed","Completed by another","Failed"};
    return state>=0 && state<QUEST_STATE_COUNT ? names[state] : "Updated";
}
unsigned coop_play_quests(PlayRow* rows,const char* query,bool recent)
{
    coop_play_data_bind(); int64_t pc=player_get_local_pc_obj(); if (!pc) return 0;
    QuestLogbookEntry entries[2000]; int count=quest_get_logbook_data(pc,entries); unsigned used=0;
    for (int i=count-1;i>=0;i--) {
        QuestLogbookEntry q=entries[i]; if (q.num<1000 || q.num>=2000 || (recent && seen[q.num]==q.state)) continue;
        PlayRow row={.id=q.num,.marked=coop_play_pin(q.num,false),.available=true};
        char description[1200]={0}; quest_copy_description(pc,q.num,description,sizeof(description));
        if (!matches(description,query)) continue;
        snprintf(row.name,sizeof(row.name),"[%s] %.110s",quest_status(q.state),description);
        snprintf(row.detail,sizeof(row.detail),"%s\n%s%s\n\n%s",quest_status(q.state),recent ? "Changed since last review. Previous: " : "",recent ? quest_status(seen[q.num]) : "",description);
        rows[used++]=row;
    }
    return used;
}
void coop_play_quests_seen(void)
{
    coop_play_data_bind(); QuestLogbookEntry entries[2000]; int64_t pc=player_get_local_pc_obj(); if (!pc) return;
    int count=quest_get_logbook_data(pc,entries); for (int i=0;i<count;i++) if (entries[i].num>0 && entries[i].num<2000) seen[entries[i].num]=(uint8_t)entries[i].state;
    store();
}
unsigned coop_play_recipes(PlayRow* rows,const char* query,bool craftable)
{
    int64_t pc=player_get_local_pc_obj(); if (!pc) return 0;
    int ids[512]; unsigned count=recipes_known(pc,ids,SDL_arraysize(ids)),used=0;
    for (unsigned i=0;i<count;i++) {
        const char* name=schematic_ui_product_name(ids[i]); if (!matches(name,query)) continue;
        PlayRow row={.id=ids[i],.available=true}; snprintf(row.name,sizeof(row.name),"%s",name);
        SchematicInfo info={0}; schematic_ui_info_get(ids[i],&info);
        snprintf(row.detail,sizeof(row.detail),"%s | Produces %d\n",name,info.qty);
        const int* parts[]={info.item1,info.item2};
        for (unsigned part=0;part<2;part++) {
            char ingredient[128]="Unknown component"; bool found=false,expert=true;
            for (unsigned v=0;v<3;v++) {
                int64_t proto=sub_4685A0(parts[part][v]); if (!proto) continue;
                if (!v) coop_play_name(proto,ingredient,sizeof(ingredient));
                if (sub_462540(pc,proto,7)) {
                    found=true; coop_play_name(proto,ingredient,sizeof(ingredient));
                    int complexity=obj_field_int32_get(proto,OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY),discipline=obj_field_int32_get(proto,OBJ_F_ITEM_DISCIPLINE);
                    expert=complexity>=0 || (discipline>=0 && discipline<TECH_COUNT && tech_degree_level_get(pc,discipline)>=-(int64_t)complexity);
                    break;
                }
            }
            row.available &= found && expert;
            size_t n=strlen(row.detail); snprintf(row.detail+n,sizeof(row.detail)-n,"\n%s: %s%s",ingredient,found ? "present" : "MISSING",expert ? "" : " | insufficient technical expertise");
        }
        if (craftable && !row.available) continue;
        size_t n=strlen(row.detail); snprintf(row.detail+n,sizeof(row.detail)-n,"\n\n%s",row.available ? "Ready to craft using the original recipe." : "Collect missing ingredients or increase the listed expertise.");
        rows[used++]=row;
    }
    return used;
}
static bool sale_eligible(int64_t pc,int64_t item)
{
    if (!item || !obj_handle_is_valid(item) || !item_is_item(item) || !coop_play_junk(item,false)) return false;
    int64_t parent=0; if (!item_parent(item,&parent) || parent!=pc || IS_WEAR_INV_LOC(item_inventory_location_get(item))) return false;
    int type=obj_field_int32_get(item,OBJ_F_TYPE);
    unsigned flags=(unsigned)obj_field_int32_get(item,OBJ_F_ITEM_FLAGS);
    return type!=OBJ_TYPE_GOLD && type!=OBJ_TYPE_KEY && type!=OBJ_TYPE_KEY_RING && type!=OBJ_TYPE_WRITTEN && !(flags&(OIF_WONT_SELL|OIF_NO_DROP|OIF_HEXED));
}
const char* coop_play_bulk_status(void) { return bulk_status; }
void coop_play_bulk_start(void)
{
    if (bulk_phase) { bulk_phase=0; snprintf(bulk_status,sizeof(bulk_status),"Sale stopped."); return; }
    if (!inven_ui_play_merchant()) { snprintf(bulk_status,sizeof(bulk_status),"Open a merchant's barter window first, then return here to sell marked junk."); return; }
    bulk_sold=bulk_skipped=0; bulk_phase=1; bulk_deadline=SDL_GetTicks()+10000;
    snprintf(bulk_status,sizeof(bulk_status),"Selling eligible marked items. Click Sell again to stop.");
}
void coop_play_bulk_tick(void)
{
    if (!bulk_phase) return;
    int64_t pc=player_get_local_pc_obj(),npc=inven_ui_play_merchant();
    if (!pc || !npc || SDL_GetTicks()>bulk_deadline) { bulk_phase=0; snprintf(bulk_status,sizeof(bulk_status),"Sale stopped: merchant unavailable or request timed out."); return; }
    CoopGameplay* g=coop_runtime_gameplay(); CoopServices* services=g ? coop_gameplay_services(g) : NULL;
    if (services && coop_services_busy(services)) return;
    if (bulk_phase==1) {
        int64_t item=0;
        for (unsigned i=0;i<junk_count;i++) { int64_t candidate=obj_pool_perm_lookup(junk[i]); if (sale_eligible(pc,candidate)) { item=candidate; break; } }
        if (!item) { bulk_phase=0; snprintf(bulk_status,sizeof(bulk_status),"Sold %u item(s); %u refused. Equipped items, keys and documents were protected.",bulk_sold,bulk_skipped); return; }
        bulk_item=obj_get_id(item); bulk_deadline=SDL_GetTicks()+10000;
        if (services) {
            bulk_result=coop_services_last_result(services,NULL); coop_runtime_service_item(item,false); bulk_phase=2;
        } else {
            CoopServiceQuote quote; unsigned status=coop_engine_service_quote(pc,npc,item,3,&quote);
            if (!status) status=coop_engine_service_buy(pc,npc,item,&quote);
            if (!status) bulk_sold++; else bulk_skipped++;
            coop_play_junk(item,true); inven_ui_update(pc);
        }
    } else {
        unsigned status=0; uint32_t result=coop_services_last_result(services,&status);
        if (result==bulk_result) return;
        int64_t item=obj_pool_perm_lookup(bulk_item);
        if (bulk_phase==2 && !status && sale_eligible(pc,item)) {
            bulk_result=result; if (coop_services_buy(services,item)) { bulk_phase=3; return; }
        }
        if (bulk_phase==3 && !status) bulk_sold++; else bulk_skipped++;
        /* Use the saved identity even when the merchant consumed the item. */
        for (unsigned i=0;i<junk_count;i++) if (objid_is_equal(junk[i],bulk_item)) { junk[i]=junk[--junk_count]; store(); break; }
        bulk_phase=1;
    }
}
