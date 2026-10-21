#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Panel pin map lives in u8g2_st7305_default_config(), not here. */

/* Panel SPI clock, Hz. 15000 bytes/frame, so this is the data floor:
 * 24/40/80 MHz = 5.0/3.0/1.5 ms. 80 MHz corrupted the image while every driver
 * counter stayed clean - a write-only bus cannot detect dropped bytes. 60 MHz
 * measured the same as 40 (divider rounding). Raise only with a scope on SCK. */
#define RLCD_SPI_CLOCK_HZ 40000000

/* DFS + automatic light sleep.
 *
 * The display task sleeps for tens of seconds between refreshes, so almost all
 * the energy goes on powering the chip idle (~6.5 ms of display work against a
 * 30 s interval). Tickless idle is what turns the long vTaskDelay() into a real
 * light sleep instead of a busy wait at full clock.
 *
 * RST must stay driven while asleep or a glitch can wedge the panel in a state
 * a pin reset does not recover from; see u8g2_st7305_init(). */
#define RLCD_USE_LIGHT_SLEEP 1

/* Runtime log level. WARN suppresses the per-frame INFO chatter and its UART
 * and CPU cost; raise to ESP_LOG_INFO for detail. */
#define RLCD_LOG_LEVEL ESP_LOG_WARN

/* WiFi TX power in quarter-dBm, as esp_wifi_set_max_tx_power() expects.
 * 40 = 10 dBm, vs the 80 default. Raise towards 80 if the link is unreliable. */
#define REDUCED_WIFI_TX_POWER_QUARTER_DBM 40

/* Night-time deep sleep.
 *
 * The device deep sleeps in chunks through the night window and wakes up
 * roughly hourly to re-read the clock, so a single long sleep is not needed
 * and a wrong sleep length cannot strand the device for hours.
 *
 * The window is expressed in the local time of the data source (Zurich), taken
 * from the timezone offset the API reports in its ISO timestamps. The window
 * may wrap midnight, which is the default: 19:00 -> 07:00.
 *
 * The chip reboots on every wake, so the display is re-initialised and the
 * clock is re-fetched from the API each time. Nothing is retained across the
 * sleep. */
#define RLCD_NIGHT_SLEEP_ENABLED 1

/* Start of the night window, in local hours (0..23). */
#define RLCD_NIGHT_SLEEP_START_HOUR 19

/* End of the night window, in local hours (0..23). */
#define RLCD_NIGHT_SLEEP_END_HOUR 7

/* Length of one deep sleep chunk. The chip wakes up after this long, re-reads
 * the clock and decides again, so the real sleep is a series of chunks. The
 * last chunk of the night is trimmed to land exactly on the end of the
 * window. */
#define RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS (60 * 60)

#endif
