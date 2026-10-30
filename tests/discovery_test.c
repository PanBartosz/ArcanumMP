#include "coop/discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    CoopDiscoveryParty ad={.port=35741,.protocol=COOP_PROTOCOL_VERSION,.players=1,.name="LAN host",.instance=123,.content={42}},decoded;
    uint8_t bytes[COOP_DISCOVERY_BYTES]; CHECK(coop_discovery_encode(&ad,99,bytes));
    CHECK(coop_discovery_decode(bytes,sizeof(bytes),99,&decoded));
    CHECK(!coop_discovery_decode(bytes,sizeof(bytes),100,&decoded));
    uint32_t random=42;
    for (unsigned i=0;i<20000;i++) {
        random=random*1664525u+1013904223u; unsigned at=random%sizeof(bytes); uint8_t old=bytes[at]; bytes[at]^=(uint8_t)(random>>24);
        if (coop_discovery_decode(bytes,sizeof(bytes),99,&decoded)) { uint8_t again[sizeof(bytes)]; CHECK(coop_discovery_encode(&decoded,99,again) && !memcmp(bytes,again,sizeof(bytes))); }
        bytes[at]=old;
    }
    CoopConfig config={.name="Discovered TCP host",.content_id={42}};
    CoopSession* session=coop_session_create(&config); CHECK(session && coop_session_host(session,0,100)); ad.port=coop_session_port(session);
    CoopDiscovery* host=coop_discovery_create(true,0); CHECK(host && coop_discovery_advertise(host,&ad));
    CoopDiscovery* browser=coop_discovery_create(false,coop_discovery_port(host)); CHECK(browser);
    uint64_t now=100;
    for (unsigned i=0;i<60;i++,now+=10) {
        CHECK(coop_discovery_poll(browser,now,"127.0.0.1") && coop_discovery_poll(host,now,NULL));
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
#endif
    }
    CHECK(coop_discovery_count(browser)==1); const CoopDiscoveryParty* found=coop_discovery_party(browser,0);
    CHECK(found && found->port==ad.port && !strcmp(found->address,"127.0.0.1") && !strcmp(found->name,ad.name) && found->players==1);
    config.name[0]='J'; CoopSession* client=coop_session_create(&config); CHECK(client && coop_session_join(client,found->address,found->port,now));
    for (unsigned i=0;i<60;i++,now+=10) { coop_session_poll(session,now);coop_session_poll(client,now);
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
#endif
    }
    CHECK(coop_session_phase(client)==COOP_LOBBY);
    ad.players=4;ad.locked=true;ad.protocol=COOP_PROTOCOL_VERSION-1;ad.content[0]=43;
    CHECK(coop_discovery_advertise(host,&ad));coop_discovery_refresh(browser);
    for (unsigned i=0;i<60;i++,now+=10) { coop_discovery_poll(browser,now,"127.0.0.1");coop_discovery_poll(host,now,NULL);
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
#endif
    }
    found=coop_discovery_party(browser,0);CHECK(coop_discovery_count(browser)==1 && found->players==4 && found->locked && found->protocol==ad.protocol && found->content[0]==43);
    coop_discovery_destroy(host); CHECK(coop_discovery_poll(browser,now+COOP_DISCOVERY_TTL,"127.0.0.1") && !coop_discovery_count(browser));
    coop_discovery_destroy(browser); coop_session_destroy(client);coop_session_destroy(session);
    puts("PASS: canonical mutations, nonce checks, actual UDP discovery, discovered TCP join, full/running/incompatible updates and expiry"); return 0;
}
