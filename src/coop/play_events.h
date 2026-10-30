#ifndef ARCMP_PLAY_EVENTS_H
#define ARCMP_PLAY_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
enum { PLAY_COMBAT, PLAY_EFFECT, PLAY_DEATH, PLAY_PING, PLAY_NOTICE, PLAY_MAP_PING, PLAY_EVENT_KINDS };
enum { COOP_PLAY_HISTORY=512, COOP_PLAY_EVENT_BYTES=604 };
typedef struct PlayEvent {
    uint32_t sequence, map, time, x, y;
    uint8_t kind, members;
    char summary[192], details[384];
} PlayEvent;
static inline uint32_t coop_play_read32(const uint8_t* p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static inline bool coop_play_event_record_valid(const uint8_t* p,size_t size)
{
    return p && size==COOP_PLAY_EVENT_BYTES && p[24]<PLAY_EVENT_KINDS && p[25]<=15 && !p[26] && !p[27]
        && memchr(p+28,0,192) && memchr(p+220,0,384) && coop_play_read32(p+4)>0 && coop_play_read32(p+4)<UINT32_MAX;
}
static inline bool coop_play_events_file_valid(const uint8_t* p,size_t size)
{
    if (!p || size<16 || memcmp(p,"MPEV0001",8)) return false;
    uint32_t first=coop_play_read32(p+8),end=coop_play_read32(p+12);
    if (!first || end<first || end==UINT32_MAX || end-first>COOP_PLAY_HISTORY || size!=16+(size_t)(end-first)*COOP_PLAY_EVENT_BYTES) return false;
    for (uint32_t n=first;n<end;n++) {
        const uint8_t* e=p+16+(size_t)(n-first)*COOP_PLAY_EVENT_BYTES;
        if (!coop_play_event_record_valid(e,COOP_PLAY_EVENT_BYTES) || coop_play_read32(e) || coop_play_read32(e+4)!=n) return false;
    }
    return true;
}
#endif
