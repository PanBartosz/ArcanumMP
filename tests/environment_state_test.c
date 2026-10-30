#include "coop/environment_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
int main(void)
{
    CoopEnvironmentState s = { .light_scheme = 4, .light_default = 1, .hour = 23, .indoor = 0x102030, .outdoor = 0xfedcba, .schemes = { 2, 10 }, .after_combat = { 3, 20 }, .after_song = { 4, 30 }, .combat = true, .pending_song = true, .combat_track = 6, .scripted_sound = true, .sound_revision = 19, .sound_members = 5, .sound_origins = { { 1200, 36 }, { 0, 0 }, { 19, 72 }, { 0, 0 } } }, out;
    uint8_t bytes[COOP_ENVIRONMENT_BYTES];
    CHECK(coop_environment_encode(&s,bytes) && coop_environment_decode(bytes,sizeof(bytes),&out));
    CHECK(!memcmp(bytes, "ACE3\0\0\0\4", 8) && bytes[19] == 0x30 && bytes[48] == 7 && bytes[49] == 6);
    CHECK(out.outdoor==s.outdoor && out.after_song[1]==30);
    CHECK(out.sound_members == 5 && out.sound_origins[2][1] == 72 && out.sound_revision == 19);
    uint8_t legacy[COOP_ENVIRONMENT_LEGACY_BYTES];
    memcpy(legacy, bytes, sizeof(legacy));
    memcpy(legacy, "ACE1", 4);
    legacy[48] &= 3;
    CHECK(coop_environment_decode(legacy, sizeof(legacy), &out) && !out.scripted_sound && !out.sound_revision && !out.sound_members);
    CHECK(coop_environment_decode(bytes, sizeof(bytes), &out));
    s.sound_members = 16;
    CHECK(!coop_environment_encode(&s, bytes));
    s.sound_members = 5;
    s.sound_origins[1][0] = 1;
    CHECK(!coop_environment_encode(&s, bytes));
    s.sound_origins[1][0] = 0;
    s.scripted_sound = false;
    CHECK(!coop_environment_encode(&s, bytes));
    s.scripted_sound = true;
    CoopEnvironmentState retained=out;
    bytes[50]=3; CHECK(!coop_environment_decode(bytes,sizeof(bytes),&out) && !memcmp(&out,&retained,sizeof(out))); bytes[50]=0;
    s.difficulty=2; s.fast_turn_based=true; s.owner_options[0]=0x85; s.owner_options[3]=0x8f;
    CHECK(coop_environment_encode(&s,bytes) && coop_environment_decode(bytes,sizeof(bytes),&out)
        && out.difficulty==2 && out.fast_turn_based && out.owner_options[0]==0x85 && out.owner_options[3]==0x8f);
    uint8_t previous[COOP_ENVIRONMENT_V2_BYTES]; memcpy(previous,bytes,sizeof(previous)); memcpy(previous,"ACE2",4); previous[50]=previous[51]=0;
    CHECK(coop_environment_decode(previous,sizeof(previous),&out) && out.difficulty==1 && !out.owner_options[0]);
    s.owner_options[0]=1; CHECK(!coop_environment_encode(&s,bytes)); s.owner_options[0]=0x85;
    s.owner_options[0]=0xc0; s.owner_options[3]=0xcf;
    CHECK(coop_environment_encode(&s,bytes) && coop_environment_decode(bytes,sizeof(bytes),&out)
        && out.owner_options[0]==0xc0 && out.owner_options[3]==0xcf);
    s.owner_options[0]=0xe0; s.owner_options[3]=0xef;
    CHECK(coop_environment_encode(&s,bytes) && coop_environment_decode(bytes,sizeof(bytes),&out)
        && out.owner_options[0]==0xe0 && out.owner_options[3]==0xef);
    s.owner_options[0]=0xf0; s.owner_options[3]=0xff;
    CHECK(coop_environment_encode(&s,bytes) && coop_environment_decode(bytes,sizeof(bytes),&out)
        && out.owner_options[0]==0xf0 && out.owner_options[3]==0xff); s.owner_options[0]=0xe0;
    s.hour=24; CHECK(!coop_environment_encode(&s,bytes)); s.hour=23;
    s.indoor=0x1000000; CHECK(!coop_environment_encode(&s,bytes)); s.indoor=0x102030;
    s.schemes[0]=11; CHECK(!coop_environment_encode(&s,bytes)); s.schemes[0]=2;
    s.combat=false; CHECK(!coop_environment_encode(&s,bytes)); s.combat=true;
    CHECK(coop_environment_encode(&s,bytes));
    uint32_t random=20261022;
    for (unsigned i=0;i<20000;i++) {
        random=random*1664525u+1013904223u;
        unsigned at=random%sizeof(bytes); uint8_t old=bytes[at]; bytes[at]^=(uint8_t)(random>>24);
        if (coop_environment_decode(bytes,sizeof(bytes),&out)) {
            uint8_t again[sizeof(bytes)]; CHECK(coop_environment_encode(&out,again) && !memcmp(bytes,again,sizeof(bytes)));
        }
        bytes[at]=old;
    }
    puts("PASS: lighting/audio environment, canonical metadata, malformed corpus and atomic decode"); return 0;
}
