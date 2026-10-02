
#include "app_shared.h"

/* The WiFi credentials come from secrets.h,
 * which is git-ignored (template: secrets.h.example). */
#include "secrets.h"

const char *TAG = "zurich_departures";
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASS = WIFI_PASS_VALUE;
const char *API_URL = "http://transport.opendata.ch/v1/stationboard";
const char *STATION_NAME = "Glattpark";

/* How often the display is redrawn. */
static constexpr uint32_t DISPLAY_REFRESH_INTERVAL_SECONDS = 30;

static void U8g2_Task(void *arg)
{
    (void)arg;
    u8g2_t *u8g2 = U8g2_InitDisplayHandle();
    if (u8g2 == nullptr) {
        ESP_LOGE(TAG, "Display initialization failed");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        uint64_t now_us = esp_timer_get_time();
        U8g2_RefreshDisplayState(now_us);
        U8g2_RenderDepartureFrame(u8g2);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_SECONDS * 1000));
    }
}

extern "C" void app_main(void)
{
    // Trim the runtime log level before anything else can log. The steady-state
    // render path emits several INFO lines per departure row on every refresh;
    // at this level those calls are compiled out (RLCD_LOG_LEVEL is below
    // ESP_LOG_INFO), which removes the UART traffic and the CPU time it costs
    // on each wake from light sleep. Raise RLCD_LOG_LEVEL in user_config.h to
    // ESP_LOG_INFO to get the detailed output back.
    esp_log_level_set("*", RLCD_LOG_LEVEL);

    ESP_LOGI(TAG, "Starting Zurich departures display with WiFi");

    // Dynamic frequency scaling + automatic light sleep.
    configure_power_management();
    wifi_init();

    BaseType_t ok = xTaskCreatePinnedToCore(U8g2_Task, "zurich_display", 16384, NULL, 4, NULL, 1);
    configASSERT(ok == pdPASS);
}

