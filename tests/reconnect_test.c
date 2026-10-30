#include "coop/reconnect.h"
#include "coop/object_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static CoopSession* sessions[5];
static CoopReconnect* reconnects[4];
static uint64_t now=100;
static unsigned prepared=15;
static bool message(void* context,uint8_t sender,uint8_t type,const uint8_t* bytes,size_t size)
{
    return coop_reconnect_message(context,sender,type,bytes,size);
}
static void pump(unsigned rounds)
{
    while (rounds--) {
        for (unsigned i=0;i<5;i++) if (sessions[i]) {
            coop_session_poll(sessions[i],now);
            if (i<4) CHECK(coop_reconnect_poll(reconnects[i],now,(prepared & (1u<<i))!=0));
        }
        now+=5;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
#endif
    }
}
static CoopSession* create(unsigned identity,const char* name)
{
    CoopConfig config={.content_id={42},.identity={0}};
    config.identity[0]=(uint8_t)identity; snprintf(config.name,sizeof(config.name),"%s",name);
    CoopSession* s=coop_session_create(&config); CHECK(s); return s;
}
int main(void)
{
    CoopReconnectView view={.epoch=2,.revision=1,.participants=15,.connected=7,.phase=COOP_RECONNECT_WAIT,.remaining_ms=120000},decoded;
    uint8_t bytes[COOP_RECONNECT_BYTES]; CHECK(coop_reconnect_encode(&view,bytes));
    CHECK(!memcmp(bytes,"\0\0\0\2\0\0\0\1\17\7\1\0",12));
    CHECK(coop_reconnect_decode(bytes,sizeof(bytes),&decoded));
    uint32_t random=42;
    for (unsigned i=0;i<20000;i++) {
        random=random*1664525u+1013904223u; unsigned at=random%sizeof(bytes); uint8_t old=bytes[at]; bytes[at]^=(uint8_t)(random>>24);
        if (coop_reconnect_decode(bytes,sizeof(bytes),&decoded)) { uint8_t again[sizeof(bytes)]; CHECK(coop_reconnect_encode(&decoded,again) && !memcmp(again,bytes,sizeof(bytes))); }
        bytes[at]=old;
    }
    for (unsigned i=0;i<4;i++) {
        char name[32]; snprintf(name,sizeof(name),"Traveller %u",i);
        sessions[i]=create(i+1,name); reconnects[i]=coop_reconnect_create(sessions[i]); CHECK(reconnects[i]);
        coop_session_set_handler(sessions[i],message,reconnects[i]);
    }
    CHECK(coop_session_host(sessions[0],0,now)); uint16_t port=coop_session_port(sessions[0]);
    for (unsigned i=1;i<4;i++) CHECK(coop_session_join(sessions[i],"127.0.0.1",port,now));
    pump(60);
    for (unsigned i=0;i<4;i++) CHECK(coop_session_character(sessions[i],16067+i,"Original hero"));
    pump(30);
    CoopCharacter custom={.hp=3,.fatigue=2,.unspent=1,.portrait=77,.name="Reserved custom hero"};
    for (unsigned i=0;i<8;i++) custom.stats[i]=8;
    CHECK(coop_session_custom_character(sessions[3],&custom)); pump(20);
    for (unsigned i=0;i<4;i++) CHECK(coop_session_ready(sessions[i],true)); pump(40);
    CHECK(coop_session_all_ready(sessions[0])); coop_session_lock(sessions[0],true); pump(20);
    for (unsigned i=0;i<4;i++) CHECK(coop_session_locked(sessions[i]) && coop_session_reserved(sessions[i])==15);
    CHECK(coop_session_drop_transport(sessions[3]) && coop_session_can_retry(sessions[3])); pump(30);
    CHECK(!coop_session_all_ready(sessions[0]) && !coop_session_players(sessions[0])[3].connected);
    CHECK(coop_session_players(sessions[1])[3].custom && !strcmp(coop_session_players(sessions[1])[3].character,custom.name));
    CHECK(coop_session_players(sessions[0])[1].ready);
    sessions[4]=create(9,"Unknown profile"); CHECK(coop_session_join(sessions[4],"127.0.0.1",port,now)); pump(30);
    CHECK(coop_session_phase(sessions[4])==COOP_OFFLINE && strstr(coop_session_status(sessions[4]),"original player profiles"));
    CHECK(coop_reconnect_begin(reconnects[0],2,15,now)); prepared=7; pump(30);
    CHECK(!coop_reconnect_ready(reconnects[0]));
    CHECK(coop_session_retry(sessions[3],now)); pump(40);
    CHECK(coop_session_local_slot(sessions[3])==3 && coop_session_players(sessions[3])[3].custom && coop_session_all_ready(sessions[0]));
    CHECK(!coop_reconnect_ready(reconnects[0])); prepared=15; pump(60);
    CHECK(coop_reconnect_ready(reconnects[0]));
    for (unsigned i=0;i<4;i++) { CHECK(coop_reconnect_current(reconnects[i])->epoch==2); coop_reconnect_end(reconnects[i]); }
    uint8_t ack[8]; coop_object_write32(ack,2); coop_object_write32(ack+4,UINT32_MAX);
    CHECK(!coop_reconnect_message(reconnects[0],1,COOP_RECONNECT_ACK,ack,sizeof(ack)));
    CHECK(!coop_reconnect_begin(reconnects[0],2,15,now));
    CHECK(coop_session_drop_transport(sessions[2]) && coop_session_drop_transport(sessions[3])); pump(30);
    CHECK(coop_reconnect_begin(reconnects[0],3,15,now)); pump(20);
    CHECK(coop_session_retry(sessions[3],now)); pump(30); CHECK(!coop_reconnect_ready(reconnects[0]));
    coop_reconnect_destroy(reconnects[2]); coop_session_destroy(sessions[2]);
    sessions[2]=create(3,"Renamed returning traveller"); reconnects[2]=coop_reconnect_create(sessions[2]); CHECK(reconnects[2]);
    coop_session_set_handler(sessions[2],message,reconnects[2]);
    CHECK(coop_session_join(sessions[2],"127.0.0.1",port,now)); pump(70);
    CHECK(coop_session_local_slot(sessions[2])==2 && !strcmp(coop_session_players(sessions[0])[2].character,"Original hero"));
    CHECK(coop_reconnect_ready(reconnects[0]));
    for (unsigned i=0;i<4;i++) coop_reconnect_end(reconnects[i]);
    CHECK(coop_reconnect_begin(reconnects[0],4,15,now));
    CHECK(!coop_reconnect_poll(reconnects[0],now+COOP_RECONNECT_GRACE,true));
    for (unsigned i=0;i<4;i++) coop_reconnect_end(reconnects[i]);
    coop_session_leave(sessions[0]); pump(20);
    for (unsigned i=1;i<4;i++) CHECK(coop_session_phase(sessions[i])==COOP_OFFLINE && !coop_session_can_retry(sessions[i]) && strstr(coop_session_status(sessions[i]),"ended"));
    for (unsigned i=0;i<4;i++) coop_reconnect_destroy(reconnects[i]);
    for (unsigned i=0;i<5;i++) coop_session_destroy(sessions[i]);
    puts("PASS: reserved custom profile, reversed returns, unknown-owner refusal, two acknowledged stages, timeout, explicit shutdown and 20000 mutations");
    return 0;
}
