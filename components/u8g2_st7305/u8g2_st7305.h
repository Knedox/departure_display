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
    /* Packed frame in panel order for the single 0x2C push; see DRAW_TILE. */
    uint8_t *frame_buf;
    size_t frame_buf_size;
    /* Tiles packed since the last flush; the 50th triggers the transfer. */
    uint8_t tile_rows_seen;
} u8g2_st7305_t;

u8g2_st7305_config_t u8g2_st7305_default_config(void);
esp_err_t u8g2_st7305_init(u8g2_st7305_t *dev, const u8g2_st7305_config_t *config);
void u8g2_st7305_deinit(u8g2_st7305_t *dev);

/* Bytes in one packed frame: 300x400 px at 8 px/byte. */
#define ST7305_FULL_FRAME_BYTES 15000

static inline u8g2_t *u8g2_st7305_get_u8g2(u8g2_st7305_t *dev)
{
    return dev == NULL ? NULL : &dev->u8g2;
}

#ifdef __cplusplus
}
#endif
