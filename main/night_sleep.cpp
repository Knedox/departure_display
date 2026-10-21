#include "app_shared.h"
#include "user_config.h"

/* Night-time deep sleep.
 *
 * The device sleeps in one-hour chunks through the night window (19:00 to 07:00
 * local time by default). Each chunk ends in a timer wakeup, the chip reboots,
 * the display task fetches the clock from the API again and, if the window is
 * still open, goes straight back to sleep. The last chunk of the night is
 * trimmed so the chip wakes exactly at the end of the window.
 *
 * Nothing is retained across the sleep: the wall clock is re-derived from the
 * HTTP Date header on the first fetch after the wake, so no RTC slow memory is
 * used and no state has to survive the reset.
 *
 * The panel is switched off before the chip sleeps (Sleep In, then the
 * hardware reset line is asserted). The panel is on the battery's 3.3 V rail
 * and would otherwise stay powered and refreshing for the whole night. No GPIO
 * is held across the sleep: the pads are left to their reset state, and the
 * panel is brought back up by the driver's init sequence after the wake.
 */

namespace {

#if RLCD_NIGHT_SLEEP_ENABLED

constexpr int64_t SECONDS_PER_DAY = 24 * 60 * 60;

/* Seconds since local midnight, in the timezone of the data source. Returns -1
 * when the clock is not known yet, i.e. before the first successful fetch. */
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

/* Is the given second-of-day inside the configured night window? The window
 * may wrap midnight (19:00 -> 07:00), which is the default. */
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
        // The end of the window lies on the next day.
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
        // No clock yet: the chip has not fetched anything since boot. Staying
        // awake is the only safe choice - a sleep of unknown length would
        // blind the device.
        return -1;
    }

    if (!is_night_time(sod)) {
        return -1;
    }

    int64_t sleep_seconds = seconds_until_night_end(sod);
    if (sleep_seconds > RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS) {
        // One chunk per sleep: the chip wakes up roughly hourly, re-reads the
        // clock and decides again.
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

    // Take the panel down before the chip sleeps. The panel is on the
    // battery's 3.3 V rail and stays powered through the deep sleep, so it has
    // to be switched off explicitly - otherwise the display would keep drawing
    // all night, which is the larger share of the device's idle current.
    U8g2_PowerDownDisplay();

    const uint64_t sleep_us = static_cast<uint64_t>(sleep_seconds) * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_deep_sleep_start();

    // esp_deep_sleep_start() does not return. If it somehow does, the sleep was
    // rejected and the caller carries on with the normal refresh loop.
    ESP_LOGW(TAG, "Deep sleep was rejected; continuing in light sleep");
}
