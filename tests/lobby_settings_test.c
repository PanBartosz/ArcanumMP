#include "coop/lobby_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    const char* path="lobby-settings-test.bin"; remove(path);
    CoopLobbySettings s={.name="A saved traveller",.address="192.168.12.34",.port=45555},out={0};
    CHECK(!coop_lobby_settings_load(path,&out)); CHECK(coop_lobby_settings_save(path,&s) && coop_lobby_settings_load(path,&out));
    CHECK(!strcmp(out.name,s.name) && !strcmp(out.address,s.address) && out.port==s.port);
    s.port=0;CHECK(!coop_lobby_settings_save(path,&s));CHECK(coop_lobby_settings_load(path,&out) && out.port==45555);
    s.port=35741;snprintf(s.name,sizeof(s.name),"Updated name");CHECK(coop_lobby_settings_save(path,&s));
    FILE* f=fopen(path,"r+b");CHECK(f && fseek(f,10,SEEK_SET)==0);fputc(255,f);CHECK(!fclose(f));
    memset(&out,17,sizeof(out));CoopLobbySettings before=out;CHECK(!coop_lobby_settings_load(path,&out) && !memcmp(&out,&before,sizeof(out)));
    CHECK(coop_lobby_settings_save(path,&s));f=fopen(path,"ab");CHECK(f);fputc(1,f);fclose(f);CHECK(!coop_lobby_settings_load(path,&out));
    CHECK(!remove(path));puts("PASS: persistent name/endpoint, atomic valid replacement, invalid-save preservation, corruption and trailing-byte refusal");return 0;
}
