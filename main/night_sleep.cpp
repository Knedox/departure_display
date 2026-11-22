#include "app_shared.h"
#include "user_config.h"

/* Night-time deep sleep in one-hour chunks; no state survives the reboot. */

namespace {

#if RLCD_NIGHT_SLEEP_ENABLED

constexpr int64_t SECONDS_PER_DAY = 24 * 60 * 60;

// Local seconds since midnight, or -1 before the clock is known.
int64_t local_seconds_of_day()
{
    if (current_epoch_seconds <= 0) {
        return -1;
    }

    int64_t local_epoch = current_epoch_seconds + current_timezone_offset_seconds;
    int64_t sod = local_epoch % SECONDS_PER_DAY;
    if (sod < 0) {
        sod += SECONDS_PER_DAY;
    }
    return sod;
}

// Window may wrap midnight (default 19:00 -> 07:00).
bool is_night_time(int64_t sod)
{
    const int64_t start = RLCD_NIGHT_SLEEP_START_HOUR * 60 * 60;
    const int64_t end = RLCD_NIGHT_SLEEP_END_HOUR * 60 * 60;

    if (start <= end) {
        return sod >= start && sod < end;
    }
    return sod >= start || sod < end;
}

/* Seconds from now until the end of the night window. */
int64_t seconds_until_night_end(int64_t sod)
{
    const int64_t end = RLCD_NIGHT_SLEEP_END_HOUR * 60 * 60;

    int64_t delta = end - sod;
    if (delta <= 0) {
        delta += SECONDS_PER_DAY;
    }
    return delta;
}

#endif  // RLCD_NIGHT_SLEEP_ENABLED

}  // namespace

int64_t night_sleep_seconds_until_wake()
{
#if RLCD_NIGHT_SLEEP_ENABLED
    const int64_t sod = local_seconds_of_day();
    if (sod < 0) {
        // No clock yet: stay awake rather than sleep for an unknown length.
        return -1;
    }

    if (!is_night_time(sod)) {
        return -1;
    }

    int64_t sleep_seconds = seconds_until_night_end(sod);
    if (sleep_seconds > RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS) {
        sleep_seconds = RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS;
    }
    if (sleep_seconds <= 0) {
        return -1;
    }

    return sleep_seconds;
#else
    return -1;
#endif
}

void night_sleep_enter(int64_t sleep_seconds)
{
    if (sleep_seconds <= 0) {
        return;
    }

    ESP_LOGI(TAG, "Night window: deep sleep for %lld s",
             static_cast<long long>(sleep_seconds));

    // The panel stays on the battery rail through deep sleep, so switch it off first.
    U8g2_PowerDownDisplay();

    const uint64_t sleep_us = static_cast<uint64_t>(sleep_seconds) * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_deep_sleep_start();

    // Does not return; reaching here means the sleep was rejected.
    ESP_LOGW(TAG, "Deep sleep was rejected; continuing in light sleep");
}
