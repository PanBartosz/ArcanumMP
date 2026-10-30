#include "coop/lobby_settings.h"
#include "coop/bootstrap.h"
#include "coop/object_state.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
static bool valid(const CoopLobbySettings* s)
{
    if (!s || !s->port || !s->name[0]) return false;
    bool end=false;
    for (unsigned i=0;i<sizeof(s->name);i++) {
        unsigned char c=(unsigned char)s->name[i];
        if (!c) end=true; else if (end || c<32 || c>126) return false;
    }
    if (!end || !memchr(s->address,0,sizeof(s->address))) return false;
    const char* p=s->address;
    for (unsigned i=0;i<4;i++) {
        unsigned value=0,count=0;
        while (*p>='0' && *p<='9') { value=value*10+(unsigned)(*p++-'0'); if (++count>3 || value>255) return false; }
        if (!count || (i==3 ? *p!=0 : *p++!='.')) return false;
    }
    return true;
}
bool coop_lobby_settings_load(const char* path,CoopLobbySettings* out)
{
    if (!path || !out) return false;
    FILE* file=fopen(path,"rb"); if (!file) return false;
    uint8_t bytes[64]; bool good=fread(bytes,1,sizeof(bytes),file)==sizeof(bytes) && fgetc(file)==EOF;
    fclose(file);
    if (!good || memcmp(bytes,"ACLP1\0\0",8) || bytes[58] || bytes[59]
        || coop_object_read32(bytes+60)!=coop_snapshot_crc(bytes,60)) return false;
    CoopLobbySettings s={0}; memcpy(s.name,bytes+8,32); memcpy(s.address,bytes+40,16);
    s.port=(uint16_t)((unsigned)bytes[56]*256+bytes[57]);
    if (!valid(&s)) return false;
    *out=s; return true;
}
bool coop_lobby_settings_save(const char* path,const CoopLobbySettings* s)
{
    if (!path || !s || !memchr(s->name,0,sizeof(s->name)) || !memchr(s->address,0,sizeof(s->address))) return false;
    /* Callers edit ordinary C strings; canonicalize bytes beyond their NULs
     * rather than rejecting stale characters left by a shorter replacement. */
    CoopLobbySettings canonical={.port=s->port};
    snprintf(canonical.name,sizeof(canonical.name),"%s",s->name);
    snprintf(canonical.address,sizeof(canonical.address),"%s",s->address);
    s=&canonical;
    if (!valid(s)) return false;
    char temporary[512]; if (snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary)) return false;
    uint8_t bytes[64]={0}; memcpy(bytes,"ACLP1\0\0",8); memcpy(bytes+8,s->name,32); memcpy(bytes+40,s->address,16);
    bytes[56]=(uint8_t)(s->port>>8); bytes[57]=(uint8_t)s->port; coop_object_write32(bytes+60,coop_snapshot_crc(bytes,60));
    FILE* file=fopen(temporary,"wb"); if (!file) return false;
    bool good=fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes) && fflush(file)==0;
#ifdef _WIN32
    if (good) good=_commit(_fileno(file))==0;
#else
    if (good) good=fsync(fileno(file))==0;
#endif
    if (fclose(file)) good=false;
#ifdef _WIN32
    if (good) good=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if (good) good=rename(temporary,path)==0;
#endif
    if (!good) remove(temporary);
    return good;
}
