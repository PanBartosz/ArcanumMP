#ifndef ARCMP_SERVICE_STATE_H
#define ARCMP_SERVICE_STATE_H
#include "coop/object_state.h"
#define COOP_SERVICE_REQUEST_BYTES 66
typedef enum CoopServiceOperation { COOP_SERVICE_QUOTE, COOP_SERVICE_BUY, COOP_SERVICE_CRAFT } CoopServiceOperation;
typedef enum CoopServiceStatus {
    COOP_SERVICE_OK, COOP_SERVICE_UNAVAILABLE, COOP_SERVICE_OWNERSHIP,
    COOP_SERVICE_CONVERSATION, COOP_SERVICE_TARGET, COOP_SERVICE_IDENTIFIED,
    COOP_SERVICE_BROKEN, COOP_SERVICE_UNDAMAGED, COOP_SERVICE_MONEY,
    COOP_SERVICE_CHANGED, COOP_SERVICE_LIMIT, COOP_SERVICE_COMPONENTS, COOP_SERVICE_EXPERTISE, COOP_SERVICE_KNOWLEDGE,
} CoopServiceStatus;
typedef struct CoopServiceRequest {
    uint32_t epoch, sequence, map;
    uint8_t actor[16];
    uint32_t conversation, page;
    CoopServiceOperation operation;
    CoopObjectId item;
    uint32_t quote;
    uint32_t quantity, recipe;
} CoopServiceRequest;
bool coop_service_request_encode(const CoopServiceRequest* request, uint8_t out[COOP_SERVICE_REQUEST_BYTES]);
bool coop_service_request_decode(const void* bytes, size_t size, CoopServiceRequest* request);
#endif
