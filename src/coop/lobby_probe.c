#include "coop/lobby_ui.h"
#include "coop/lobby_settings.h"
#include "coop/runtime.h"
#include "ui/intgame.h"
#include "ui/mainmenu_ui.h"
#include "game/gamelib.h"
#include <tig/tig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void click(int x,int y)
{
    TigMessage message={.type=TIG_MESSAGE_MOUSE};message.data.mouse.x=x;message.data.mouse.y=y;
    message.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN;tig_message_enqueue(&message);
    message.data.mouse.event=TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP;tig_message_enqueue(&message);
}
int coop_lobby_probe(int argc,char** argv)
{
    unsigned player=4,port=0;bool host=false;
    for (int i=1;i<argc;i++) {
        if (!strncmp(argv[i],"--coop-probe-player=",20)) player=(unsigned)atoi(argv[i]+20);
        if (!strncmp(argv[i],"--coop-probe-port=",18)) port=(unsigned)atoi(argv[i]+18);
        if (!strcmp(argv[i],"--coop-probe-host")) host=true;
    }
    if (!port || port>65535 || player>3 || host!=(player==0)) return 1;
    CoopLobbySettings settings={.address="127.0.0.1",.port=host?(uint16_t)port:1};
    snprintf(settings.name,sizeof(settings.name),"LAN player %u",player);
    bool good=coop_lobby_settings_save("arcmp-lobby.bin",&settings);
    mainmenu_ui_window_type_set(MM_WINDOW_MAINMENU);coop_lobby_open();
    uint64_t start=SDL_GetTicks(),found_at=0,ready_at=0,chats=0;unsigned mask=0;
    bool opened=false,joining=false,picked=false,sent=false,captured=false,discovered=false;
    while (good && SDL_GetTicks()-start<30000) {
        uint64_t now=SDL_GetTicks();tig_ping();coop_lobby_tick();
        if (!opened && now-start>300) {click(host?100:570,154);opened=true;}
        if (!host && !joining) {
            int row=coop_lobby_probe_party((uint16_t)port);
            if (row>=0) {
                discovered=true;if (!found_at) found_at=now;
                if (now-found_at>400) {
                    click(180,250+row*25);tig_window_display();
                    good &= tig_video_screenshot_save("arcmp-lan-browser.bmp")==TIG_OK;
                    click(330,374);joining=true;
                }
            }
        }
        CoopSession* s=coop_runtime_session();
        if (s && coop_session_phase(s)==COOP_LOBBY) {
            unsigned slot=coop_session_local_slot(s);
            if (!picked) {click(90,374);picked=true;}
            else if (!coop_session_players(s)[slot].ready) {
                bool selected=true;for (unsigned i=0;i<4;i++) selected &= coop_session_players(s)[i].connected && coop_session_players(s)[i].prototype;
                if (selected) click(570,374);
            }
            unsigned count=0;for (unsigned i=0;i<4;i++) count+=coop_session_players(s)[i].connected;
            if (count==4 && coop_session_all_ready(s)) {
                if (!ready_at) ready_at=now;
                if (!captured && now-ready_at>300) {tig_window_display();good &= tig_video_screenshot_save("arcmp-lan-ready.bmp")==TIG_OK;captured=true;}
                if (captured && !sent) {good &= coop_session_chat(s,"LAN ready");sent=true;}
            }
            for (;chats<coop_session_chat_count(s);chats++) {
                const CoopChat* chat=coop_session_chat_at(s,chats);if (chat && !strcmp(chat->text,"LAN ready")) mask|=1u<<chat->slot;
            }
            if (mask==15 && now-ready_at>1100) break;
        }
        gamelib_redraw();tig_window_display();TigMessage message;
        while (tig_message_dequeue(&message)==TIG_OK) {if (message.type==TIG_MESSAGE_QUIT) good=false;else if (message.type==TIG_MESSAGE_REDRAW) gamelib_redraw();else intgame_process_event(&message);}
        SDL_Delay(2);
    }
    CoopLobbySettings saved={0};good &= coop_lobby_settings_load("arcmp-lobby.bin",&saved);
    good &= saved.port==port && !strcmp(saved.name,settings.name) && (host||discovered) && mask==15 && captured;
    FILE* out=fopen("arcmp-lan-result.json","wb");if (!out) good=false;
    if (out) {fprintf(out,"{\"verified\":%s,\"host\":%s,\"discovered\":%s,\"ready_mask\":%u,\"saved_port\":%u}\n",good?"true":"false",host?"true":"false",discovered?"true":"false",mask,saved.port);if(fclose(out))good=false;}
    fprintf(stderr,"ARCMP LAN lobby probe %s mask=%u discovered=%d\n",good?"PASS":"FAIL",mask,discovered);
    return good?0:1;
}
