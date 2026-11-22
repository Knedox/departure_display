#include "app_shared.h"
#include "user_config.h"

void configure_power_management(void)
{
    if (!RLCD_USE_LIGHT_SLEEP) {
        ESP_LOGI(TAG, "Power management: disabled (RLCD_USE_LIGHT_SLEEP=0), CPU runs at max frequency");
        return;
    }

    // 40 MHz XTAL floor halves idle CPU current; bursts ramp to 160 MHz on demand.
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
