#ifndef ARCMP_STARTING_SHOP_H
#define ARCMP_STARTING_SHOP_H
#include "coop/character.h"
typedef struct CoopStartingShop CoopStartingShop;
typedef struct CoopStartingItem {
    uint16_t prototype;
    unsigned quantity;
    int price;
    char name[96];
} CoopStartingItem;
CoopStartingShop* coop_starting_open(const CoopCharacter* character);
void coop_starting_close(CoopStartingShop* shop);
bool coop_starting_validate(const CoopCharacter* character);
bool coop_starting_apply(int64_t pc, const CoopCharacter* character);
unsigned coop_starting_items(CoopStartingShop* shop, bool selling, CoopStartingItem* items, unsigned capacity);
int coop_starting_gold(const CoopStartingShop* shop);
uint32_t coop_starting_inventory(int64_t pc);
uint32_t coop_starting_preview(const CoopCharacter* character);
bool coop_starting_choose(CoopCharacter* character);
bool coop_starting_probe(bool framebuffer);
#endif
