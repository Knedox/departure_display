#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Panel pin map lives in u8g2_st7305_default_config(), not here. */

/* Panel SPI clock, Hz. 15000 bytes/frame, so this is the data floor:
 * 24/40/80 MHz = 5.0/3.0/1.5 ms. 80 MHz corrupted the image while every driver
 * counter stayed clean - a write-only bus cannot detect dropped bytes. 60 MHz
 * measured the same as 40 (divider rounding). Raise only with a scope on SCK. */
#define RLCD_SPI_CLOCK_HZ 40000000

/* ST7305 panel self-refresh mode.
 * 1: after each frame is written, switch the panel to LPM (0x39, 1 Hz
 *    self-refresh) so the static image is held at roughly 1/32 of the HPM
 *    refresh energy. The image is retained in frame memory, so it should look
 *    identical. The panel is switched back to HPM (0x38, 32 Hz) before the
 *    next frame is drawn.
 * 0: leave the panel in HPM permanently (previous behaviour).
 * Set to 0 to disable if the display shows artefacts after a refresh. */
#define RLCD_USE_PANEL_LPM 1

/* Runtime text log level, applied in app_main(). The per-frame INFO chatter
 * (several lines per departure row on every refresh) is suppressed at this
 * level, removing the UART traffic and its CPU cost from each wake. Raise to
 * ESP_LOG_INFO to get the detailed output back. */
#define RLCD_LOG_LEVEL ESP_LOG_WARN

/* WiFi transmit power limit in quarter-dBm units, as expected by
 * esp_wifi_set_max_tx_power(). 40 = 10 dBm. The default is 20 dBm (80); the
 * board normally sits next to its access point, so the lower setting cuts the
 * current drawn by the power amplifier with no practical loss of range.
 * Raise towards 80 if the link is unreliable at distance. */
#define REDUCED_WIFI_TX_POWER_QUARTER_DBM 40

#endif
