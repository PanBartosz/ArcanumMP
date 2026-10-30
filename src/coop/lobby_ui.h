#ifndef ARCMP_LOBBY_UI_H
#define ARCMP_LOBBY_UI_H
#include <stdint.h>
#include "coop/character.h"
void coop_lobby_open(void);
bool coop_lobby_is_open(void);
void coop_lobby_tick(void);
void coop_lobby_shutdown(void);
/* Integration probe: read the visible LAN row; never joins or changes settings. */
int coop_lobby_probe_party(uint16_t tcp_port);
int coop_lobby_probe(int argc,char** argv);
void coop_lobby_release_previews(void);
bool coop_lobby_preparation_probe(unsigned player, bool framebuffer, CoopCharacter* character);
bool coop_lobby_dropin_probe(bool framebuffer);
#endif
