#include "app_shared.h"

Departure departures[MAX_DEPARTURES];
int num_departures = 0;
char current_time[16] = "--:--:--";
int64_t current_epoch_seconds = 0;
int64_t current_timezone_offset_seconds = 0;

typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t filled;
} http_response_t;

static void set_current_time_from_epoch(time_t epoch_seconds)
{
    if (epoch_seconds == (time_t)-1) {
        return;
    }

    current_epoch_seconds = static_cast<int64_t>(epoch_seconds);
    // The epoch is the GMT instant. current_time is the wall clock of the data
    // source's timezone, so the clock lines up with the departure times.
    time_t display_epoch = epoch_seconds + static_cast<time_t>(current_timezone_offset_seconds);
    struct tm wall_tm = {};
    gmtime_r(&display_epoch, &wall_tm);
    snprintf(current_time, sizeof(current_time), "%02d:%02d:%02d",
             wall_tm.tm_hour, wall_tm.tm_min, wall_tm.tm_sec);
}

static void set_current_time_from_http_date(const char *date_header)
{
    if (date_header == NULL) {
        return;
    }

    struct tm utc_tm = {};
    char buffer[64] = {};
    strlcpy(buffer, date_header, sizeof(buffer));

    char *parsed = strptime(buffer, "%a, %d %b %Y %H:%M:%S GMT", &utc_tm);
    if (parsed == NULL) {
        return;
    }

    // timegm() interprets the broken-down time as UTC. Do not use mktime() with
    // setenv("TZ","UTC"): this build uses picolibc, where mktime() ignores the TZ
    // environment variable and would apply the local offset instead.
    time_t epoch_seconds = timegm(&utc_tm);

    if (epoch_seconds != (time_t)-1) {
        set_current_time_from_epoch(epoch_seconds);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *rsp = (http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (evt->header_key && evt->header_value && strcmp(evt->header_key, "Date") == 0) {
                set_current_time_from_http_date(evt->header_value);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (rsp && rsp->buffer) {
                size_t space = rsp->buffer_size - rsp->filled;
                size_t copy_len = (evt->data_len < space) ? evt->data_len : space;
                memcpy(rsp->buffer + rsp->filled, evt->data, copy_len);
                rsp->filled += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (rsp && rsp->buffer && rsp->filled < rsp->buffer_size) {
                rsp->buffer[rsp->filled] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

bool fetch_departures(void)
{
    ESP_LOGI(TAG, "Fetching departures from API");

    if (!wifi_connected) {
        ESP_LOGI(TAG, "WiFi down, bringing the station up for this refresh...");
        if (!wifi_start_and_wait(15 * 1000)) {
            ESP_LOGW(TAG, "WiFi did not connect in time; skipping API refresh");
            return false;
        }
    }

    ESP_LOGI(TAG, "WiFi connected, proceeding...");

    static char response_buffer[4096 * 16];
    http_response_t response = {
        .buffer = response_buffer,
        .buffer_size = sizeof(response_buffer),
        .filled = 0
    };

    esp_http_client_config_t config = {};
    config.url = API_URL;
    config.event_handler = http_event_handler;
    config.user_data = &response;
    config.buffer_size = 2048;
    config.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "User-Agent", "ESP32-Departures/1.0");

    char full_url[512];
    char station_query[128];
    strlcpy(station_query, STATION_NAME, sizeof(station_query));
    for (char *p = station_query; *p != '\0'; ++p) {
        if (*p == ' ') {
            *p = '%';
            memmove(p + 3, p + 1, strlen(p + 1) + 1);
            p[1] = '2';
            p[2] = '0';
        }
    }
    static const char *fields = "&fields[]=stationboard/category&fields[]=stationboard/number&fields[]=stationboard/to&fields[]=stationboard/stop/prognosis/departure";
    snprintf(full_url, sizeof(full_url),
             "%s?station=%s&limit=%d&type=departure%s",
             API_URL, station_query, MAX_DEPARTURES, fields);
    esp_http_client_set_url(client, full_url);

    esp_err_t err = esp_http_client_perform(client);
    bool updated = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET successful, status = %d, received %zu bytes",
                 status, response.filled);

        if (status == 200 && response.filled > 0) {
            parse_api_response(response_buffer);
            updated = true;
        } else {
            ESP_LOGW(TAG, "Unexpected HTTP status %d or empty body; keeping previous departures", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // The data is in hand, so the radio is no longer needed until the next
    // refresh. Shut it down regardless of whether the request succeeded.
    wifi_stop();

    return updated;
}
