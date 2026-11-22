
#include "app_shared.h"

#include "secrets.h"

const char *TAG = "zurich_departures";
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASS = WIFI_PASS_VALUE;
const char *API_URL = "http://transport.opendata.ch/v1/stationboard";
const char *STATION_NAME = "Glattpark";

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

        // Panel is powered down inside night_sleep_enter(), after this frame.
        int64_t sleep_seconds = night_sleep_seconds_until_wake();
        if (sleep_seconds > 0) {
            night_sleep_enter(sleep_seconds);
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_SECONDS * 1000));
    }
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", RLCD_LOG_LEVEL);

    ESP_LOGI(TAG, "Starting Zurich departures display with WiFi");

    configure_power_management();
    wifi_init();

    BaseType_t ok = xTaskCreatePinnedToCore(U8g2_Task, "zurich_display", 16384, NULL, 4, NULL, 1);
    configASSERT(ok == pdPASS);
}

