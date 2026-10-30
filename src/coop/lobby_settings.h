#ifndef ARCMP_LOBBY_SETTINGS_H
#define ARCMP_LOBBY_SETTINGS_H
#include "coop/session.h"
typedef struct CoopLobbySettings { char name[COOP_NAME_SIZE], address[16]; uint16_t port; } CoopLobbySettings;
bool coop_lobby_settings_load(const char* path,CoopLobbySettings* out);
bool coop_lobby_settings_save(const char* path,const CoopLobbySettings* settings);
#endif
