#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Panel SPI clock, Hz. 80 MHz corrupted the image; 60 MHz matched 40. */
#define RLCD_SPI_CLOCK_HZ 40000000

/* DFS + automatic light sleep (40-160 MHz). */
#define RLCD_USE_LIGHT_SLEEP 0

/* Runtime log level; raise to ESP_LOG_INFO for per-frame detail. */
#define RLCD_LOG_LEVEL ESP_LOG_WARN

/* WiFi TX power in quarter-dBm (40 = 10 dBm); raise towards 80 if needed. */
#define REDUCED_WIFI_TX_POWER_QUARTER_DBM 40

/* Night-time deep sleep in one-hour chunks through the window (local time). */
#define RLCD_NIGHT_SLEEP_ENABLED 0

/* Window start/end in local hours (0..23); start > end wraps midnight. */
#define RLCD_NIGHT_SLEEP_START_HOUR 19
#define RLCD_NIGHT_SLEEP_END_HOUR 7

/* Length of one deep sleep chunk. */
#define RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS (60 * 60)

#endif
