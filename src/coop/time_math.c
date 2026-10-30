#include "coop/time_math.h"
#include <limits.h>
static uint64_t total(CoopTime t) { return (uint64_t)t.days * COOP_DAY_MS + t.milliseconds; }
CoopTime coop_time_from_milliseconds(uint64_t ms)
{
    uint64_t maximum = (uint64_t)UINT32_MAX * COOP_DAY_MS + COOP_DAY_MS - 1;
    if (ms > maximum) ms = maximum;
    return (CoopTime) { (uint32_t)(ms / COOP_DAY_MS), (uint32_t)(ms % COOP_DAY_MS) };
}
CoopTime coop_time_add(CoopTime time, CoopTime interval)
{
    return coop_time_from_milliseconds(total(time) + total(interval));
}
CoopTime coop_time_subtract(CoopTime time, uint64_t milliseconds)
{
    uint64_t value = total(time);
    return coop_time_from_milliseconds(value > milliseconds ? value - milliseconds : 0);
}
CoopTime coop_time_script_delay(int32_t seconds, unsigned scale)
{
    uint64_t value = seconds > 0 ? (uint64_t)(uint32_t)seconds * 1000 : 1;
    /* Engine callers use 1 or 8; constrain the public helper's multiplier too. */
    if (scale != 8) scale = 1;
    return coop_time_from_milliseconds(value * scale);
}
CoopTime coop_time_script_at(CoopTime now, int32_t seconds_of_day)
{
    int64_t target = seconds_of_day % COOP_DAY_SECONDS;
    if (target < 0) target += COOP_DAY_SECONDS;
    uint32_t current = (uint32_t)((total(now) / 1000) % COOP_DAY_SECONDS);
    int64_t delay = target - current;
    if (delay < 0) delay += COOP_DAY_SECONDS;
    return coop_time_script_delay((int32_t)delay, 1);
}
