# Zurich Departures Display

A battery-friendly departure board for public transport in the Zurich area, built on the
Waveshare **ESP32-S3-RLCD-4.2** board. The device fetches live departures for a configured
station (currently *Glattpark*) and renders them on a reflective LCD, which needs no backlight.
The panel is not bistable - it does not keep its image with no power applied, it simply needs
very little power to hold one. The whole device draws roughly 10 mW, which lets a single
battery charge last about a month.

![Departure display running on the ESP32-S3-RLCD-4.2 board](doc/departure_display_running.jpg)

## What it does

- Fetches up to 20 departures for a station from the public
  [transport.opendata.ch](https://transport.opendata.ch) `stationboard` API every 5 minutes.
- Renders them in rows: line number in a badge, destination, and a countdown to departure in
  minutes (`now` once under 30 seconds). The render loop stops once the next row would fall off
  the bottom of the panel.
- Shows the wall clock of the data source's timezone and a battery gauge in the header, plus
  the station name.
- Drops already-departed services and keeps the list chronological.
- Drives the WiFi radio only around a fetch, and powers the station down between refreshes.
- Deep sleeps during the night (19:00 to 07:00 local time by default) in one-hour chunks.

## Hardware

| Item | Detail |
| --- | --- |
| Board | Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3, Xtensa dual-core, 240 MHz) |
| Display | 4.2" reflective LCD (RLCD), 400x300 px, ST7305 controller, SPI |
| Flash | 16 MB. `sdkconfig.defaults` says QIO, the active `sdkconfig` resolves to DIO |
| PSRAM | Disabled - an idle 80 MHz octal PSRAM interface costs several mA, and the only large allocation (the ~15 KB framebuffer) fits internal RAM |
| Regulator | TI TPS63020 buck-boost; its PS/SYNC pin must be pulled low for low-power mode (see below) |
| Battery sense | On-board divider on GPIO4 / ADC1 channel 3 (3.0 V = empty, 4.12 V = full) |
| Power draw | ~10 mW, giving roughly one month of runtime per battery charge |

### Display wiring

The pin map lives in `u8g2_st7305_default_config()`.

| Signal | GPIO |
| --- | --- |
| DC | 5 |
| CS | 40 |
| SCK | 11 |
| MOSI | 12 |
| RST | 41 |

The panel is driven over SPI3 at 40 MHz (`RLCD_SPI_CLOCK_HZ`) in `U8G2_R1` rotation. 24/40/80 MHz
would give 5.0/3.0/1.5 ms per frame, but 80 MHz corrupted the image while every driver counter
stayed clean - a write-only bus cannot detect dropped bytes.

### Low-power regulator requirement

The ~10 mW figure above depends on the TPS63020 running in power-save mode. Its `PS/SYNC` pin
selects between forced fixed-frequency PWM (high, the board default) and power-save mode (low),
which skips switching cycles at light load. Left high, the converter burns considerably more
than the rest of the system at idle and the month of runtime is not reachable.

To get low-power mode, cut the trace tying `PS/SYNC` to its pull-up and tie the pin to ground.
This is a hardware modification and cannot be done from firmware.

## Software

- **Framework:** ESP-IDF (see `.devcontainer/Dockerfile`, target `esp32s3`)
- **Graphics:** [u8g2](https://github.com/olikraus/u8g2), with a custom ST7305 panel driver
  in `components/u8g2_st7305`

### Power management

The firmware is tuned for long battery life. Each point below is expanded in the comment at
the code it refers to.

- Dynamic frequency scaling between 40 MHz and 160 MHz, with automatic light sleep
  (`main/power.cpp`). This needs `CONFIG_PM_ENABLE` and `CONFIG_FREERTOS_USE_TICKLESS_IDLE`;
  without them the `esp_pm` API links as no-op stubs and the device runs at a fixed 160 MHz.
- The panel is switched to LPM (1 Hz self-refresh, command `0x39`) at init, so a static image
  is held far more cheaply than the 32 Hz refresh. The LPM *voltage values* are still the HPM
  set - that table is panel-specific and not in the datasheet - so only part of the intended
  saving is realised.
- WiFi TX power is capped at 10 dBm instead of the 20 dBm default, and the station is stopped
  after every fetch. Modem sleep (`WIFI_PS_MAX_MODEM`) is asserted per association and cleared
  in `wifi_stop()`, because it holds `ESP_PM_APB_FREQ_MAX` and would otherwise pin the clock at
  maximum.
- Bluetooth/WiFi/MDNS/other components are trimmed out via `sdkconfig`, and the task watchdog's
  idle subscription is off, so the chip is not woken for no benefit.
- During light sleep only `RST` is kept driven; `DC`/`CS`/`SCK`/`MOSI` float, giving up part of
  the 200-300 uA that `PM_SLP_DISABLE_GPIO` exists to save. `RST` must stay driven because a
  glitch on a floating `RST` can wedge the ST7305 in a state its charge-pump rails hold, which
  only removing power recovers from.

### Night-time deep sleep

Inside the night window (19:00 to 07:00 local time by default) the ESP32-S3 deep sleeps in
one-hour chunks instead of idling. The panel is switched off before each sleep, and the clock
is re-read from the API after each wake, so nothing is retained across the sleep. Unlike light
sleep, deep sleep holds no GPIO, so `RST` floats while the chip is asleep.

### Source layout

```
main/
  main.cpp             app entry point, display task, refresh interval
  display.cpp          framebuffer rendering, layout, battery ADC
  departures_api.cpp   HTTP fetch, clock sync from HTTP Date header
  departures_parser.cpp JSON parsing, timezone handling, sorting
  wifi_manager.cpp     WiFi station lifecycle and power saving
  power.cpp            DFS / light-sleep configuration
  night_sleep.cpp      night-window decision and deep-sleep entry
  user_config.h        log level, power tuning knobs
  app_shared.h         shared structs and extern declarations
  secrets.h            WiFi credentials (git-ignored, local only)
  secrets.h.example    template for secrets.h with placeholder values
components/
  u8g2_st7305/         ST7305 SPI panel driver plus u8g2 glue
  u8g2/                u8g2 graphics library
```

## Building and flashing

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Or use the provided dev container, which ships the ESP-IDF toolchain and QEMU.

## Configuration

### Credentials

WiFi credentials are kept out of the repository. Create your local file from the committed
template and fill in the placeholders:

```bash
cp main/secrets.h.example main/secrets.h
```

```c
#define WIFI_SSID_VALUE "your-wifi-ssid"
#define WIFI_PASS_VALUE "your-wifi-password"
```

`main/secrets.h` is listed in `.gitignore` and must never be committed;
`main/secrets.h.example` holds the dummy values and is tracked instead. The build fails with a
missing-header error if `main/secrets.h` has not been created yet.

The clock needs no timezone configuration: the offset is taken from the ISO timestamps of each
API response, so DST changes are picked up automatically.

### Other settings

The station and API endpoint are compiled into `main/main.cpp`:

```c
const char *API_URL = "http://transport.opendata.ch/v1/stationboard";
const char *STATION_NAME = "Glattpark";
```

Changing the station requires a rebuild.

Tuning knobs live in `main/user_config.h`:

| Knob | Effect |
| --- | --- |
| `RLCD_SPI_CLOCK_HZ` | Panel SPI clock in Hz (40000000). Raise only with a scope on SCK. |
| `RLCD_USE_LIGHT_SLEEP` | DFS plus automatic light sleep. When 0 the CPU idles at full clock and the `sdkconfig` power options become inert. |
| `RLCD_NIGHT_SLEEP_ENABLED` | Master switch for the night window. 0 keeps the light-sleep-only behaviour. |
| `RLCD_NIGHT_SLEEP_START_HOUR` / `RLCD_NIGHT_SLEEP_END_HOUR` | The window, in local hours (0..23). A start later than the end wraps midnight. |
| `RLCD_NIGHT_SLEEP_MAX_SLEEP_SECONDS` | Length of one sleep chunk (3600). |
| `RLCD_LOG_LEVEL` | Runtime log level; `ESP_LOG_WARN` keeps the per-row chatter out of the UART. |
| `REDUCED_WIFI_TX_POWER_QUARTER_DBM` | WiFi TX power limit in quarter-dBm (40 = 10 dBm). |

## Notes

- Times are stored in GMT and rendered in the timezone advertised by the API's ISO timestamps,
  so the on-screen clock lines up with the departure times.
- HTTP responses are read over plain HTTP with certificate common-name checking skipped, which
  is acceptable for this public read-only endpoint on a memory-constrained target.
