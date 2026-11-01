#ifndef ARCMP_CONTENT_IDENTITY_H
#define ARCMP_CONTENT_IDENTITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { COOP_CONTENT_LEGACY_MAX = 64 };
/* Local save compatibility only. Network admission always compares exact IDs.
 * Register on the main thread after verifying that only ignored files differ. */
void coop_content_legacy_set(const uint8_t current[32], const uint8_t* legacy, size_t count);
const uint8_t* coop_content_legacy_at(const uint8_t current[32], size_t index);
bool coop_content_matches(const uint8_t current[32], const uint8_t saved[32]);
#ifdef __cplusplus
}
#endif
#endif
