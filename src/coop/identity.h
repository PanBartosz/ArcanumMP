#ifndef ARCMP_IDENTITY_H
#define ARCMP_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>
#define COOP_IDENTITY_SIZE 16
bool coop_identity_generate(uint8_t out[COOP_IDENTITY_SIZE]);
bool coop_identity_valid(const uint8_t id[COOP_IDENTITY_SIZE]);
/* Existing invalid profiles are errors, never silently replaced. */
bool coop_identity_load_or_create(const char* path, uint8_t out[COOP_IDENTITY_SIZE]);
#endif
