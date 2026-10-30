#ifndef ARCMP_CONTENT_H
#define ARCMP_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Nonblocking: validate cached asset metadata or hash changed content in the background. */
bool coop_content_read(uint8_t id[32]);
/* Returns true while a progress/error message should be displayed. */
bool coop_content_status(char* message, size_t size);
void coop_content_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
