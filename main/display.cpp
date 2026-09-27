#include "app_shared.h"

static constexpr uint64_t WIFI_FETCH_INTERVAL_US = 5ULL * 60ULL * 1000000ULL;
// Departures closer than this are shown as "now". This is half of MINUTE_SECONDS:
// with round-half-up the "1" bucket starts at 30 s, so ending "now" at the same
// point keeps the transition continuous (no 1-minute jump at the boundary).
static constexpr int NOW_THRESHOLD_SECONDS = 30;
// Width of one displayed minute, and the half-width used for rounding.
static constexpr int MINUTE_SECONDS = 60;
static constexpr int MINUTE_ROUNDING_SECONDS = MINUTE_SECONDS / 2;

// Row layout. The logical drawing surface is 400 wide x 300 tall: for U8G2_R1
// u8g2 swaps the panel's native dimensions, giving height = pixel_width = 300
// (u8g2_setup.c:236-237). Rows begin below the header and advance by ROW_PITCH.
// MAX_DEPARTURES may exceed what fits, so the render loop stops once the next
// row would fall off the panel instead of drawing the surplus off-screen.
static constexpr int ROW_TOP_Y = 60;
static constexpr int ROW_PITCH = 30;
static constexpr int ROW_LAST_BASELINE_Y = 270;  // bottom edge minus one pitch

u8g2_st7305_t g_u8g2_lcd;
static adc_cali_handle_t g_adc_cali_handle = nullptr;
static adc_oneshot_unit_handle_t g_adc1_handle = nullptr;
static uint64_t last_wifi_fetch_us = 0;
static uint64_t last_clock_update_us = 0;

static void update_current_time_from_epoch(void)
{
    if (current_epoch_seconds <= 0) {
        return;
    }

    // Render the wall clock of the data source's timezone so it lines up with the
    // departure times, which are rendered the same way.
    int64_t display_epoch_seconds = current_epoch_seconds + current_timezone_offset_seconds;
    time_t display_epoch = static_cast<time_t>(display_epoch_seconds);
    struct tm wall_tm = {};
    gmtime_r(&display_epoch, &wall_tm);
    snprintf(current_time, sizeof(current_time), "%02d:%02d:%02d",
             wall_tm.tm_hour, wall_tm.tm_min, wall_tm.tm_sec);
}

static void advance_current_epoch_time(uint64_t now_us)
{
    if (current_epoch_seconds <= 0) {
        return;
    }

    // Only the current monotonic time is a valid reference here: last_clock_update_us
    // is deliberately set ahead of it (to the moment the fetch completed) so the
    // time spent inside the HTTP request is not counted twice.
    if (now_us < last_clock_update_us) {
        return;
    }

    uint64_t elapsed_us = now_us - last_clock_update_us;
    uint32_t elapsed_seconds = static_cast<uint32_t>(elapsed_us / 1000000ULL);
    if (elapsed_seconds == 0) {
        return;
    }

    current_epoch_seconds += static_cast<int64_t>(elapsed_seconds);
    last_clock_update_us += static_cast<uint64_t>(elapsed_seconds) * 1000000ULL;
    update_current_time_from_epoch();
}

static void battery_adc_init(void)
{
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali_handle));

    adc_oneshot_unit_init_cfg_t init_config1 = {};
    init_config1.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &g_adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    chan_cfg.atten = ADC_ATTEN_DB_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc1_handle, ADC_CHANNEL_3, &chan_cfg));
}

static float battery_get_voltage(void)
{
    int raw_value = 0;
    int voltage_mV = 0;
    if (g_adc1_handle == nullptr || g_adc_cali_handle == nullptr) {
        return 0.0f;
    }

    esp_err_t err = adc_oneshot_read(g_adc1_handle, ADC_CHANNEL_3, &raw_value);
    if (err == ESP_OK) {
        adc_cali_raw_to_voltage(g_adc_cali_handle, raw_value, &voltage_mV);
        return voltage_mV * 0.001f * 3.0f;
    }
    return 0.0f;
}

static uint8_t battery_get_percent(void)
{
    float vol = battery_get_voltage();
    if (vol <= 0.0f) {
        return 0;
    }
    if (vol < 3.0f) {
        return 0;
    }
    if (vol > 4.12f) {
        return 100;
    }
    float pct = ((vol - 3.0f) / 1.12f) * 100.0f;
    return (uint8_t)pct;
}

static void draw_battery_icon(u8g2_t *u8g2, int x, int y, uint8_t percent)
{
    const int icon_width = 18;
    const int icon_height = 20;
    const int body_w = 10;
    const int body_h = 16;
    const int cap_w = 6;
    const int cap_h = 2;
    const int body_x = x + (icon_width - body_w) / 2;
    const int body_y = y + icon_height - body_h - 2;
    const int cap_x = x + (icon_width - cap_w) / 2;
    const int cap_y = y;

    u8g2_DrawBox(u8g2, cap_x, cap_y, cap_w, cap_h);
    u8g2_DrawFrame(u8g2, body_x, body_y, body_w, body_h);

    int fill_h = (percent * (body_h - 4)) / 100;
    if (fill_h > 0) {
        u8g2_DrawBox(u8g2,
                     body_x + 2,
                     body_y + body_h - 2 - fill_h,
                     body_w - 4,
                     fill_h);
    }
}

static void format_route_badge(const char *line, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (line == nullptr) {
        return;
    }

    size_t idx = 0;
    for (const char *p = line; *p != '\0' && idx + 1 < out_size; ++p) {
        if (*p >= '0' && *p <= '9') {
            out[idx++] = *p;
        }
    }
    out[idx] = '\0';

    if (out[0] == '\0') {
        strlcpy(out, line, out_size);
    }
}

static void truncate_destination_to_width(u8g2_t *u8g2, const char *src, char *out, size_t out_size, int max_width)
{
    if (out == nullptr || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (src == nullptr || u8g2 == nullptr || max_width <= 0) {
        return;
    }

    const char *p = src;
    bool has_output = false;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }

        const char *word_start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
        size_t word_len = static_cast<size_t>(p - word_start);
        if (word_len == 0) {
            break;
        }

        char word[64];
        if (word_len >= sizeof(word)) {
            word_len = sizeof(word) - 1;
        }
        memcpy(word, word_start, word_len);
        word[word_len] = '\0';

        char candidate[128];
        if (has_output) {
            snprintf(candidate, sizeof(candidate), "%s %s", out, word);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", word);
        }

        if (u8g2_GetStrWidth(u8g2, candidate) > max_width) {
            break;
        }

        if (has_output) {
            size_t current_len = strlen(out);
            if (current_len + 1 + word_len + 1 > out_size) {
                break;
            }
            strlcat(out, " ", out_size);
            strlcat(out, word, out_size);
        } else {
            strlcpy(out, word, out_size);
        }
        has_output = true;
    }
}

static int seconds_until_epoch(int64_t current_epoch, int64_t departure_epoch)
{
    if (current_epoch <= 0 || departure_epoch <= 0) {
        return -1;
    }

    int64_t diff = departure_epoch - current_epoch;
    return (diff < 0) ? -1 : static_cast<int>(diff);
}

static int departure_sort_key(const Departure &departure)
{
    if (current_epoch_seconds <= 0 || departure.scheduled_epoch <= 0) {
        return 2147483647;
    }
    int secs = seconds_until_epoch(current_epoch_seconds, departure.scheduled_epoch);
    return (secs < 0) ? 2147483647 : secs;
}

static void sort_departures_for_display(void)
{
    if (num_departures <= 1 || current_epoch_seconds <= 0) {
        return;
    }

    for (int i = 1; i < num_departures; ++i) {
        Departure current = departures[i];
        int current_key = departure_sort_key(current);
        int j = i - 1;

        while (j >= 0) {
            int prev_key = departure_sort_key(departures[j]);
            if (prev_key <= current_key) {
                break;
            }
            departures[j + 1] = departures[j];
            --j;
        }

        departures[j + 1] = current;
    }
}

u8g2_t *U8g2_InitDisplayHandle(void)
{
    ESP_LOGI(TAG, "=== Display Task Started ===");

    u8g2_st7305_config_t config = u8g2_st7305_default_config();
    config.mosi_io = RLCD_MOSI_PIN;
    config.sclk_io = RLCD_SCK_PIN;
    config.dc_io = RLCD_DC_PIN;
    config.cs_io = RLCD_CS_PIN;
    config.reset_io = RLCD_RST_PIN;
    config.rotation = U8G2_R1;
    config.tile_buf_height = U8G2_ST7305_TILE_BUF_FULL;

    ESP_LOGI(TAG, "Initializing u8g2 display");
    ESP_ERROR_CHECK(u8g2_st7305_init(&g_u8g2_lcd, &config));
    u8g2_t *u8g2 = u8g2_st7305_get_u8g2(&g_u8g2_lcd);
    ESP_LOGI(TAG, "u8g2 display initialized successfully");

    battery_adc_init();
    return u8g2;
}

void U8g2_RefreshDisplayState(uint64_t now_us)
{
    bool needs_wifi_refresh = (last_wifi_fetch_us == 0) || ((now_us - last_wifi_fetch_us) >= WIFI_FETCH_INTERVAL_US);

    if (needs_wifi_refresh) {
        ESP_LOGI(TAG, "Fetching live departure data (WiFi refresh interval reached)");
        bool updated = fetch_departures();
        if (updated) {
            // Only arm the interval after a real update, otherwise a failed fetch
            // would suppress retries for the whole refresh period.
            last_wifi_fetch_us = esp_timer_get_time();
            last_clock_update_us = last_wifi_fetch_us;
            update_current_time_from_epoch();
        } else {
            ESP_LOGW(TAG, "Departure refresh failed; retrying on next cycle");
        }
    }

    if (current_epoch_seconds > 0) {
        advance_current_epoch_time(now_us);
    }

    sort_departures_for_display();
}

void U8g2_RenderDepartureFrame(u8g2_t *u8g2)
{
#if RLCD_USE_PANEL_LPM
    /* The panel may be sitting in LPM (1 Hz) from the previous frame. Writes
     * issued in LPM can be delayed by up to one refresh period, so return to
     * HPM (32 Hz) before touching frame memory. */
    u8g2_st7305_set_high_power_mode(&g_u8g2_lcd);
#endif

    if (u8g2 == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Updating display buffer");
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawBox(u8g2, 0, 0, 400, 300);
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);

    char display_title[64];
    snprintf(display_title, sizeof(display_title), "%s", STATION_NAME);
    u8g2_DrawStr(u8g2, 10, 28, display_title);
    ESP_LOGI(TAG, "DISPLAY REFRESH: station=%s current_time=%s battery=%u%%", display_title, current_time, battery_get_percent());

    char display_clock[6];
    strlcpy(display_clock, current_time, sizeof(display_clock));
    display_clock[5] = '\0';

    int time_w = u8g2_GetStrWidth(u8g2, display_clock);
    const int icon_w = 21;
    const int spacing = 1;
    int time_x = 400 - time_w - 10;
    int icon_x = time_x - icon_w - spacing + 2;
    int icon_y = 10;
    draw_battery_icon(u8g2, icon_x, icon_y, battery_get_percent());
    u8g2_DrawStr(u8g2, time_x, 28, display_clock);
    u8g2_DrawHLine(u8g2, 10, 32, 380);

    int y = ROW_TOP_Y;
    ESP_LOGI(TAG, "Drawing %d departures on display", num_departures);
    int visible_rows = 0;
    for (int i = 0; i < num_departures; i++) {
        if (y > ROW_LAST_BASELINE_Y) {
            // No room left on the panel; the remaining departures stay parsed
            // and sorted but are not drawn.
            break;
        }
        char rendered_time[16];
        int secs = (current_epoch_seconds > 0 && departures[i].scheduled_epoch > 0)
            ? seconds_until_epoch(current_epoch_seconds, departures[i].scheduled_epoch)
            : -1;
        if (secs < 0) {
            ESP_LOGI(TAG, "SKIP_ROW[%d] expired departure line=%s dest=%s scheduled=%s current_time=%s secs=%d",
                     i + 1,
                     departures[i].line,
                     departures[i].destination,
                     departures[i].scheduled_time,
                     current_time,
                     secs);
            continue;
        }
        visible_rows++;

        if (secs < NOW_THRESHOLD_SECONDS) {
            snprintf(rendered_time, sizeof(rendered_time), "  now");
        } else {
            // Round to the nearest minute. This halves the worst-case rounding
            // error compared with flooring or ceiling (30 s instead of 59 s), at
            // the cost of occasionally reading slightly optimistic at the top of
            // a bucket. NOW_THRESHOLD_SECONDS keeps the boundary continuous.
            int display_minutes = (secs + MINUTE_ROUNDING_SECONDS) / MINUTE_SECONDS;
            snprintf(rendered_time, sizeof(rendered_time), "  %d", display_minutes);
        }

        ESP_LOGI(TAG, "ROW[%d] countdown_sec=%d line=%s dest=%s scheduled=%s rendered_text='%s'", i + 1,
                 secs,
                 departures[i].line,
                 departures[i].destination,
                 departures[i].scheduled_time,
                 rendered_time);

        char line_badge[8];
        format_route_badge(departures[i].line, line_badge, sizeof(line_badge));

        const int badge_x = 10;
        const int badge_w = 32;
        const int badge_h = 18;
        const int badge_y = y - 18;
        const int dest_x = badge_x + badge_w + 8;

        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawRBox(u8g2, badge_x, badge_y, badge_w, badge_h, 4);
        u8g2_SetDrawColor(u8g2, 1);

        u8g2_SetFont(u8g2, u8g2_font_helvB12_tf);
        int text_w = u8g2_GetStrWidth(u8g2, line_badge);
        int badge_text_x = badge_x + (badge_w - text_w) / 2;
        int badge_text_y = badge_y + badge_h - 3;
        u8g2_DrawUTF8(u8g2, badge_text_x, badge_text_y, line_badge);

        u8g2_SetDrawColor(u8g2, 0);
        u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);

        int time_right_x = 400 - 10;
        int time_text_w = u8g2_GetStrWidth(u8g2, rendered_time);
        int min_suffix_w = 0;
        u8g2_SetFont(u8g2, u8g2_font_helvB10_tf);
        if (secs >= NOW_THRESHOLD_SECONDS) {
            min_suffix_w = u8g2_GetStrWidth(u8g2, "min") + 2;
        }
        u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);

        int time_block_w = time_text_w + min_suffix_w;
        int max_destination_w = time_right_x - dest_x - 8 - time_block_w;
        if (max_destination_w < 20) {
            max_destination_w = 20;
        }

        char limited_destination[64];
        truncate_destination_to_width(u8g2, departures[i].destination, limited_destination, sizeof(limited_destination), max_destination_w);
        u8g2_DrawUTF8(u8g2, dest_x, y, limited_destination);

        int time_draw_x = time_right_x - time_block_w;
        u8g2_DrawUTF8(u8g2, time_draw_x, y, rendered_time);

        u8g2_SetFont(u8g2, u8g2_font_helvB10_tf);
        if (secs >= NOW_THRESHOLD_SECONDS) {
            int min_x = time_draw_x + time_text_w + 2;
            if (min_x < 400 - 18) {
                u8g2_DrawStr(u8g2, min_x, y, "min");
            }
        }

        y += ROW_PITCH;
    }

    ESP_LOGI(TAG, "Visible departures after filtering: %d", visible_rows);
    ESP_LOGI(TAG, "Sending buffer to display");
    u8g2_SendBuffer(u8g2);

#if RLCD_USE_PANEL_LPM
    /* Image is now in frame memory. Drop to LPM (1 Hz self-refresh): the RLCD
     * holds the image with no backlight and the panel now re-drives it ~1/32
     * as often. This happens after SendBuffer so the frame is never written
     * while the panel is in the slow mode. */
    u8g2_st7305_set_low_power_mode(&g_u8g2_lcd);
    ESP_LOGI(TAG, "Panel switched to LPM (1 Hz self-refresh)");
#endif
}
