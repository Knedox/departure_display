#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define U8G2_ST7305_TILE_BUF_FULL 0

typedef struct {
    gpio_num_t mosi_io;
    gpio_num_t sclk_io;
    gpio_num_t dc_io;
    gpio_num_t cs_io;
    gpio_num_t reset_io;
    spi_host_device_t spi_host;
    /* Required, must be > 0; u8g2_st7305_init() rejects 0. The default config
     * leaves it unset, so the caller must assign it (the project uses
     * RLCD_SPI_CLOCK_HZ from main/user_config.h). */
    int clock_hz;
    uint8_t tile_buf_height;
    const u8g2_cb_t *rotation;
    bool prefer_psram;
} u8g2_st7305_config_t;

typedef struct {
    u8g2_t u8g2;
    spi_device_handle_t spi;
    spi_host_device_t spi_host;
    int clock_hz;
    gpio_num_t dc_io;
    gpio_num_t cs_io;
    gpio_num_t reset_io;
    uint8_t *buffer;
    size_t buffer_size;
    uint8_t tile_buf_height;
    bool owns_spi_bus;
} u8g2_st7305_t;

u8g2_st7305_config_t u8g2_st7305_default_config(void);
esp_err_t u8g2_st7305_init(u8g2_st7305_t *dev, const u8g2_st7305_config_t *config);
void u8g2_st7305_deinit(u8g2_st7305_t *dev);

/* Panel refresh-rate mode.
 *
 * The ST7305 has two self-refresh modes that differ only in how often the
 * panel re-drives its pixels:
 *   HPM (0x38) - high power mode,  32 Hz self-refresh. Required for writing.
 *   LPM (0x39) - low power mode,    1 Hz self-refresh. Image is retained.
 *
 * Frame memory is preserved across the switch, so a static image can be left
 * displayed in LPM at roughly 1/32 of the refresh energy. The datasheet also
 * calls for changing the source-voltage group when switching, but the init
 * sequence already programs the voltages this panel needs, so only the mode
 * command is sent.
 *
 * Writing while in LPM can delay the visible update by up to one refresh
 * period (~1 s), so callers should switch back to HPM before drawing.
 *
 * These are NOT wrapped by u8g2; the equivalent is sending command bytes
 * directly, which is what these helpers do. */
esp_err_t u8g2_st7305_set_low_power_mode(u8g2_st7305_t *dev);
esp_err_t u8g2_st7305_set_high_power_mode(u8g2_st7305_t *dev);

static inline u8g2_t *u8g2_st7305_get_u8g2(u8g2_st7305_t *dev)
{
    return dev == NULL ? NULL : &dev->u8g2;
}

#ifdef __cplusplus
}
#endif
