#include "coop/time_math.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
static uint64_t total(CoopTime t)
{
    CHECK(t.milliseconds < COOP_DAY_MS);
    return (uint64_t)t.days * COOP_DAY_MS + t.milliseconds;
}
int main(void)
{
    CHECK(total(coop_time_from_milliseconds(COOP_DAY_MS)) == COOP_DAY_MS);
    CHECK(coop_time_from_milliseconds(COOP_DAY_MS).days == 1);
    CHECK(total(coop_time_add((CoopTime) { 7, 86399999 }, (CoopTime) { 0, 1 })) == (uint64_t)8 * COOP_DAY_MS);
    CHECK(total(coop_time_subtract((CoopTime) { 3, 100 }, 101)) == (uint64_t)3 * COOP_DAY_MS - 1);
    CHECK(total(coop_time_subtract((CoopTime) { 0, 100 }, 101)) == 0);
    CHECK(total(coop_time_script_delay(INT32_MAX, 8)) == (uint64_t)INT32_MAX * 8000);
    CHECK(total(coop_time_script_delay(240000, 1)) == 240000000);
    CHECK(total(coop_time_script_delay(0, 8)) == 8 && total(coop_time_script_delay(-1, 1)) == 1);
    CHECK(total(coop_time_script_at((CoopTime) { 123, 23 * 3600000 }, 3600)) == 2 * 3600000);
    CHECK(total(coop_time_script_at((CoopTime) { 7, 9 * 3600000 }, 8 * 3600)) == 23 * 3600000);
    CHECK(total(coop_time_script_at((CoopTime) { 7, 9 * 3600000 }, 9 * 3600)) == 1);
    CHECK(total(coop_time_script_at((CoopTime) { 0, 0 }, -1)) == 86399000);
    CoopTime maximum = coop_time_add((CoopTime) { UINT32_MAX, COOP_DAY_MS - 1 }, (CoopTime) { 1, UINT32_MAX });
    CHECK(maximum.days == UINT32_MAX && maximum.milliseconds == COOP_DAY_MS - 1);
    uint32_t random = 31;
    for (unsigned i = 0; i < 50000; i++) {
        random = random * 1664525u + 1013904223u;
        uint32_t days = random % 20000, ms = random;
        uint64_t original = (uint64_t)days * COOP_DAY_MS + ms;
        random = random * 1664525u + 1013904223u;
        uint32_t delta = random;
        CHECK(total(coop_time_add((CoopTime) { days, ms }, coop_time_from_milliseconds(delta))) == original + delta);
        CHECK(total(coop_time_subtract((CoopTime) { days, ms }, delta)) == (original > delta ? original - delta : 0));
        CoopTime at = coop_time_script_at((CoopTime) { days, ms }, (int32_t)random);
        CHECK(total(at) > 0 && total(at) < COOP_DAY_MS);
    }
    puts("PASS: campaign clock normalization, midnight borrow, saturation, long script/sea delays and native daily scheduling");
    return 0;
}
