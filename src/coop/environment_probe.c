#include "coop/environment_probe.h"
#include "coop/campaign.h"
#include "coop/engine_dialogue.h"
#include "coop/engine_environment.h"
#include "coop/lobby_ui.h"
#include "coop/runtime.h"
#include "game/gamelib.h"
#include "game/gsound.h"
#include "game/light.h"
#include "game/light_scheme.h"
#include "game/map.h"
#include "game/mes.h"
#include "game/obj.h"
#include "game/obj_pool.h"
#include "game/object.h"
#include "game/player.h"
#include "game/script.h"
#include "game/snd.h"
#include "game/timeevent.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "tig/color.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PHASES = 14, TAG = 188899 };
static CoopSession* held;
static int music[2], ambient, volume, effects_volume;
static bool script_written;
static CoopEnvironmentState saved;
static const char script_path[] = "data/scr/999996arcmp-environment.scr";
static int64_t speaker(void)
{
    int64_t obj; int it;
    if (obj_inst_first(&obj,&it)) do {
        if (obj_field_int32_get(obj,OBJ_F_TYPE)==OBJ_TYPE_NPC && obj_field_int32_get(obj,OBJ_F_NPC_PAD_I_1)==TAG) return obj;
    } while (obj_inst_next(&obj,&it));
    return 0;
}
static bool prepare(void)
{
    int64_t obj; int it; bool first=true;
    if (obj_inst_first(&obj,&it)) do {
        if (obj_field_int32_get(obj,OBJ_F_TYPE)!=OBJ_TYPE_NPC) continue;
        if (first) { obj_field_int32_set(obj,OBJ_F_NPC_PAD_I_1,TAG); first=false; }
        object_flags_set(obj,OF_OFF);
        Script none={0}; for (int sap=0;sap<=SAP_CRITICAL_MISS;sap++) obj_arrayfield_script_set(obj,OBJ_F_SCRIPTS_IDX,sap,&none);
    } while (obj_inst_next(&obj,&it));
    mes_file_handle_t index,list;
    if (!mes_load("sound\\SchemeIndex.mes",&index)) return false;
    if (!mes_load("sound\\SchemeList.mes",&list)) { mes_unload(index); return false; }
    for (int i=0;i<mes_entries_count(index);i++) {
        MesFileEntry entry; if (!mes_get_entry(index,i,&entry) || entry.num<=0) continue;
        char* hash=strchr(entry.str,'#'); if (!hash) continue;
        int id=entry.num,base=atoi(hash+1); bool loop=false,noise=false,any_song=false;
        for (int j=0;j<100;j++) {
            MesFileEntry sound={.num=base+j}; if (!mes_search(list,&sound)) continue;
            bool song=strstr(sound.str,"/loop") || strstr(sound.str,"/anchor") || strstr(sound.str,"/over");
            any_song |= song;
            if (strstr(sound.str,"/loop") && !strstr(sound.str,"/time:")) loop=true;
            if (!song) noise=true;
        }
        if (loop && !music[0]) music[0]=id;
        else if (loop && !music[1]) music[1]=id;
        if (!any_song && noise && !ambient) ambient=id;
    }
    mes_unload(index); mes_unload(list);
    fprintf(stderr,"ARCMP environment retail schemes=%d,%d ambient=%d\n",music[0],music[1],ambient);
    return !first && music[0] && music[1] && ambient;
}
static void le32(uint8_t* p,uint32_t value)
{
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(i*8));
}
static bool script_scheme(void)
{
    uint8_t bytes[196]={0}; memcpy(bytes+8,"ARCMP private environment fixture",32);
    le32(bytes+52,1); le32(bytes+56,1); le32(bytes+108,SAT_PLAY_SOUND_SCHEME);
    bytes[112]=bytes[113]=SVT_NUMBER; le32(bytes+120,music[1]); le32(bytes+124,ambient);
    FILE* file=fopen(script_path,"wb"); if (!file) return false;
    bool good=fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes); if (fclose(file)) good=false;
    script_written=true;
    Script script={.num=999996}; ScriptInvocation call={.script=&script,.triggerer_obj=player_get_local_pc_obj(),.attachment_point=SAP_USE};
    if (!good) return false;
    /* The interpreter returns whether native default handling should run;
     * reaching EOF normally returns false after executing the action. */
    script_execute(&call);
    CoopEnvironmentState state={0}; gsound_campaign_state_get(&state);
    return (state.schemes[0]==(uint32_t)music[1] && state.schemes[1]==(uint32_t)ambient)
        || (state.schemes[1]==(uint32_t)music[1] && state.schemes[0]==(uint32_t)ambient);
}
static void hour(int target)
{
    DateTime game,animation; timeevent_campaign_clock_get(&game,&animation);
    game.milliseconds=(uint32_t)target*3600000; animation=game;
    timeevent_campaign_clock_restore(&game,&animation); light_scheme_set_hour(target);
}
static bool announce(unsigned phase)
{
    if (phase==1) { hour(12); gsound_play_scheme(music[0],ambient); }
    if (phase==2) hour(23);
    if (phase==3) light_set_colors(tig_color_make(92,40,64),tig_color_make(58,32,92));
    if (phase==4 && !script_scheme()) return false;
    if (phase==5) gsound_start_combat_music(speaker());
    if (phase==6) gsound_play_scheme(music[0],ambient);
    if (phase==8) { gsound_stop_combat_music(player_get_local_pc_obj()); hour(12); gsound_play_scheme(music[1],ambient); }
    if (phase==10) gsound_stop_combat_music(player_get_local_pc_obj());
    if (phase==12) gsound_play_scheme(0,0);
    CoopEnvironmentState state;
    if (!coop_engine_environment_capture(&state)) return false;
    if (phase==7) saved=state;
    if (phase==9) state=saved;
    uint8_t bytes[COOP_ENVIRONMENT_BYTES]; if (!coop_environment_encode(&state,bytes)) return false;
    char text[COOP_ENVIRONMENT_BYTES * 2 + 40];
    int n = snprintf(text, sizeof(text), "environment-state:%u:", phase);
    for (unsigned i=0;i<sizeof(bytes);i++) n+=snprintf(text+n,sizeof(text)-n,"%02x",bytes[i]);
    return coop_session_chat(held,text);
}
void coop_environment_probe_cleanup(void)
{
    if (held) { coop_runtime_detach(); coop_session_destroy(held); held=NULL; }
    settings_set_value(&settings,MUSIC_VOLUME_KEY,volume);
    settings_set_value(&settings,EFFECTS_VOLUME_KEY,effects_volume);
    tig_sound_probe_enable(false);
    if (script_written) { remove(script_path); script_written=false; }
}
bool coop_environment_probe(int argc,char** argv)
{
    bool host=false,framebuffer=false,ui_tail=false; unsigned port=35745,player=4;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--coop-probe-host")) host=true;
        if (!strcmp(argv[i],"--coop-probe-framebuffer")) framebuffer=true;
        if (!strcmp(argv[i],"--coop-engine-probe=runtime-environment-ui")) ui_tail=true;
        if (!strncmp(argv[i],"--coop-probe-port=",18)) port=(unsigned)atoi(argv[i]+18);
        if (!strncmp(argv[i],"--coop-probe-player=",20)) player=(unsigned)atoi(argv[i]+20);
    }
    if (!port || port>65535 || player>=4 || host!=(player==0)) return false;
    CoopConfig config={0}; snprintf(config.name,sizeof(config.name),"Environment player %u",player);
    FILE* content=fopen("arcmp-content.id","rb"); char hex[66]={0}; if (!content) return false;
    bool good=fread(hex,1,65,content)>=64; fclose(content);
    for (unsigned i=0;good && i<32;i++) { char pair[]={hex[i*2],hex[i*2+1],0},*end; config.content_id[i]=(uint8_t)strtoul(pair,&end,16); good=!*end; }
    good=good && coop_identity_load_or_create("arcmp-player.id",config.identity);
    held=good ? coop_session_create(&config) : NULL; if (!held) return false;
    volume=settings_get_value(&settings,MUSIC_VOLUME_KEY); settings_set_value(&settings,MUSIC_VOLUME_KEY,10); tig_sound_probe_enable(true);
    effects_volume=settings_get_value(&settings,EFFECTS_VOLUME_KEY); settings_set_value(&settings,EFFECTS_VOLUME_KEY,10);
    uint64_t start=SDL_GetTicks(),playing=0,chats=0,at=0,ended=0,measure_at=0,instance=0;
    uint64_t frames=0; double energy=0; TigSoundSignal before={0}; tig_sound_handle_t measured=TIG_SOUND_HANDLE_INVALID;
    good=(host ? coop_session_host(held,(uint16_t)port,start) : coop_session_join(held,"127.0.0.1",(uint16_t)port,start)) && coop_runtime_attach(held,config.identity);
    fprintf(stderr,"ARCMP bootstrap connection host=%d port=%u\n",host,port); fflush(stderr);
    bool selected=false,initialized=false,sent=false,done=false,finishing=false,voted=false,corrupted=false,locked=false;
    unsigned phase=0,completed=0,ack[4]={0},baseline=0,music_checks=0,lock_step=0,repair_scheme=0;
    CoopEnvironmentState expected={0};
    unsigned sample_step=0; uint64_t stream_frames=0,effect_frames=0,sample_instance=0;
    tig_sound_handle_t sample_handle=TIG_SOUND_HANDLE_INVALID;
    while (good && SDL_GetTicks()-start<180000) {
        uint64_t now=SDL_GetTicks(); tig_ping(); coop_session_poll(held,now); coop_lobby_tick();
        if (coop_runtime_phase()==COOP_RUNTIME_FAILED) { fprintf(stderr,"ARCMP environment runtime: %s\n",coop_runtime_status()); good=false; break; }
        CoopGameplay* game=coop_runtime_gameplay();
        if (coop_runtime_phase()==COOP_RUNTIME_LOBBY && coop_session_phase(held)==COOP_LOBBY) {
            if (!selected) { char name[32]; snprintf(name,sizeof(name),"Environment hero %u",player); selected=coop_session_character(held,16067+player,name); }
            else if (coop_session_players(held)[coop_session_local_slot(held)].prototype && !coop_session_players(held)[coop_session_local_slot(held)].ready) coop_session_ready(held,true);
            unsigned count=0; for (unsigned i=0;i<4;i++) count+=coop_session_players(held)[i].connected;
            if (host && count==4 && coop_session_all_ready(held)) good=ui_tail ? coop_runtime_start_checkpoint() : coop_runtime_start();
        }
        if (game && coop_runtime_phase()==COOP_RUNTIME_PLAYING) {
            if (!playing) playing=now;
            const CoopDialogueView* d=coop_dialogue_current(coop_gameplay_dialogue(game));
            if (!phase && d->active && d->chooser==coop_session_local_slot(held)) coop_engine_dialogue_submit(COOP_DIALOGUE_CANCEL);
            if (!initialized && host && !d->active && now-playing>1200) { initialized=true; good=(ui_tail || prepare()) && announce(ui_tail ? 10 : 1); }
        }
        for (;chats<coop_session_chat_count(held);chats++) {
            const CoopChat* chat=coop_session_chat_at(held,chats); unsigned value; int offset;
            if (!chat->slot && sscanf(chat->text,"environment-state:%u:%n",&value,&offset)==1 && value==(!phase && ui_tail ? 10 : phase+1)) {
                uint8_t bytes[COOP_ENVIRONMENT_BYTES]; const char* p=chat->text+offset;
                if (strlen(p)!=sizeof(bytes)*2) { good=false; break; }
                for (unsigned i=0;i<sizeof(bytes);i++) { char pair[]={p[i*2],p[i*2+1],0},*end; bytes[i]=(uint8_t)strtoul(pair,&end,16); if (*end) good=false; }
                good &= coop_environment_decode(bytes,sizeof(bytes),&expected);
                if (value==4 || (ui_tail && value==10)) repair_scheme=expected.schemes[1];
                phase=value; at=now; sent=done=voted=false; baseline=coop_runtime_epoch(); measure_at=instance=0; lock_step=0;
                fprintf(stderr,"ARCMP environment phase=%u slot=%u\n",phase,coop_session_local_slot(held));
            }
            if (chat->slot<4 && sscanf(chat->text,"environment-done:%u",&value)==1) ack[chat->slot]=value;
        }
        CoopControls* controls=coop_runtime_controls(); const CoopControlView* view=coop_controls_current(controls);
        uint8_t slot=coop_session_local_slot(held);
        if (phase && !done && game && coop_runtime_phase()==COOP_RUNTIME_PLAYING && now-at>350) {
            bool ready=true;
            if (phase==7 || phase==9) {
                unsigned acting=phase==7 ? 0 : 1;
                if (slot==acting && !sent) { good=coop_runtime_control(phase==7 ? COOP_CONTROL_SAVE : COOP_CONTROL_LOAD,0,0,0); sent=true; }
                if (phase==9 && view->phase==COOP_CONTROL_VOTING && slot!=acting && !voted) { good=coop_runtime_control(COOP_CONTROL_VOTE,1,view->proposal,0); voted=true; }
                ready &= coop_runtime_epoch()==baseline+1 && view->epoch==coop_runtime_epoch() && view->phase==COOP_CONTROL_IDLE && view->notice==COOP_CONTROL_OK;
            }
            if (phase==11 && !lock_step) { gsound_lock(); gsound_lock(); locked=true; lock_step=1; ready=false; }
            if (phase==11 && lock_step==1) {
                tig_sound_handle_t handle; ready=false;
                if (gsound_campaign_music(&handle,false)) good=false;
                if (now-at>1000) { gsound_unlock(); gsound_unlock(); locked=false; lock_step=2; }
            }
            if (phase==13 && !host && !corrupted) {
                light_set_colors(tig_color_make(255,255,255),tig_color_make(255,255,255));
                gsound_play_scheme((int)repair_scheme,0); corrupted=true; ready=false;
            }
            if (phase==14) {
                ready=false;
                if (!sample_step) {
                    char path[TIG_MAX_PATH]={0}; gsound_resolve_path(SND_INTERFACE_BOOK_PAGE_TURN,path,sizeof(path));
                    sample_handle=gsound_play_music_once(path,0); sample_instance=tig_sound_instance(sample_handle);
                    good &= sample_instance!=0; sample_step=1;
                } else if (sample_step==1 || sample_step==2) {
                    if (tig_sound_instance(sample_handle)==sample_instance) {
                        TigSoundSignal signal={0}; good &= tig_sound_probe_signal(sample_handle,&signal);
                        if (sample_step==1) stream_frames=signal.frames; else effect_frames=signal.frames;
                    } else if (sample_step==1) {
                        good &= stream_frames>0;
                        sample_handle=gsound_play_sfx(SND_INTERFACE_BOOK_PAGE_TURN,1); sample_instance=tig_sound_instance(sample_handle);
                        good &= sample_instance!=0; sample_step=2;
                    } else { good &= effect_frames>0; sample_step=3; }
                } else ready=true;
            }
            CoopEnvironmentState state; uint8_t actual[COOP_ENVIRONMENT_BYTES],want[COOP_ENVIRONMENT_BYTES];
            ready &= coop_engine_environment_capture(&state) && coop_environment_encode(&state,actual) && coop_environment_encode(&expected,want) && !memcmp(actual,want,sizeof(actual));
            bool check_music=phase==1 || phase==4 || phase==5 || phase==6 || phase==9 || phase==10 || phase==11;
            if (ready && check_music) {
                tig_sound_handle_t handle;
                if (!gsound_campaign_music(&handle,expected.combat) || !tig_sound_is_playing(handle)) ready=false;
                else if (!measure_at) { measured=handle; instance=tig_sound_instance(handle); good &= tig_sound_probe_signal(handle,&before); measure_at=now; ready=false; }
                else if (now-measure_at<700) ready=false;
                else {
                    TigSoundSignal after; good &= handle==measured && tig_sound_instance(handle)==instance && tig_sound_probe_signal(handle,&after);
                    if (good && after.frames>before.frames+1000 && after.left+after.right>before.left+before.right) {
                        frames+=after.frames-before.frames; energy+=after.left+after.right-before.left-before.right; music_checks++;
                    } else ready=false;
                }
            }
            if (ready && (phase!=11 || lock_step==2)) {
                if (ui_tail) good &= object_hp_current(player_get_local_pc_obj())>0;
                if (framebuffer && (phase==1 || phase==2 || phase==9 || (ui_tail && (phase==10 || phase==14)))) {
                    gamelib_redraw(); iso_redraw(); tig_window_display(); char name[64]; snprintf(name,sizeof(name),"arcmp-environment-%u.bmp",phase); good &= tig_video_screenshot_save(name)==TIG_OK;
                }
                char text[64]; snprintf(text,sizeof(text),"environment-done:%u",phase); good &= coop_session_chat(held,text); done=true; completed=phase;
            }
        }
        if (phase && !done && now-at>25000) { fprintf(stderr,"ARCMP environment timeout phase=%u measured=%llu checks=%u\n",phase,(unsigned long long)frames,music_checks); good=false; }
        if (host && done && !finishing && game) {
            bool all=true; for (unsigned i=0;i<4;i++) all &= ack[i]==phase;
            if (all) { if (phase<PHASES) good &= announce(phase+1); else { good &= coop_gameplay_finish(game); finishing=true; } }
        }
        if (game && coop_gameplay_finished(game)) { if (!ended) ended=now; if (now-ended>600) break; }
        gamelib_ping(); iso_redraw(); tig_window_display();
        TigMessage message; while (tig_message_dequeue(&message)==TIG_OK) { if (message.type==TIG_MESSAGE_QUIT) good=false; else if (message.type==TIG_MESSAGE_REDRAW) gamelib_redraw(); else intgame_process_event(&message); }
        SDL_Delay(2);
    }
    if (locked) { gsound_unlock(); gsound_unlock(); }
    CoopGameplay* game=coop_runtime_gameplay();
    good &= game && coop_gameplay_finished(game) && completed==PHASES && music_checks==(ui_tail ? 2u : 7u) && coop_runtime_epoch()==(ui_tail ? 1u : 3u) && sample_step==3;
    FILE* report=fopen("arcmp-environment-result.json","wb"); if (!report) good=false;
    if (report) { fprintf(report,"{\"verified\":%s,\"phases\":%u,\"first_phase\":%u,\"epoch\":%u,\"music_checks\":%u,\"pcm_frames\":%llu,\"pcm_energy\":%.9f,\"replica_repair\":%s,\"stream_eof_released\":%s,\"stream_pcm_frames\":%llu,\"effect_pcm_frames\":%llu,\"local_hp\":%d,\"local_max_hp\":%d}\n",good?"true":"false",completed,ui_tail?10:1,coop_runtime_epoch(),music_checks,(unsigned long long)frames,energy,corrupted?"true":"false",sample_step==3?"true":"false",(unsigned long long)stream_frames,(unsigned long long)effect_frames,object_hp_current(player_get_local_pc_obj()),object_hp_max(player_get_local_pc_obj())); if (fclose(report)) good=false; }
    fprintf(stderr,"ARCMP environment probe %s phases=%u\n",good?"PASS":"FAIL",completed); return good;
}
