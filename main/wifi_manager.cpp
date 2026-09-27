#include "app_shared.h"

bool wifi_connected = false;
esp_netif_t *g_wifi_sta_netif = nullptr;
bool static_ip_configured = false;
// Set while the station is intentionally down between refreshes, so the
// disconnect handler does not immediately reconnect and undo the power saving.
static bool wifi_suppressed = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (wifi_suppressed) {
            ESP_LOGI(TAG, "WiFi disconnected as requested; staying down until next refresh");
            return;
        }
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        if (!static_ip_configured && g_wifi_sta_netif != nullptr) {
            esp_netif_dns_info_t dns_info = {};
            if (esp_netif_get_dns_info(g_wifi_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read DHCP DNS info before disabling DHCP");
            }

            esp_err_t err = esp_netif_dhcpc_stop(g_wifi_sta_netif);
            if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
                ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(err));
            }

            err = esp_netif_set_ip_info(g_wifi_sta_netif, &event->ip_info);
            if (err == ESP_OK) {
                if (esp_netif_set_dns_info(g_wifi_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
                    static_ip_configured = true;
                    ESP_LOGI(TAG, "Configured current DHCP IP and DNS as static for next light-sleep restart");
                } else {
                    ESP_LOGW(TAG, "Configured IP, but failed to restore DNS info");
                }
            } else {
                ESP_LOGW(TAG, "Failed to apply static IP info: %s", esp_err_to_name(err));
            }
        }
    }
}

void wifi_stop(void)
{
    // Tear the station down between refreshes: the association is the dominant
    // idle cost, and the RLCD keeps its image without power.
    wifi_connected = false;
    wifi_suppressed = true;
    // Drop modem sleep before releasing the driver. WIFI_PS_MAX_MODEM makes the
    // driver hold ESP_PM_APB_FREQ_MAX for the whole association, which pins the
    // APB/CPU clock at maximum and would stop DFS from reaching its 40 MHz floor
    // even while the radio is stopped between refreshes.
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi stopped to save power");
    } else {
        ESP_LOGW(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
    }
}

bool wifi_start_and_wait(uint32_t timeout_ms)
{
    // esp_wifi_start() re-raises WIFI_EVENT_STA_START, whose handler reconnects.
    wifi_suppressed = false;
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
    }

    // Modem sleep is re-applied here as well as in wifi_init(): esp_wifi_stop()
    // releases the driver, and re-asserting the policy guarantees the association
    // always runs with the power save enabled, whatever the driver restored.
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    uint64_t start_us = esp_timer_get_time();
    const uint64_t timeout_us = static_cast<uint64_t>(timeout_ms) * 1000ULL;
    while (!wifi_connected && (esp_timer_get_time() - start_us) < timeout_us) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return wifi_connected;
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Default event loop already exists from a previous cycle. Skipping creation.");
    }

    g_wifi_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Modem sleep: the radio dozes between beacons (DTIM) while associated and
    // only wakes to receive. WIFI_PS_MAX_MODEM sleeps for the longest interval the
    // AP's DTIM schedule allows, which suits a station that only pulls a small
    // JSON response every few minutes.
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable WiFi modem sleep: %s", esp_err_to_name(ps_err));
    }

    // The board sits next to the access point, so the 20 dBm default TX power is
    // far more than the link needs. Lowering it cuts the current drawn by the
    // power amplifier during each transmitted frame, at no cost to range here.
    esp_err_t tx_err = esp_wifi_set_max_tx_power(REDUCED_WIFI_TX_POWER_QUARTER_DBM);
    if (tx_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to reduce WiFi TX power: %s", esp_err_to_name(tx_err));
    } else {
        ESP_LOGI(TAG, "WiFi TX power limited to %d quarter-dBm", REDUCED_WIFI_TX_POWER_QUARTER_DBM);
    }

    // The station is deliberately left stopped here: it is brought up on demand by
    // wifi_start_and_wait() and torn down again by wifi_stop() after each fetch.
    ESP_LOGI(TAG, "WiFi initialization completed, station stopped until first fetch");
}
