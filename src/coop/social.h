#ifndef ARCMP_SOCIAL_H
#define ARCMP_SOCIAL_H
#include <stdint.h>
/* Explicit story rewards. Native quest/combat rewards keep their own paths. */
void coop_social_xp(int64_t actor, int amount);
void coop_social_reputation(int64_t actor, int number, int remove);
/* '+': relative, '>': minimum, '<': maximum, '=': absolute. */
void coop_social_alignment(int64_t actor, char operation, int value);
#endif
