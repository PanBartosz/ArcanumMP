#ifndef ARCMP_ENGINE_SERVICE_H
#define ARCMP_ENGINE_SERVICE_H
#include "coop/service_state.h"
typedef struct CoopServiceQuote {
    int price, mode, maximum, damage, training, effectiveness;
    unsigned quantity;
    bool selling, follower;
    uint32_t art, flags;
} CoopServiceQuote;
CoopServiceStatus coop_engine_service_quote(int64_t pc, int64_t npc, int64_t item, unsigned mode, CoopServiceQuote* quote);
CoopServiceStatus coop_engine_service_quote_quantity(int64_t pc, int64_t npc, int64_t item, unsigned mode, unsigned quantity, CoopServiceQuote* quote);
bool coop_engine_service_equal(const CoopServiceQuote* first, const CoopServiceQuote* second);
CoopServiceStatus coop_engine_service_buy(int64_t pc, int64_t npc, int64_t item, const CoopServiceQuote* quote);
#endif
