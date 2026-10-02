#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Panel pin map lives in u8g2_st7305_default_config(), not here. */

/* Panel SPI clock, Hz. 15000 bytes/frame, so this is the data floor:
 * 24/40/80 MHz = 5.0/3.0/1.5 ms. 80 MHz corrupted the image while every driver
 * counter stayed clean - a write-only bus cannot detect dropped bytes. 60 MHz
 * measured the same as 40 (divider rounding). Raise only with a scope on SCK. */
#define RLCD_SPI_CLOCK_HZ 40000000

/* DFS + automatic light sleep. Off until there is a measurement of the idle
 * current with it enabled; see power.cpp. */
#define RLCD_USE_LIGHT_SLEEP 0

/* Runtime log level. WARN suppresses the per-frame INFO chatter and its UART
 * and CPU cost; raise to ESP_LOG_INFO for detail. */
#define RLCD_LOG_LEVEL ESP_LOG_WARN

/* WiFi TX power in quarter-dBm, as esp_wifi_set_max_tx_power() expects.
 * 40 = 10 dBm, vs the 80 default. Raise towards 80 if the link is unreliable. */
#define REDUCED_WIFI_TX_POWER_QUARTER_DBM 40

#endif
