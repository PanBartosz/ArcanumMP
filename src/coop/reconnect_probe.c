#include "coop/reconnect_probe.h"
#include "coop/bootstrap.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_journal.h"
#include "coop/engine_combat.h"
#include "coop/engine_magic.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/map.h"
#include "game/sector.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/teleport.h"
#include "game/tile.h"
#include "game/townmap.h"
#include "game/tile_script_list.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "game/gamelib.h"
#include "game/item.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/script.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/dialog_ui.h"
#include "ui/inven_ui.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { TAG=188910, PHASES=15 };
static CoopSession* held;
static int bindings[4]={-1,-1,-1,-1};
static DateTime frozen;
static int64_t destination;
static unsigned recovery_scripts;
static uint32_t page_crc(const CoopDialogueView* view)
{
    CoopDialogueView copy = *view;
    copy.conversation = copy.revision = 1;
    size_t size = 0;
    uint8_t* bytes = coop_dialogue_view_encode(&copy, &size);
    uint32_t crc = bytes ? coop_snapshot_crc(bytes, size) : 0;
    if (bytes) {
        static unsigned frame;
        char path[80]; snprintf(path, sizeof(path), "arcmp-reconnect-view-%u.bin", frame++);
        FILE* file = fopen(path, "wb");
        if (file) { fwrite(bytes, 1, size, file); fclose(file); }
    }
    free(bytes);
    return crc;
}
static int64_t pc(unsigned slot)
{
    int member=coop_session_is_host(held) ? coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity) : bindings[slot];
    return member<0 ? 0 : coop_campaign_object((unsigned)member);
}
static int64_t tagged(bool npc)
{
    int64_t obj,found=0; int it;
    if (obj_inst_first(&obj,&it)) do {
        int type=obj_field_int32_get(obj,OBJ_F_TYPE);
        if ((npc ? type==OBJ_TYPE_NPC : obj_type_is_item(type))
            && obj_field_int32_get(obj,npc ? OBJ_F_NPC_PAD_I_1 : OBJ_F_ITEM_PAD_I_1)==TAG+(npc ? 0 : 1)) {
            if (found) return 0; found=obj;
        }
    } while (obj_inst_next(&obj,&it));
    return found;
}
static bool prepare(void)
{
    int64_t obj,npc=0; int it;
    if (obj_inst_first(&obj,&it)) do {
        if (obj_field_int32_get(obj,OBJ_F_TYPE)!=OBJ_TYPE_NPC) continue;
        Script dialog={0}; obj_arrayfield_script_get(obj,OBJ_F_SCRIPTS_IDX,SAP_DIALOG,&dialog);
        if (dialog.num==1324) { npc=obj; obj_field_int32_set(npc,OBJ_F_NPC_PAD_I_1,TAG); }
        else object_flags_set(obj,OF_OFF);
        Script none={0}; obj_arrayfield_script_set(obj,OBJ_F_SCRIPTS_IDX,SAP_FIRST_HEARTBEAT,&none);
        obj_arrayfield_script_set(obj,OBJ_F_SCRIPTS_IDX,SAP_HEARTBEAT,&none);
    } while (obj_inst_next(&obj,&it));
    if (!npc) return false;
    int64_t origin=obj_field_int64_get(npc,OBJ_F_LOCATION);
    for (unsigned i=0;i<4;i++) sub_43E770(pc(i),location_make(location_get_x(origin)+2+(i&1)*2,location_get_y(origin)+2+(i>>1)*2),0,0);
    /* Controlled follower setup; panel actions below use ordinary ownership
     * requests. This fixture is not proof of original recruitment gates. */
    critter_leader_set(npc, pc(3));
    int64_t weapon=item_wield_get(pc(2),ITEM_INV_LOC_WEAPON); if (!weapon) return false;
    obj_field_int32_set(weapon,OBJ_F_ITEM_PAD_I_1,TAG+1); script_global_var_set(1999,551234);
    return true;
}
static bool queue_recovery_work(void)
{
    /* Private native one-shot tile scripts expose ordering without changing
     * installed campaign content. Their pending events cross the same native
     * save/load boundary as original arrivals. */
    script_global_var_set(1998, 0);
    int64_t anchor = obj_field_int64_get(pc(0), OBJ_F_LOCATION);
    unsigned count = 0;
    for (int dx = 8; dx < 32 && count < 2; dx++) {
        int64_t loc = location_make(location_get_x(anchor) + dx, location_get_y(anchor));
        Sector* sector;
        int64_t id = sector_id_from_loc(loc);
        if (!sector_lock(id, &sector)) return false;
        Script previous;
        bool occupied = tile_script_list_get(&sector->tile_scripts, tile_id_from_loc(loc), &previous);
        sector_unlock(id);
        if (occupied) continue;
        ScriptAction actions[] = {
            { .type = SAT_MULTIPLY, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1998, 1998, 10 } },
            { .type = SAT_ADD, .op_type = { SVT_GL_VAR, SVT_GL_VAR, SVT_NUMBER }, .op_value = { 1998, 1998, (int)count + 1 } },
            { .type = SAT_REMOVE_THIS_SCRIPT }, { .type = SAT_RETURN_AND_RUN_DEFAULT }
        };
        char path[100];
        snprintf(path, sizeof(path), "data/scr/%uarcmp-reconnect-arrival.scr", 999982 + count);
        uint8_t header[64] = { 0 };
        header[52] = header[56] = SDL_arraysize(actions);
        uint8_t payload[sizeof(header) + sizeof(ScriptCondition) * SDL_arraysize(actions)] = { 0 };
        memcpy(payload, header, sizeof(header));
        for (unsigned i = 0; i < SDL_arraysize(actions); i++) {
            ScriptCondition row = { .type = SCT_TRUE, .action = actions[i] };
            memcpy(payload + sizeof(header) + i * sizeof(row), &row, sizeof(row));
        }
        FILE* old = fopen(path, "rb");
        bool good;
        if (old) {
            uint8_t previous[sizeof(payload)];
            good = fread(previous, 1, sizeof(previous), old) == sizeof(previous) && fgetc(old) == EOF
                && !memcmp(previous, payload, sizeof(payload));
            fclose(old);
            if (good) recovery_scripts |= 1u << count;
        } else {
            FILE* file = fopen(path, "wb");
            if (!file) return false;
            recovery_scripts |= 1u << count;
            good = fwrite(payload, 1, sizeof(payload), file) == sizeof(payload);
            if (fclose(file)) good = false;
        }
        if (!good || !sector_lock(id, &sector)) return false;
        Script script = { .num = 999982 + (int)count };
        good = tile_script_list_set(&sector->tile_scripts, tile_id_from_loc(loc), &script);
        sector_unlock(id);
        if (!good) return false;
        TimeEvent event = { .type = TIMEEVENT_TYPE_COOP_ARRIVAL_ORDERED };
        event.params[0].object_value = pc(3 - count);
        event.params[1].location_value = loc;
        event.params[2].integer_value = map_current_map();
        event.params[3].integer_value = (int)count + 1;
        DateTime delay = { .milliseconds = (int)count + 1 };
        if (!timeevent_add_delay(&event, &delay)) return false;
        count++;
    }
    if (count != 2) return false;
    int64_t npc = tagged(true);
    dialog_ui_start_dialog(pc(3), npc, 1324, 0, 1);
    dialog_ui_start_dialog(pc(2), npc, 1324, 0, 1);
    fprintf(stderr, "ARCMP queued recovery work conversations=2 arrivals=2\n");
    return !coop_engine_dialogue_failed() && coop_runtime_entry_pending();
}
static bool announce(unsigned phase)
{
    char text[100];
    if (phase==1) for (unsigned slot=0;slot<4;slot++) {
        snprintf(text,sizeof(text),"reconnect-bind:%u:%d",slot,coop_campaign_slot_for_owner(coop_session_players(held)[slot].identity));
        if (!coop_session_chat(held,text)) return false;
    }
    if (phase==12) {
        int64_t actor=pc(3),from=obj_field_int64_get(actor,OBJ_F_LOCATION); bool found=false;
        for (int dy=-3;dy<=3 && !found;dy++) for (int dx=-3;dx<=3 && !found;dx++) {
            if (abs(dx)!=3 && abs(dy)!=3) continue;
            int64_t target=location_make(location_get_x(from)+dx,location_get_y(from)+dy);
            int8_t rotations[128]; PathCreateInfo path={.obj=actor,.from=from,.to=target,.max_rotations=128,.rotations=rotations};
            if (path_make(&path)>0) { destination=target; found=true; }
        }
        if (!found) return false;
        snprintf(text,sizeof(text),"reconnect-walk:%lld:%lld",(long long)location_get_x(destination),(long long)location_get_y(destination));
        if (!coop_session_chat(held,text)) return false;
    }
    snprintf(text,sizeof(text),"reconnect-phase:%u",phase); return coop_session_chat(held,text);
}
static bool image(unsigned phase,bool framebuffer)
{
    if (!framebuffer) return true;
    gamelib_redraw(); iso_redraw(); tig_window_display(); char name[64]; snprintf(name,sizeof(name),"arcmp-reconnect-%u.bmp",phase);
    return tig_video_screenshot_save(name)==TIG_OK;
}
static void retry_click(void)
{
    TigMessage m={.type=TIG_MESSAGE_MOUSE}; m.data.mouse.x=275; m.data.mouse.y=154;
    m.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN; tig_message_enqueue(&m);
    m.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP; tig_message_enqueue(&m);
}
void coop_reconnect_probe_cleanup(void)
{
    if (held) { coop_runtime_detach(); coop_session_destroy(held); held=NULL; }
    if (recovery_scripts) {
        if (recovery_scripts & 1) remove("data/scr/999982arcmp-reconnect-arrival.scr");
        if (recovery_scripts & 2) remove("data/scr/999983arcmp-reconnect-arrival.scr");
        recovery_scripts = 0;
    }
}
bool coop_reconnect_probe(int argc,char** argv)
{
    bool host=false,framebuffer=false; unsigned port=35746,player=4;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--coop-probe-host")) host=true;
        if (!strcmp(argv[i],"--coop-probe-framebuffer")) framebuffer=true;
        if (!strncmp(argv[i],"--coop-probe-port=",18)) port=(unsigned)atoi(argv[i]+18);
        if (!strncmp(argv[i],"--coop-probe-player=",20)) player=(unsigned)atoi(argv[i]+20);
    }
    if (!port || port>65535 || player>=4 || host!=(player==0)) return false;
    CoopConfig config={0}; snprintf(config.name,sizeof(config.name),"Reconnect player %u",player);
    FILE* content=fopen("arcmp-content.id","rb"); char hex[66]={0}; if (!content) return false;
    bool good=fread(hex,1,65,content)>=64; fclose(content);
    for (unsigned i=0;good && i<32;i++) { char pair[]={hex[i*2],hex[i*2+1],0},*end; config.content_id[i]=(uint8_t)strtoul(pair,&end,16); good=!*end; }
    if (!good || !coop_identity_load_or_create("arcmp-player.id",config.identity)) return false;
    held=coop_session_create(&config); if (!held) return false; coop_runtime_probe_enable();
    uint64_t start=SDL_GetTicks(),playing=0,chats=0,at=0,ended=0,wait_at=0;
    good=(host ? coop_session_host(held,(uint16_t)port,start) : coop_session_join(held,"127.0.0.1",(uint16_t)port,start)) && coop_runtime_attach(held,config.identity);
    fprintf(stderr,"ARCMP bootstrap connection host=%d port=%u\n",host,port); fflush(stderr);
    bool selected=false,initialized=false,sent=false,done=false,finishing=false,queued=false,frozen_known=false,freeze_sent=false,arrived=false,retry_clicked=false,shot=false;
    unsigned phase=0,completed=0,ack[4]={0},owned_slot=4,drops=0,rejoins=0,last_epoch=1,moves=0;
    uint32_t dialogue_crc[2]={0}; uint8_t local_guid[16]={0};
    uint32_t restored_crc = 0, restored_conversation = 0;
    unsigned restored_pages = 0;
    uint64_t restored_at = 0;
    bool recovery_queued = false;
    bool restored_choice_sent = false;
    bool opening_choice_sent = false;
    unsigned recovery_journal_pages = 0;
    uint32_t panel_crc = 0;
    unsigned panel_journal_pages = 0;
    uint64_t panel_seen_at = 0;
    bool panel_requested = false, panel_restored = false, panel_closed = false;
    while (good && SDL_GetTicks()-start<180000) {
        uint64_t now=SDL_GetTicks(); tig_ping(); coop_session_poll(held,now); coop_lobby_tick();
        if (coop_runtime_phase()==COOP_RUNTIME_FAILED) { fprintf(stderr,"ARCMP reconnect runtime: %s\n",coop_runtime_status()); good=false; break; }
        CoopGameplay* game=coop_runtime_gameplay();
        if (coop_runtime_phase()==COOP_RUNTIME_LOBBY && coop_session_phase(held)==COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name,sizeof(name),"Reconnect hero %u",player); selected=coop_session_character(held,16067+player,name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held,true);
            unsigned count=0; for (unsigned i=0;i<4;i++) count+=coop_session_players(held)[i].connected;
            if (host && count==4 && coop_session_all_ready(held)) good=coop_runtime_start();
        }
        if (game && coop_runtime_phase()==COOP_RUNTIME_PLAYING) {
            if (!playing) { playing=now; owned_slot=coop_session_local_slot(held); memcpy(local_guid,obj_get_id(player_get_local_pc_obj()).d.g.data,16); }
            if (coop_runtime_epoch()!=last_epoch) { last_epoch=coop_runtime_epoch(); rejoins++; }
            good &= coop_session_local_slot(held)==owned_slot && !memcmp(local_guid,obj_get_id(player_get_local_pc_obj()).d.g.data,16);
            const CoopDialogueView* d=coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && d->active && d->chooser==owned_slot) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (!initialized && host && !d->active && now-playing>1200) { initialized=true; good=prepare() && announce(1); }
        }
        if (chats>coop_session_chat_count(held)) chats=0;
        for (;chats<coop_session_chat_count(held);chats++) {
            const CoopChat* chat=coop_session_chat_at(held,chats); if (!chat) continue;
            unsigned value,slot,member,days,ms; long long x,y;
            if (!chat->slot && sscanf(chat->text,"reconnect-bind:%u:%u",&slot,&member)==2) { if (slot>=4 || member>=4) good=false; else bindings[slot]=(int)member; }
            if (!chat->slot && sscanf(chat->text,"reconnect-freeze:%u:%u",&days,&ms)==2) { frozen=(DateTime){.days=days,.milliseconds=ms}; frozen_known=true; }
            if (!chat->slot && sscanf(chat->text,"reconnect-walk:%lld:%lld",&x,&y)==2) destination=location_make(x,y);
            if (!chat->slot && !strcmp(chat->text,"reconnect-arrived")) arrived=true;
            if (!chat->slot && sscanf(chat->text,"reconnect-phase:%u",&value)==1 && value>phase) {
                if (value!=phase+1 || value>PHASES) { good=false; break; }
                phase=value; at=now; sent=done=freeze_sent=shot=false; wait_at=0;
                if (phase==3 || phase==9) frozen_known=false;
                fprintf(stderr,"ARCMP reconnect phase=%u slot=%u epoch=%u\n",phase,owned_slot,coop_runtime_epoch());
            }
            if (chat->slot<4 && sscanf(chat->text,"reconnect-done:%u",&value)==1) ack[chat->slot]=value;
        }
        CoopRuntimePhase state=coop_runtime_phase();
        if ((phase==4 || phase==10) && state==COOP_RUNTIME_RECONNECTING) {
            if (!wait_at) wait_at=now;
            DateTime current=sub_45A7C0(); good &= frozen_known && current.value==frozen.value;
            if (host && !shot && now-wait_at>450) { good &= image(phase,framebuffer); shot=true; }
            if (phase==4 && owned_slot==1 && !retry_clicked && now-wait_at>1500) {
                good &= image(40,framebuffer); retry_click(); retry_clicked=true;
            }
        }
        if (phase && !done && now-at>350) {
            bool live=game && state==COOP_RUNTIME_PLAYING;
            const CoopControlView* control=coop_controls_current(coop_runtime_controls());
            const CoopDialogueView* dialogue=game ? coop_dialogue_current(coop_gameplay_dialogue(game)) : NULL;
            int64_t item=tagged(false),parent=0; if (item) item_parent(item,&parent);
            bool ready=live;
            if (phase==1) ready &= item && parent==pc(2) && tagged(true) && script_global_var_get(1999)==551234;
            if (phase==2 || phase==13) {
                if (owned_slot==1 && !sent && live) { good=coop_runtime_talk(tagged(true)); sent=true; }
                ready &= dialogue && dialogue->active && dialogue->chooser==1;
                if (phase == 2 && ready) {
                    if (owned_slot == 1 && !opening_choice_sent && dialogue->option_count) {
                        good &= coop_engine_dialogue_submit(0);
                        opening_choice_sent = true;
                    }
                    ready &= dialogue->selected[0] != 0;
                }
                if (ready) {
                    dialogue_crc[phase==2 ? 0 : 1] = page_crc(dialogue);
                    good &= dialogue_crc[phase==2 ? 0 : 1] != 0;
                    if (phase == 2) recovery_journal_pages = coop_journal_count(coop_engine_journal());
                    if (phase==13) good &= image(13,framebuffer);
                }
            } else if (phase==3 || phase==9) {
                unsigned pauser=phase==3 ? 1 : 3;
                if (phase == 9) {
                    if (owned_slot == 3 && !panel_requested && live) {
                        good &= coop_runtime_follower_order(tagged(true), COOP_FOLLOW_INVENTORY, 0, 0, false);
                        panel_requested = true;
                    }
                    ready &= dialogue && dialogue->active && dialogue->mode == 3 && dialogue->chooser == 3;
                    if (ready && !panel_crc) {
                        panel_crc = page_crc(dialogue);
                        panel_journal_pages = coop_journal_count(coop_engine_journal());
                    }
                }
                if (owned_slot==pauser && !sent && ready) { good=coop_runtime_control(COOP_CONTROL_PAUSE,1,0,0); sent=true; }
                ready &= control && control->paused==(1u<<pauser);
                if (phase == 3 && host && ready && !recovery_queued) {
                    if (!queue_recovery_work()) { good = false; fprintf(stderr, "ARCMP could not prepare queued recovery work\n"); break; }
                    recovery_queued = true;
                }
                if (host && ready && !freeze_sent) {
                    frozen=sub_45A7C0(); char text[96]; snprintf(text,sizeof(text),"reconnect-freeze:%u:%u",frozen.days,frozen.milliseconds);
                    good=coop_session_chat(held,text); frozen_known=freeze_sent=true;
                }
                if (phase==3 && owned_slot==2 && ready && !queued) { good=coop_gameplay_item(game,COOP_ITEM_DROP,item,0,0); queued=true; }
                DateTime current=sub_45A7C0(); ready &= frozen_known && current.value==frozen.value && item && parent==pc(2);
            } else if (phase==4 || phase==10) {
                bool dropping=phase==4 ? owned_slot==1 : owned_slot==2 || owned_slot==3;
                if (dropping && !sent) {
                    coop_runtime_probe_retry_delay(phase==4 ? 5000 : owned_slot==3 ? 2500 : 1000);
                    good=coop_session_drop_transport(held); drops++; sent=true; ready=false;
                }
                ready &= coop_runtime_epoch()==(phase==4 ? 2u : 3u) && control && control->paused==(phase==4 ? 2u : 8u);
                DateTime current=sub_45A7C0(); ready &= frozen_known && current.value==frozen.value;
            } else if (phase==5) {
                ready &= coop_runtime_epoch()==2 && parent==pc(2) && dialogue && !dialogue->active
                    && script_global_var_get(1999)==551234 && !script_global_var_get(1998);
                if (host && ready) good &= coop_engine_dialogue_pending() && coop_runtime_entry_pending();
            }
            else if (phase==6 || phase==11) {
                if (host && !sent && live) { good=coop_runtime_control(COOP_CONTROL_RESUME_ALL,0,0,0); sent=true; }
                ready &= control && !control->paused;
                if (phase==6 && ready) {
                    if (dialogue && dialogue->active && dialogue->conversation != restored_conversation) {
                        static const unsigned order[] = { 1, 3, 2 };
                        good &= restored_pages < SDL_arraysize(order) && dialogue->chooser == order[restored_pages];
                        if (!restored_pages) {
                            restored_crc = page_crc(dialogue);
                            good &= restored_crc == dialogue_crc[0] && dialogue->selected[0]
                                && coop_journal_count(coop_engine_journal()) == recovery_journal_pages;
                        }
                        restored_pages++;
                        restored_conversation = dialogue->conversation; restored_at = now;
                        restored_choice_sent = false;
                        fprintf(stderr, "ARCMP recovered dialogue page=%u chooser=%u crc=%u arrivals=%d\n", restored_pages, dialogue->chooser, page_crc(dialogue), script_global_var_get(1998));
                    }
                    if (dialogue && dialogue->active && !restored_choice_sent && dialogue->chooser == owned_slot && now - restored_at > 500
                        && coop_dialogue_settled(coop_gameplay_dialogue(game))) {
                        good &= coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
                        restored_choice_sent = true;
                    }
                    ready &= restored_pages == 3 && dialogue && !dialogue->active && !coop_engine_dialogue_pending()
                        && script_global_var_get(1998) == 12;
                } else if (phase == 11 && ready) {
                    if (dialogue && dialogue->active && !panel_restored) {
                        good &= dialogue->mode == 3 && dialogue->chooser == 3 && page_crc(dialogue) == panel_crc
                            && coop_journal_count(coop_engine_journal()) == panel_journal_pages;
                        panel_restored = true; panel_seen_at = now;
                        good &= image(11, framebuffer);
                    }
                    if (panel_restored && owned_slot == 3 && !panel_closed && now - panel_seen_at > 500) {
                        inven_ui_destroy(); panel_closed = true;
                    }
                    ready &= panel_restored && dialogue && !dialogue->active && !coop_engine_dialogue_pending();
                }
            } else if (phase==7 || phase==8) {
                if (owned_slot==2 && !sent && live) { good=coop_runtime_item(phase==7 ? COOP_ITEM_DROP : COOP_ITEM_PICKUP,item,0,0); sent=true; }
                ready &= item && (phase==7 ? !parent : parent==pc(2)) && coop_items_settled(coop_gameplay_items(game));
            } else if (phase==12) {
                if (owned_slot==3 && !sent && live) { good=coop_runtime_move(destination,true); sent=true; moves++; }
                if (host && !arrived && location_dist(obj_field_int64_get(pc(3),OBJ_F_LOCATION),destination)<=1) { good=coop_session_chat(held,"reconnect-arrived"); arrived=true; }
                ready &= arrived && location_dist(obj_field_int64_get(pc(3),OBJ_F_LOCATION),destination)<=1;
            } else if (phase==14) {
                if (owned_slot==1 && !sent && dialogue && dialogue->active) { good=coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL); sent=true; }
                ready &= dialogue && !dialogue->active;
            } else if (phase==15) ready &= item && parent==pc(2) && script_global_var_get(1999)==551234
                && script_global_var_get(1998)==12 && restored_crc==dialogue_crc[0] && restored_pages==3 && panel_restored && rejoins==2;
            if (ready && good) {
                char text[64]; snprintf(text,sizeof(text),"reconnect-done:%u",phase); good=coop_session_chat(held,text); done=true; completed=phase;
            }
        }
        if (phase && !done && now-at>30000) { fprintf(stderr,"ARCMP reconnect timeout phase=%u slot=%u state=%u epoch=%u\n",phase,owned_slot,state,coop_runtime_epoch()); good=false; }
        if (host && done && !finishing && game) {
            bool all=true; for (unsigned i=0;i<4;i++) all &= ack[i]==phase;
            if (all) { if (phase<PHASES) good &= announce(phase+1); else { good &= coop_gameplay_finish(game); finishing=true; } }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended=now; if (now-ended>600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage m; while (tig_message_dequeue(&m)==TIG_OK) { if (m.type==TIG_MESSAGE_QUIT) good=false; else if (m.type==TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&m); }
        SDL_Delay(2);
    }
    CoopGameplay* game=coop_runtime_gameplay(); good &= game && coop_gameplay_finished(game) && completed==PHASES && rejoins==2;
    FILE* report=fopen("arcmp-reconnect-result.json","wb"); if (!report) good=false;
    if (report) {
        fprintf(report, "{\"verified\":%s,\"phases\":%u,\"epoch\":%u,\"rejoins\":%u,\"transport_drops\":%u,\"native_retry_click\":%s,\"resumed_moves\":%u,\"dialogue_crc\":[%u,%u],\"restored_crc\":%u,\"restored_pages\":%u,\"panel_restored\":%s,\"arrival_order\":%d,\"marker\":%d}\n",
            good ? "true" : "false", completed, coop_runtime_epoch(), rejoins, drops, retry_clicked ? "true" : "false", moves,
            dialogue_crc[0], dialogue_crc[1], restored_crc, restored_pages, panel_restored ? "true" : "false", script_global_var_get(1998), script_global_var_get(1999));
        if (fclose(report)) good = false;
    }
    fprintf(stderr,"ARCMP reconnect probe %s phases=%u\n",good?"PASS":"FAIL",completed); return good;
}

/* Independent combined fixture: native combat, rest, journey and transfer loss.
 * It does not repeat the ordinary dialogue/item reconnection sequence. */
static bool barrier_route(bool landing,int64_t* destination)
{
    int64_t sec=sector_id_from_loc(obj_field_int64_get(pc(2),OBJ_F_LOCATION)),mx,my;
    sector_limits_get(&mx,&my); int8_t rotations[256];
    for (int radius=landing?2:8;radius<(landing?16:24);radius++) for (int dir=0;dir<8;dir++) {
        static const int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
        int64_t x=(int64_t)SECTOR_X(sec)+radius*dx[dir],y=(int64_t)SECTOR_Y(sec)+radius*dy[dir];
        if (x<1 || y<1 || x>=mx || y>=my) continue;
        int64_t target=SECTOR_MAKE(x,y);
        if (sector_is_blocked(target) || (landing && townmap_get(target)!=TOWNMAP_NONE)) continue;
        WmapPathInfo path={.from=sec,.to=target,.max_rotations=256,.rotations=rotations};
        int steps=wmap_path_make(&path); if (steps<(landing?2:8) || steps>(landing?16:64)) continue;
        int64_t corner=sector_loc_from_id(target);
        *destination=location_make(location_get_x(corner)+32,location_get_y(corner)+32);
        if (landing) {
            bool found=false;
            for (int offset=0;offset<16 && !found;offset++) {
                int64_t anchor=location_make(location_get_x(*destination)+offset,location_get_y(*destination));
                if (tile_is_blocking(anchor,false)) continue;
                unsigned exits=0;
                for (int d=0;d<8;d++) {
                    int64_t to; location_in_dir(anchor,d,&to);
                    PathCreateInfo walk={.obj=pc(2),.from=anchor,.to=to,.max_rotations=256,.rotations=rotations};
                    if (path_make(&walk)>0) exits++;
                }
                if (exits>=4) { *destination=anchor; found=true; }
            }
            if (!found) continue;
        }
        return true;
    }
    return false;
}
static uint64_t strength_spell(void)
{
    CoopMagicView view; if (!coop_engine_magic_capture(&view)) return 0;
    for (unsigned i=0;i<view.count;i++) if (view.entries[i].spell==SPELL_STRENGTH_OF_EARTH
        && !memcmp(view.entries[i].owner,obj_get_id(pc(1)).d.g.data,16)) return view.entries[i].token;
    return 0;
}
bool coop_reconnect_barrier_probe(int argc,char** argv)
{
    bool host=false,framebuffer=false,tail=false,journey_tail=false; unsigned port=35747,player=4;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--coop-probe-host")) host=true;
        if (!strcmp(argv[i],"--coop-probe-framebuffer")) framebuffer=true;
        if (!strcmp(argv[i],"--coop-engine-probe=runtime-reconnect-actions")) tail=true;
        if (!strcmp(argv[i],"--coop-engine-probe=runtime-reconnect-journey")) tail=journey_tail=true;
        if (!strncmp(argv[i],"--coop-probe-port=",18)) port=(unsigned)atoi(argv[i]+18);
        if (!strncmp(argv[i],"--coop-probe-player=",20)) player=(unsigned)atoi(argv[i]+20);
    }
    if (!port || port>65535 || player>=4 || host!=(player==0)) return false;
    CoopConfig config={0}; snprintf(config.name,sizeof(config.name),"Barrier player %u",player);
    FILE* f=fopen("arcmp-content.id","rb"); char hex[66]={0}; if (!f) return false;
    bool good=fread(hex,1,65,f)>=64; fclose(f);
    for (unsigned i=0;good && i<32;i++) { char pair[]={hex[i*2],hex[i*2+1],0},*end; config.content_id[i]=(uint8_t)strtoul(pair,&end,16); good=!*end; }
    if (!good || !coop_identity_load_or_create("arcmp-player.id",config.identity)) return false;
    held=coop_session_create(&config); if (!held) return false; coop_runtime_probe_enable();
    uint64_t start=SDL_GetTicks(),playing=0,chats=0,at=0,ended=0;
    good=(host ? coop_session_host(held,(uint16_t)port,start) : coop_session_join(held,"127.0.0.1",(uint16_t)port,start)) && coop_runtime_attach(held,config.identity);
    fprintf(stderr,"ARCMP bootstrap connection host=%d port=%u\n",host,port); fflush(stderr);
    bool selected=false,initialized=false,sent=false,done=false,voted=false,finishing=false,dropped=false,waiting=false,clock_known=false,clock_sent=false;
    unsigned phase=0,completed=0,ack[4]={0},slot=4,drops=0,recoveries=0,rest_hours=0,journey_hours=0,item_owner=2;
    unsigned points=0,round=0,generation=0; uint8_t guid[16]={0}; uint64_t token=0;
    int strength=0; int64_t route=0; DateTime clock={0};
    while (good && SDL_GetTicks()-start<180000) {
        uint64_t now=SDL_GetTicks(); tig_ping(); coop_session_poll(held,now);
        if (phase==11 && slot==3 && !dropped && coop_runtime_phase()==COOP_RUNTIME_LOADING && coop_runtime_epoch()==(journey_tail?4u:8u)) {
            good=coop_session_drop_transport(held); dropped=true; drops++;
        }
        coop_runtime_tick(now);
        CoopRuntimePhase state=coop_runtime_phase(); CoopGameplay* game=coop_runtime_gameplay();
        if (state==COOP_RUNTIME_FAILED) { fprintf(stderr,"ARCMP barrier failed: %s\n",coop_runtime_status()); good=false; break; }
        if (state==COOP_RUNTIME_LOBBY && coop_session_phase(held)==COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name,sizeof(name),"Barrier hero %u",player); selected=coop_session_character(held,16067+player,name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held,true);
            unsigned count=0; for (unsigned i=0;i<4;i++) count+=coop_session_players(held)[i].connected;
            if (host && count==4 && coop_session_all_ready(held)) good=tail ? coop_runtime_recover_checkpoint() : coop_runtime_start();
        }
        if (state==COOP_RUNTIME_RECONNECTING) {
            waiting=true;
            if (host && !clock_sent) {
                clock=sub_45A7C0(); char text[100]; snprintf(text,sizeof(text),"barrier-clock:%u:%u",clock.days,clock.milliseconds);
                good &= coop_session_chat(held,text); clock_sent=clock_known=true;
                good &= image(100+phase,framebuffer);
            }
            if (host && sub_45A7C0().value!=clock.value) good=false;
        }
        if (game && state==COOP_RUNTIME_PLAYING) {
            if (!playing) { playing=now; slot=coop_session_local_slot(held); memcpy(guid,obj_get_id(player_get_local_pc_obj()).d.g.data,16); }
            good &= coop_session_local_slot(held)==slot && !memcmp(guid,obj_get_id(player_get_local_pc_obj()).d.g.data,16);
            if (waiting) { recoveries++; waiting=false; }
            const CoopDialogueView* dialogue=coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && dialogue->active && dialogue->chooser==slot) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (host && !initialized && !dialogue->active && now-playing>1200) {
                initialized=true; good=tail || prepare();
                for (unsigned i=0;i<4;i++) {
                    int64_t actor=pc(i); stat_base_set(actor,STAT_DEXTERITY,20);
                    if (!tail) {
                        stat_base_set(actor,STAT_INTELLIGENCE,20); stat_base_set(actor,STAT_WILLPOWER,20);
                        obj_field_int32_set(actor,OBJ_F_HP_PTS,200); obj_field_int32_set(actor,OBJ_F_HP_DAMAGE,80);
                        critter_fatigue_pts_set(actor,200); critter_fatigue_damage_set(actor,0);
                    }
                    char text[80]; snprintf(text,sizeof(text),"barrier-bind:%u:%d",i,coop_campaign_slot_for_owner(coop_session_players(held)[i].identity)); good &= coop_session_chat(held,text);
                }
                good &= spell_add(pc(1),SPELL_STRENGTH_OF_EARTH,true); strength=stat_level_get(pc(1),STAT_STRENGTH);
                if (tail) {
                    int64_t parent=0; item_parent(tagged(false),&parent);
                    int member=coop_campaign_slot_for_object(parent); item_owner=4;
                    if (member>=0) for (unsigned i=0;i<4;i++)
                        if (!memcmp(coop_campaign_member((unsigned)member)->owner,coop_session_players(held)[i].identity,16)) item_owner=i;
                    good &= item_owner<4;
                }
                char text[80]; snprintf(text,sizeof(text),"barrier-strength:%d",strength); good &= coop_session_chat(held,text);
                snprintf(text,sizeof(text),"barrier-item-owner:%u",item_owner); good &= coop_session_chat(held,text);
                if (journey_tail) good &= coop_runtime_control(COOP_CONTROL_PAUSE,1,0,0);
                good &= coop_session_chat(held,journey_tail ? "barrier-phase:9" : tail ? "barrier-phase:12" : "barrier-phase:1");
            }
        }
        if (chats>coop_session_chat_count(held)) chats=0;
        for (;chats<coop_session_chat_count(held);chats++) {
            const CoopChat* chat=coop_session_chat_at(held,chats); if (!chat) continue;
            unsigned value,owner,member,days,ms; long long x,y; unsigned long long spell;
            if (!chat->slot && sscanf(chat->text,"barrier-bind:%u:%u",&owner,&member)==2) { if (owner>=4 || member>=4) good=false; else bindings[owner]=(int)member; }
            if (!chat->slot && sscanf(chat->text,"barrier-strength:%d",&strength)==1) { }
            if (!chat->slot && sscanf(chat->text,"barrier-item-owner:%u",&item_owner)==1) { }
            if (!chat->slot && sscanf(chat->text,"barrier-spell:%llu",&spell)==1) token=spell;
            if (!chat->slot && sscanf(chat->text,"barrier-turn:%u:%u:%u",&points,&round,&generation)==3) { }
            if (!chat->slot && sscanf(chat->text,"barrier-route:%lld:%lld",&x,&y)==2) route=location_make(x,y);
            if (!chat->slot && sscanf(chat->text,"barrier-clock:%u:%u",&days,&ms)==2) { clock=(DateTime){.days=days,.milliseconds=ms}; clock_known=true; }
            if (!chat->slot && sscanf(chat->text,"barrier-phase:%u",&value)==1 && value>phase) {
                if ((value!=phase+1 && !(tail && !phase && value==(journey_tail?9u:12u))) || value>14) { good=false; break; }
                phase=value; at=now; sent=done=voted=dropped=clock_sent=clock_known=false;
                fprintf(stderr,"ARCMP barrier phase=%u slot=%u epoch=%u\n",phase,slot,coop_runtime_epoch());
            }
            if (chat->slot<4 && sscanf(chat->text,"barrier-done:%u",&value)==1) ack[chat->slot]=value;
        }
        const CoopControlView* control=coop_controls_current(coop_runtime_controls());
        if (phase && !done && now-at>350) {
            bool live=game && state==COOP_RUNTIME_PLAYING,ready=live;
            if (phase==1) ready &= strength && spell_is_known(pc(1),SPELL_STRENGTH_OF_EARTH);
            else if (phase==2) {
                if (slot==1 && !sent && live) { TargetDescriptor target; target_descriptor_set_obj(&target,pc(1)); good=coop_runtime_cast(pc(1),SPELL_STRENGTH_OF_EARTH,&target,false); sent=true; }
                uint64_t current=strength_spell(); ready &= current && stat_level_get(pc(1),STAT_STRENGTH)>strength;
                if (host && ready && !sent) { char text[80]; snprintf(text,sizeof(text),"barrier-spell:%llu",(unsigned long long)current); good=coop_session_chat(held,text); sent=true; }
            } else if (phase==3) {
                if (host && !sent && live) {
                    good=sub_4B6C90(false); combat_critter_activate_combat_mode(pc(1)); good &= sub_4B6C90(true);
                    combat_turn_based_whos_turn_set(pc(1)); good &= coop_runtime_control(COOP_CONTROL_PAUSE,1,0,0);
                    CoopCombatState turn; good &= coop_engine_combat_capture(&turn);
                    char text[100]; snprintf(text,sizeof(text),"barrier-turn:%u:%u:%u",turn.points,turn.round,turn.generation); good &= coop_session_chat(held,text); sent=true;
                }
                ready &= combat_turn_based_is_active() && combat_turn_based_whos_turn_get()==pc(1) && control->paused==1 && generation;
            } else if (phase==4) {
                if (slot==1 && !dropped && live) { good=coop_session_drop_transport(held); dropped=true; drops++; ready=false; }
                CoopCombatState turn; good &= coop_engine_combat_capture(&turn);
                ready &= coop_runtime_epoch()==2 && control->paused==1 && turn.active && turn.points==points && turn.round==round && turn.generation==generation
                    && combat_turn_based_whos_turn_get()==pc(1) && strength_spell()==token && stat_level_get(pc(1),STAT_STRENGTH)>strength;
                if (ready) good &= image(104,framebuffer);
            } else if (phase==5) {
                if (host && control->paused && !sent && live) { good=coop_runtime_control(COOP_CONTROL_RESUME_ALL,0,0,0); sent=true; }
                if (slot==1 && !control->paused && !sent && live) { good=coop_runtime_combat(COOP_COMBAT_END_TURN,0,0); sent=true; }
                ready &= !control->paused && combat_turn_generation_get()>generation;
            } else if (phase==6) {
                if (host && !sent && live) { good=sub_4B6C90(false); for (unsigned i=0;i<4;i++) combat_critter_deactivate_combat_mode(pc(i)); sent=true; }
                if (slot==1 && !sent && live && !combat_turn_based_is_active()) { good=coop_runtime_cancel_spell(strength_spell()); sent=true; }
                bool cleared=!combat_turn_based_is_active() && !strength_spell() && stat_level_get(pc(1),STAT_STRENGTH)==strength;
                if (host && cleared && !control->paused) good &= coop_runtime_control(COOP_CONTROL_PAUSE,1,0,0);
                ready &= cleared && control->paused==1;
            } else if (phase==7 || phase==10) {
                bool rest=phase==7;
                if (slot==2 && !sent && live) { good=rest ? coop_runtime_control(COOP_CONTROL_REST,4,0,0) : coop_runtime_journey(route); sent=true; }
                if (control->phase==COOP_CONTROL_VOTING && !voted) { good &= coop_runtime_control(COOP_CONTROL_VOTE,1,control->proposal,0); voted=true; }
                if (slot==(rest?3u:2u) && !dropped && control->phase==(rest?COOP_CONTROL_RESTING:COOP_CONTROL_JOURNEY) && control->completed>=(rest?2u:1u)) {
                    good &= coop_session_drop_transport(held); dropped=true; drops++; ready=false;
                }
                ready &= coop_runtime_epoch()==(rest?4u:journey_tail?3u:7u) && control->phase==COOP_CONTROL_IDLE && control->notice==COOP_CONTROL_INTERRUPTED
                    && control->action==(rest?COOP_CONTROL_REST:COOP_CONTROL_TRAVEL) && control->completed>=(rest?2u:1u) && control->paused==1;
                if (ready && clock_known) ready &= sub_45A7C0().value==clock.value;
                if (ready) {
                    if (rest) rest_hours=control->completed; else journey_hours=control->completed;
                    for (unsigned i=0;i<4;i++) good &= !(obj_field_int32_get(pc(i),OBJ_F_FLAGS)&OF_OFF);
                    /* Native town waiting advances time without healing. */
                    if (rest) good &= control->completed<24 && object_hp_damage_get(pc(1))<=80;
                    good &= image(100+phase,framebuffer);
                }
            } else if (phase==8) {
                if (host && !sent && live) { good=barrier_route(true,&route); TeleportData travel={.obj=pc(2),.map=map_current_map(),.loc=route,.flags=TELEPORT_COOP_PARTY}; good &= teleport_do(&travel); sent=true; }
                ready &= coop_runtime_epoch()==5 && townmap_get(sector_id_from_loc(obj_field_int64_get(pc(2),OBJ_F_LOCATION)))==TOWNMAP_NONE;
            } else if (phase==9) {
                if (host && !sent && live) { good=barrier_route(false,&route); char text[100]; snprintf(text,sizeof(text),"barrier-route:%lld:%lld",(long long)location_get_x(route),(long long)location_get_y(route)); good &= coop_session_chat(held,text); sent=true; }
                ready &= route!=0 && control->paused==1;
            } else if (phase==11) {
                if (host && !sent && live) { good=coop_runtime_control(COOP_CONTROL_SAVE,0,0,0); sent=true; }
                ready &= coop_runtime_epoch()==(journey_tail?5u:9u) && control->paused==1 && control->notice==COOP_CONTROL_INTERRUPTED && control->action==COOP_CONTROL_SAVE;
                if (ready) good &= image(111,framebuffer);
            } else if (phase==12) {
                if (host && !sent && live) {
                    /* The wilderness loader brings in new encounter NPCs.
                     * Keep this transfer fixture's original no-retaliation
                     * policy when checking an ordinary item action afterward. */
                    int64_t obj; int iterator;
                    if (obj_inst_first(&obj,&iterator)) do {
                        if (obj_field_int32_get(obj,OBJ_F_TYPE)==OBJ_TYPE_NPC) object_flags_set(obj,OF_OFF);
                    } while (obj_inst_next(&obj,&iterator));
                    good=sub_4B6C90(false);
                    for (unsigned i=0;i<4;i++) combat_critter_deactivate_combat_mode(pc(i));
                    good &= coop_runtime_control(COOP_CONTROL_RESUME_ALL,0,0,0); sent=true;
                }
                ready &= !control->paused && !combat_turn_based_is_active();
            } else if (phase==13 || phase==14) {
                int64_t item=tagged(false),parent=0; if (item) item_parent(item,&parent);
                if (slot==item_owner && !sent && live) { good=coop_runtime_item(phase==13?COOP_ITEM_DROP:COOP_ITEM_PICKUP,item,0,0); sent=true; }
                ready &= item && (phase==13 ? !parent : parent==pc(item_owner)) && coop_items_settled(coop_gameplay_items(game));
                if (phase==14) ready &= recoveries==(journey_tail?2u:tail?0u:4u) && script_global_var_get(1999)==551234;
            }
            if (ready && good) { char text[80]; snprintf(text,sizeof(text),"barrier-done:%u",phase); good=coop_session_chat(held,text); done=true; completed=phase; }
        }
        if (phase && !done && now-at>35000) { fprintf(stderr,"ARCMP barrier timeout phase=%u slot=%u state=%u epoch=%u control=%u notice=%u hours=%u\n",phase,slot,state,coop_runtime_epoch(),control->phase,control->notice,control->completed); good=false; }
        if (host && done && !finishing && game) {
            bool all=true; for (unsigned i=0;i<4;i++) all &= ack[i]==phase;
            if (all) { if (phase<14) { char text[80]; snprintf(text,sizeof(text),"barrier-phase:%u",phase+1); good &= coop_session_chat(held,text); } else { good &= coop_gameplay_finish(game); finishing=true; } }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended=now; if (now-ended>600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display(); TigMessage message;
        while (tig_message_dequeue(&message)==TIG_OK) { if (message.type==TIG_MESSAGE_QUIT) good=false; else if (message.type==TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message); }
        SDL_Delay(2);
    }
    CoopGameplay* game=coop_runtime_gameplay(); good &= game && coop_gameplay_finished(game) && completed==14 && recoveries==(journey_tail?2u:tail?0u:4u);
    f=fopen("arcmp-barrier-result.json","wb"); if (!f) good=false;
    if (f) { fprintf(f,"{\"verified\":%s,\"phases\":%u,\"epoch\":%u,\"recoveries\":%u,\"transport_drops\":%u,\"combat_points\":%u,\"combat_round\":%u,\"combat_generation\":%u,\"spell_token\":%llu,\"rest_hours\":%u,\"journey_hours\":%u}\n",good?"true":"false",completed,coop_runtime_epoch(),recoveries,drops,points,round,generation,(unsigned long long)token,rest_hours,journey_hours); if (fclose(f)) good=false; }
    fprintf(stderr,"ARCMP barrier probe %s phases=%u\n",good?"PASS":"FAIL",completed); return good;
}
