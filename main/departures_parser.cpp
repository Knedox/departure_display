#include "app_shared.h"
#include <time.h>
#include <cctype>
#include <vector>

static time_t utc_mktime_from_tm(struct tm *tm_value)
{
    // timegm() interprets the broken-down time as UTC. Do not use mktime() with
    // setenv("TZ","UTC"): this build uses picolibc, where mktime() ignores the TZ
    // environment variable and would apply the local offset instead.
    return timegm(tm_value);
}

static void format_utc_hhmmss(time_t epoch_seconds, char *hhmmss_out)
{
    if (hhmmss_out == NULL) {
        return;
    }

    struct tm utc_tm;
    if (gmtime_r(&epoch_seconds, &utc_tm) == NULL) {
        strlcpy(hhmmss_out, "--:--:--", 16);
        return;
    }

    snprintf(hhmmss_out, 16, "%02d:%02d:%02d",
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);
}

static bool parse_timezone_offset_from_suffix(const char *tz_part, int64_t &offset_seconds)
{
    offset_seconds = 0;
    if (tz_part == NULL || tz_part[0] == '\0') {
        return false;
    }
    if (tz_part[0] == 'Z') {
        return true;
    }
    if (tz_part[0] != '+' && tz_part[0] != '-') {
        return false;
    }

    int tz_sign = (tz_part[0] == '-') ? -1 : 1;
    int tz_hours = 0;
    int tz_minutes = 0;
    int parsed = 0;

    if (strlen(tz_part) >= 5) {
        parsed = sscanf(tz_part, "%*c%2d%2d", &tz_hours, &tz_minutes);
    }
    if (parsed != 2) {
        parsed = sscanf(tz_part, "%*c%2d:%2d", &tz_hours, &tz_minutes);
    }
    if (parsed != 2) {
        return false;
    }

    offset_seconds = static_cast<int64_t>((tz_hours * 3600 + tz_minutes * 60) * tz_sign);
    return true;
}

// Reports the timezone offset carried by an ISO 8601 timestamp (e.g. "+0200"),
// so the UI can render the wall clock of the data source's timezone.
int64_t get_timezone_offset_seconds_from_iso(const char *iso_time)
{
    if (iso_time == NULL) {
        return 0;
    }

    const char *t_pos = strchr(iso_time, 'T');
    if (t_pos == NULL) {
        return 0;
    }

    const char *tz_pos = strpbrk(t_pos + 1, "Z+-");
    if (tz_pos == NULL) {
        return 0;
    }

    int64_t offset_seconds = 0;
    if (!parse_timezone_offset_from_suffix(tz_pos, offset_seconds)) {
        return 0;
    }

    return offset_seconds;
}

int64_t parse_iso_utc_to_epoch(const char *iso_time)
{
    if (iso_time == NULL) {
        return 0;
    }

    const char *t_pos = strchr(iso_time, 'T');
    if (t_pos == NULL) {
        return 0;
    }

    char date_part[12] = {};
    char time_part[12] = {};
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;

    size_t date_len = static_cast<size_t>(t_pos - iso_time);
    if (date_len >= sizeof(date_part)) {
        return 0;
    }
    memcpy(date_part, iso_time, date_len);
    date_part[date_len] = '\0';

    const char *time_start = t_pos + 1;
    const char *tz_pos = strpbrk(time_start, "Z+-");
    size_t time_len = (tz_pos == NULL) ? strlen(time_start) : static_cast<size_t>(tz_pos - time_start);
    if (time_len >= sizeof(time_part)) {
        return 0;
    }
    memcpy(time_part, time_start, time_len);
    time_part[time_len] = '\0';

    if (sscanf(date_part, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        return 0;
    }
    if (sscanf(time_part, "%2d:%2d:%2d", &hour, &minute, &second) != 3) {
        return 0;
    }

    int64_t tz_offset_seconds = 0;
    if (tz_pos != NULL && !parse_timezone_offset_from_suffix(tz_pos, tz_offset_seconds)) {
        return 0;
    }

    struct tm utc_tm = {};
    utc_tm.tm_year = year - 1900;
    utc_tm.tm_mon = month - 1;
    utc_tm.tm_mday = day;
    utc_tm.tm_hour = hour;
    utc_tm.tm_min = minute;
    utc_tm.tm_sec = second;
    utc_tm.tm_isdst = 0;

    time_t epoch = utc_mktime_from_tm(&utc_tm);
    if (epoch == (time_t)-1) {
        return 0;
    }

    if (tz_pos != NULL) {
        current_timezone_offset_seconds = tz_offset_seconds;
    }

    return static_cast<int64_t>(epoch - tz_offset_seconds);
}

void parse_time_from_iso(const char *iso_time, char *hhmmss_out)
{
    if (iso_time == NULL || hhmmss_out == NULL) {
        strlcpy(hhmmss_out, "--:--:--", 16);
        return;
    }

    // Report the timestamp in GMT, i.e. the same basis as current_time, so that
    // the rendered time and the countdown always agree.
    int64_t epoch_seconds = parse_iso_utc_to_epoch(iso_time);
    if (epoch_seconds <= 0) {
        strlcpy(hhmmss_out, "--:--:--", 16);
        return;
    }

    format_utc_hhmmss(static_cast<time_t>(epoch_seconds), hhmmss_out);
}

static void decode_struct_string(char *str, size_t max_len)
{
    if (str == nullptr || strlen(str) == 0) {
        return;
    }

    std::string input(str);
    std::string output;
    output.reserve(input.length());

    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '\\' && i + 1 < input.length()) {
            char escaped = input[i + 1];

            if (escaped == 'u' && i + 5 < input.length()) {
                std::string hex_str = input.substr(i + 2, 4);
                unsigned int cp = 0;
                std::stringstream ss;
                ss << std::hex << hex_str;
                ss >> cp;

                if (cp <= 0x7F) {
                    output += static_cast<char>(cp);
                } else if (cp <= 0x7FF) {
                    output += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                    output += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    output += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                    output += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    output += static_cast<char>(0x80 | (cp & 0x3F));
                }
                i += 5;
                continue;
            }

            switch (escaped) {
                case '/': output += '/'; ++i; continue;
                case '\\': output += '\\'; ++i; continue;
                case '"': output += '"'; ++i; continue;
                case 'n': output += '\n'; ++i; continue;
                case 'r': output += '\r'; ++i; continue;
                case 't': output += '\t'; ++i; continue;
                case 'b': output += '\b'; ++i; continue;
                case 'f': output += '\f'; ++i; continue;
                default: output += escaped; ++i; continue;
            }
        }

        output += input[i];
    }

    strncpy(str, output.c_str(), max_len - 1);
    str[max_len - 1] = '\0';
}

static void normalize_destination_string(char *str, size_t max_len)
{
    if (str == nullptr || max_len == 0) {
        return;
    }

    std::string value(str);
    std::string cleaned;
    cleaned.reserve(value.length());

    for (char ch : value) {
        if (ch == ',') {
            continue;
        }
        cleaned += ch;
    }

    std::vector<std::string> parts;
    std::string current;
    for (char ch : cleaned) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    if (parts.size() >= 2) {
        std::swap(parts[0], parts[1]);
    }

    std::string swapped;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            swapped += ' ';
        }
        swapped += parts[i];
    }

    strncpy(str, swapped.c_str(), max_len - 1);
    str[max_len - 1] = '\0';
}

void parse_api_response(const char *json_response)
{
    num_departures = 0;

    if (json_response == NULL) {
        ESP_LOGW(TAG, "Empty API response");
        return;
    }

    const char *board_start = strstr(json_response, "\"stationboard\":");
    if (board_start == NULL) {
        ESP_LOGW(TAG, "No stationboard in response");
        return;
    }

    const char *array_start = strchr(board_start, '[');
    if (array_start == NULL) {
        return;
    }

    const char *current = array_start + 1;
    int departure_count = 0;

    while (current != NULL && departure_count < MAX_DEPARTURES) {
        current = strchr(current, '{');
        if (current == NULL) {
            break;
        }

        const char *obj_end = strchr(current, '}');
        if (obj_end == NULL) {
            break;
        }

        const char *cat_ptr = strstr(current, "\"category\":\"");
        char category[4] = "";
        if (cat_ptr != NULL) {
            sscanf(cat_ptr, "\"category\":\"%3[^\"]", category);
        }

        const char *num_ptr = strstr(current, "\"number\":\"");
        char number[8] = "";
        if (num_ptr != NULL) {
            sscanf(num_ptr, "\"number\":\"%7[^\"]", number);
            char *num_start = number;
            while ((*num_start == '0' || *num_start == ' ') && *(num_start + 1) != '\0') {
                num_start++;
            }
            if (num_start != number) {
                memmove(number, num_start, strlen(num_start) + 1);
            }
        }

        const char *to_ptr = strstr(current, "\"to\":\"");
        char destination[32] = "";
        if (to_ptr != NULL) {
            sscanf(to_ptr, "\"to\":\"%31[^\"]", destination);
        }

        char iso_time[32] = "";
        const char *prog_ptr = strstr(current, "\"prognosis\"");
        if (prog_ptr != NULL) {
            const char *dep_ptr = strstr(prog_ptr, "\"departure\":\"");
            if (dep_ptr != NULL) {
                sscanf(dep_ptr, "\"departure\":\"%31[^\"]", iso_time);
            }
        }

        if (iso_time[0] == '\0') {
            const char *dep_ptr = strstr(current, "\"departure\":\"");
            if (dep_ptr != NULL) {
                sscanf(dep_ptr, "\"departure\":\"%31[^\"]", iso_time);
            }
        }

        if (strlen(category) > 0 && strlen(number) > 0 && strlen(destination) > 0) {
            snprintf(departures[departure_count].line, sizeof(departures[departure_count].line), "%s%s", category, number);
            decode_struct_string(destination, sizeof(destination));
            normalize_destination_string(destination, sizeof(destination));
            strlcpy(departures[departure_count].destination, destination, sizeof(departures[departure_count].destination));

            char hhmm[8] = {};
            parse_time_from_iso(iso_time, hhmm);
            strlcpy(departures[departure_count].scheduled_time, hhmm, sizeof(departures[departure_count].scheduled_time));
            departures[departure_count].scheduled_epoch = parse_iso_utc_to_epoch(iso_time);
            strlcpy(departures[departure_count].status, "OK", sizeof(departures[departure_count].status));

            departure_count++;
            num_departures = departure_count;
        }

        current = obj_end + 1;
    }

    num_departures = departure_count;
    ESP_LOGI(TAG, "Total departures parsed: %d", num_departures);
}
