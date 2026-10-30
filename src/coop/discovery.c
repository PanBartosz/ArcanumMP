#include "coop/discovery.h"
#include "coop/object_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET DiscoverySocket;
#define BAD_SOCKET INVALID_SOCKET
#define socket_close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int DiscoverySocket;
#define BAD_SOCKET (-1)
#define socket_close close
#endif
struct CoopDiscovery {
    DiscoverySocket socket; bool host;
    uint16_t port,discovery_port;
    uint64_t query,previous_query,instance,now,sent,window;
    unsigned responses,count;
    CoopDiscoveryParty advertisement,parties[COOP_DISCOVERY_CAPACITY];
};
static uint64_t read64(const uint8_t* p) { return ((uint64_t)coop_object_read32(p)<<32)|coop_object_read32(p+4); }
static void write64(uint8_t* p,uint64_t v) { coop_object_write32(p,(uint32_t)(v>>32)); coop_object_write32(p+4,(uint32_t)v); }
static bool valid(const CoopDiscoveryParty* p)
{
    if (!p || !p->port || !p->protocol || !p->instance || p->players<1 || p->players>COOP_MAX_PLAYERS || !p->name[0]) return false;
    bool end=false;
    for (unsigned i=0;i<sizeof(p->name);i++) { unsigned char c=(unsigned char)p->name[i]; if (!c) end=true; else if (end || c<32 || c>126) return false; }
    return end;
}
static void header(uint8_t* p,unsigned kind,unsigned protocol,uint64_t query)
{
    memcpy(p,"ACMD",4); p[4]=1; p[5]=(uint8_t)kind; p[6]=(uint8_t)(protocol>>8); p[7]=(uint8_t)protocol; write64(p+8,query);
}
bool coop_discovery_encode(const CoopDiscoveryParty* p,uint64_t query,uint8_t bytes[COOP_DISCOVERY_BYTES])
{
    if (!bytes || !query || !valid(p)) return false;
    memset(bytes,0,COOP_DISCOVERY_BYTES); header(bytes,2,p->protocol,query);
    bytes[16]=(uint8_t)(p->port>>8); bytes[17]=(uint8_t)p->port; bytes[18]=p->players; bytes[19]=p->locked;
    write64(bytes+20,p->instance); memcpy(bytes+28,p->content,32); memcpy(bytes+60,p->name,32); return true;
}
bool coop_discovery_decode(const void* bytes,size_t size,uint64_t query,CoopDiscoveryParty* out)
{
    if (!bytes || !out || size!=COOP_DISCOVERY_BYTES) return false;
    const uint8_t* p=bytes;
    if (memcmp(p,"ACMD",4) || p[4]!=1 || p[5]!=2 || !query || read64(p+8)!=query || p[19]>1) return false;
    CoopDiscoveryParty a={.port=(uint16_t)((unsigned)p[16]*256+p[17]),.players=p[18],.locked=p[19]!=0,
        .protocol=(uint16_t)((unsigned)p[6]*256+p[7]),.instance=read64(p+20)};
    memcpy(a.content,p+28,32); memcpy(a.name,p+60,32);
    if (!valid(&a)) return false;
    *out=a; return true;
}
CoopDiscovery* coop_discovery_create(bool host,uint16_t port)
{
#ifdef _WIN32
    WSADATA data; if (WSAStartup(MAKEWORD(2,2),&data)) return NULL;
#endif
    CoopDiscovery* d=calloc(1,sizeof(*d));
    if (!d) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }
    d->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); d->host=host; d->discovery_port=port?port:host?0:COOP_DISCOVERY_PORT;
    if (d->socket==BAD_SOCKET) { coop_discovery_destroy(d); return NULL; }
    int one=1;
    if (setsockopt(d->socket,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof(one))) { coop_discovery_destroy(d); return NULL; }
    if (host) setsockopt(d->socket,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof(one));
#ifdef _WIN32
    u_long nonblocking=1; if (ioctlsocket(d->socket,FIONBIO,&nonblocking)) { coop_discovery_destroy(d); return NULL; }
#else
    int flags=fcntl(d->socket,F_GETFL,0); if (flags<0 || fcntl(d->socket,F_SETFL,flags|O_NONBLOCK)<0) { coop_discovery_destroy(d); return NULL; }
#endif
    struct sockaddr_in address={0}; address.sin_family=AF_INET; address.sin_port=htons(host?d->discovery_port:0);
    if (bind(d->socket,(const struct sockaddr*)&address,sizeof(address))) { coop_discovery_destroy(d); return NULL; }
#ifdef _WIN32
    int length=sizeof(address);
#else
    socklen_t length=sizeof(address);
#endif
    if (getsockname(d->socket,(struct sockaddr*)&address,&length)) { coop_discovery_destroy(d); return NULL; }
    d->port=ntohs(address.sin_port);
    uint8_t random[16]; if (!coop_identity_generate(random)) { coop_discovery_destroy(d); return NULL; }
    d->instance=read64(random); d->query=read64(random+8); if (!d->instance) d->instance=1; if (!d->query) d->query=1;
    return d;
}
void coop_discovery_destroy(CoopDiscovery* d)
{
    if (!d) return;
    if (d->socket!=BAD_SOCKET) socket_close(d->socket);
    free(d);
#ifdef _WIN32
    WSACleanup();
#endif
}
bool coop_discovery_advertise(CoopDiscovery* d,const CoopDiscoveryParty* p)
{
    if (!d || !d->host || !p) return false;
    CoopDiscoveryParty a=*p; a.instance=d->instance;
    if (!valid(&a)) return false;
    d->advertisement=a; return true;
}
static void query(CoopDiscovery* d,const char* target)
{
    struct sockaddr_in address={0}; address.sin_family=AF_INET; address.sin_port=htons(d->discovery_port);
    if (inet_pton(AF_INET,target,&address.sin_addr)!=1) return;
    uint8_t bytes[16]; header(bytes,1,COOP_PROTOCOL_VERSION,d->query);
    sendto(d->socket,(const char*)bytes,sizeof(bytes),0,(const struct sockaddr*)&address,sizeof(address));
}
bool coop_discovery_poll(CoopDiscovery* d,uint64_t now,const char* target)
{
    if (!d) return false;
    if (now>d->now) d->now=now;
    for (unsigned i=0;i<d->count;) {
        if (now-d->parties[i].seen>=COOP_DISCOVERY_TTL) { memmove(&d->parties[i],&d->parties[i+1],(--d->count-i)*sizeof(d->parties[0])); }
        else i++;
    }
    if (!d->host && (!d->sent || now-d->sent>=1500)) {
        d->previous_query=d->query; if (++d->query==0) ++d->query;
        query(d,target?target:"255.255.255.255"); if (!target) query(d,"127.0.0.1"); d->sent=now;
    }
    if (now-d->window>=1000) { d->window=now; d->responses=0; }
    for (unsigned packets=0;packets<32;packets++) {
        uint8_t bytes[128]; struct sockaddr_in source={0};
#ifdef _WIN32
        int length=sizeof(source);
#else
        socklen_t length=sizeof(source);
#endif
        int size=(int)recvfrom(d->socket,(char*)bytes,sizeof(bytes),0,(struct sockaddr*)&source,&length);
        if (size<0) break;
        if (d->host) {
            if (size!=16 || memcmp(bytes,"ACMD",4) || bytes[4]!=1 || bytes[5]!=1 || !read64(bytes+8) || ++d->responses>32) continue;
            uint8_t reply[COOP_DISCOVERY_BYTES];
            if (coop_discovery_encode(&d->advertisement,read64(bytes+8),reply)) sendto(d->socket,(const char*)reply,sizeof(reply),0,(const struct sockaddr*)&source,sizeof(source));
        } else {
            CoopDiscoveryParty a;
            if (!coop_discovery_decode(bytes,(size_t)size,d->query,&a)
                && !coop_discovery_decode(bytes,(size_t)size,d->previous_query,&a)) continue;
            if (!inet_ntop(AF_INET,&source.sin_addr,a.address,sizeof(a.address))) continue;
            a.seen=now; unsigned i;
            for (i=0;i<d->count;i++) if (d->parties[i].port==a.port && (d->parties[i].instance==a.instance || !strcmp(d->parties[i].address,a.address))) break;
            if (i==d->count) { if (d->count==COOP_DISCOVERY_CAPACITY) continue; d->count++; }
            else if (d->parties[i].instance==a.instance && strncmp(d->parties[i].address,"127.",4) && !strncmp(a.address,"127.",4))
                memcpy(a.address,d->parties[i].address,sizeof(a.address));
            d->parties[i]=a;
        }
    }
    return true;
}
void coop_discovery_refresh(CoopDiscovery* d) { if (d) d->sent=0; }
unsigned coop_discovery_count(const CoopDiscovery* d) { return d?d->count:0; }
const CoopDiscoveryParty* coop_discovery_party(const CoopDiscovery* d,unsigned i) { return d && i<d->count ? &d->parties[i] : NULL; }
uint16_t coop_discovery_port(const CoopDiscovery* d) { return d?d->port:0; }
