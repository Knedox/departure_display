#include "app_shared.h"

void configure_power_management(void)
{
    // The 40 MHz floor is the XTAL frequency, the lowest step the ESP32-S3
    // offers under DFS. The display task sleeps for seconds at a time between
    // refreshes, so virtually all of the energy is burned at the floor
    // frequency and halving it from 80 MHz roughly halves the idle CPU current.
    // Work that needs speed (WiFi/HTTP bursts, the frame render) still ramps up
    // to the 160 MHz ceiling on demand.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };

    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Power management: DFS %" PRIu32 "-%" PRIu32 " MHz, light sleep enabled",
             pm_config.min_freq_mhz, pm_config.max_freq_mhz);
}
