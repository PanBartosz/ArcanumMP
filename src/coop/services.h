#ifndef ARCMP_SERVICES_H
#define ARCMP_SERVICES_H
#include "coop/service_state.h"
#include "coop/session.h"
#include "coop/world_net.h"
#include "coop/dialogue.h"
enum { COOP_SERVICE_REQUEST = 59, COOP_SERVICE_RESULT = 60 };
typedef struct CoopServices CoopServices;
typedef int64_t (*CoopServiceActor)(void* context, uint8_t sender, const uint8_t* common);
CoopServices* coop_services_create(CoopSession* session, CoopWorldNetwork* world, CoopDialogue* dialogue, uint32_t epoch, uint64_t now, CoopServiceActor actor, void* context);
void coop_services_destroy(CoopServices* services);
bool coop_services_message(CoopServices* services, uint8_t sender, uint8_t type, const uint8_t* bytes, size_t size);
bool coop_services_poll(CoopServices* services, uint64_t now, bool accepting);
bool coop_services_submit(CoopServices* services, const CoopServiceRequest* request);
void coop_services_hover(CoopServices* services, int64_t item);
bool coop_services_buy(CoopServices* services, int64_t item);
void coop_services_hover_quantity(CoopServices* services, int64_t item, unsigned quantity);
bool coop_services_buy_quantity(CoopServices* services, int64_t item, unsigned quantity);
bool coop_services_craft(CoopServices* services, int64_t crafter, unsigned recipe);
void coop_services_close(CoopServices* services);
bool coop_services_settled(const CoopServices* services);
bool coop_services_busy(const CoopServices* services);
uint32_t coop_services_last_result(const CoopServices* services, unsigned* status);
uint32_t coop_services_quote(const CoopServices* services, int* price);
unsigned coop_services_executed(const CoopServices* services);
unsigned coop_services_rejected(const CoopServices* services);
#endif
