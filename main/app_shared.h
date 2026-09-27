#pragma once

#include <stdio.h>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_http_client.h>
#include <esp_sleep.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_pm.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include "user_config.h"
#include "u8g2_st7305.h"

extern const char *TAG;
extern const char *WIFI_SSID;
extern const char *WIFI_PASS;
extern const char *API_URL;
extern const char *STATION_NAME;

typedef struct {
    char line[16];
    char destination[32];
    char scheduled_time[16];
    int64_t scheduled_epoch;
    char status[16];
} Departure;

/* Number of departures requested from the API and retained in RAM. Only the
 * subset that fits the panel is drawn; the rest are kept so the list can be
 * sorted and filtered (e.g. dropping already-departed services) without having
 * to fall back to fewer rows. */
#define MAX_DEPARTURES 20

extern Departure departures[MAX_DEPARTURES];
extern int num_departures;
extern bool wifi_connected;
extern esp_netif_t *g_wifi_sta_netif;
extern bool static_ip_configured;
extern char current_time[16];
extern int64_t current_epoch_seconds;
// Timezone offset of the data source (from the API's ISO timestamps, e.g. +0200).
// Used only to render the wall clock; all stored times stay in GMT.
extern int64_t current_timezone_offset_seconds;

int64_t parse_iso_utc_to_epoch(const char *iso_time);
int64_t get_timezone_offset_seconds_from_iso(const char *iso_time);
void parse_time_from_iso(const char *iso_time, char *hhmmss_out);
void parse_api_response(const char *json_response);

void wifi_init(void);
void wifi_stop(void);
bool wifi_start_and_wait(uint32_t timeout_ms);
bool fetch_departures(void);
u8g2_t *U8g2_InitDisplayHandle(void);
void U8g2_RefreshDisplayState(uint64_t now_us);
void U8g2_RenderDepartureFrame(u8g2_t *u8g2);
void configure_power_management(void);
