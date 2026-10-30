#ifndef ARCMP_TIME_MATH_H
#define ARCMP_TIME_MATH_H
#include <stdint.h>
enum { COOP_DAY_MS = 86400000,
    COOP_DAY_SECONDS = 86400 };
typedef struct CoopTime {
    uint32_t days, milliseconds;
} CoopTime;
CoopTime coop_time_from_milliseconds(uint64_t milliseconds);
CoopTime coop_time_add(CoopTime time, CoopTime interval);
CoopTime coop_time_subtract(CoopTime time, uint64_t milliseconds);
/* Nonpositive script delays retain the native minimum tick. IN uses scale 8,
 * while fades/travel/AT use scale 1. Multiplication never occurs in int32. */
CoopTime coop_time_script_delay(int32_t seconds, unsigned scale);
CoopTime coop_time_script_at(CoopTime now, int32_t seconds_of_day);
#endif
