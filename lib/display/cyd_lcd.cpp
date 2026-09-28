#ifdef FUJINET_CYD

#include "cyd_lcd.h"
#include "cyd_font6847.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"

#include "../../include/pinmap.h"
#include "../../include/debug.h"

#define LCD_SPI_HOST   SPI2_HOST
#define LCD_PCLK_HZ    (40 * 1000 * 1000)

// MADCTL: MV (0x20) swaps X/Y for landscape, BGR (0x08) matches the panel's
// colour filter. Use 0xE8 instead if the picture is upside down.
#ifndef CYD_LCD_MADCTL
#define CYD_LCD_MADCTL 0x28
#endif
// The ES3C28P's IPS panel shows inverted colours unless INVON is sent
#ifndef CYD_LCD_INVERT
#define CYD_LCD_INVERT 1
#endif

// Largest glyph we render: 4x4 scale
#define GLYPH_BUF_PIXELS (CYD_FONT_W * 4 * CYD_FONT_H * 4)
// Fills go out in strips of this many pixels
#define FILL_BUF_PIXELS  (CYD_LCD_WIDTH * 4)

static esp_lcd_panel_io_handle_t s_io = nullptr;
static SemaphoreHandle_t s_done = nullptr;
static uint16_t *s_glyph_buf = nullptr;
static uint16_t *s_fill_buf = nullptr;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

// Panel expects big-endian RGB565 over SPI
static inline uint16_t swap565(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static void cmd(uint8_t c, const uint8_t *data = nullptr, size_t len = 0)
{
    esp_lcd_panel_io_tx_param(s_io, c, data, len);
}

static void set_window(int x, int y, int w, int h)
{
    int x1 = x + w - 1, y1 = y + h - 1;
    uint8_t col[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t row[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
    cmd(0x2A, col, 4); // CASET
    cmd(0x2B, row, 4); // RASET
}

static void push(const uint16_t *pixels, int count)
{
    esp_lcd_panel_io_tx_color(s_io, 0x2C, pixels, count * 2); // RAMWR
    xSemaphoreTake(s_done, portMAX_DELAY);                     // buffer is reused
}

bool cyd_lcd_init()
{
    s_done = xSemaphoreCreateBinary();
    s_glyph_buf = (uint16_t *)heap_caps_malloc(GLYPH_BUF_PIXELS * 2, MALLOC_CAP_DMA);
    s_fill_buf = (uint16_t *)heap_caps_malloc(FILL_BUF_PIXELS * 2, MALLOC_CAP_DMA);
    if (!s_done || !s_glyph_buf || !s_fill_buf)
    {
        Debug_println("CYD LCD: out of memory");
        return false;
    }

    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_LCD_SCK;
    bus.mosi_io_num = PIN_LCD_MOSI;
    bus.miso_io_num = PIN_LCD_MISO;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = FILL_BUF_PIXELS * 2;
    if (spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK)
    {
        Debug_println("CYD LCD: SPI bus init failed");
        return false;
    }

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = PIN_LCD_CS;
    io.dc_gpio_num = PIN_LCD_DC;
    io.spi_mode = 0;
    io.pclk_hz = LCD_PCLK_HZ;
    io.trans_queue_depth = 4;
    io.on_color_trans_done = on_color_done;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io, &s_io) != ESP_OK)
    {
        Debug_println("CYD LCD: panel IO init failed");
        return false;
    }

    // Panel reset is tied to EN, so use a software reset
    cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t colmod = 0x55; // 16-bit RGB565
    cmd(0x3A, &colmod, 1);
    const uint8_t madctl = CYD_LCD_MADCTL;
    cmd(0x36, &madctl, 1);
    cmd(CYD_LCD_INVERT ? 0x21 : 0x20); // INVON / INVOFF

    cyd_lcd_fill(0, 0, CYD_LCD_WIDTH, CYD_LCD_HEIGHT, 0);
    cmd(0x29); // DISPON
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_reset_pin(PIN_LCD_BL);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, 1);

    Debug_println("CYD LCD: ILI9341 ready");
    return true;
}

void cyd_lcd_fill(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0)
        return;
    uint16_t c = swap565(color);
    int rows_per_strip = FILL_BUF_PIXELS / w;
    int strip_pixels = rows_per_strip * w;
    for (int i = 0; i < strip_pixels; i++)
        s_fill_buf[i] = c;

    while (h > 0)
    {
        int rows = h < rows_per_strip ? h : rows_per_strip;
        set_window(x, y, w, rows);
        push(s_fill_buf, rows * w);
        y += rows;
        h -= rows;
    }
}

// ASCII to MC6847 glyph index
static int glyph_index(char ch)
{
    unsigned char c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z')
        c -= 0x20;
    if (c >= 0x40 && c <= 0x5F)
        return c - 0x40;
    if (c >= 0x20 && c <= 0x3F)
        return c;
    return '?';
}

void cyd_lcd_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int sx, int sy)
{
    if (sx < 1 || sy < 1 || sx > 4 || sy > 4)
        return;
    uint16_t f = swap565(fg), b = swap565(bg);
    int gw = CYD_FONT_W * sx, gh = CYD_FONT_H * sy;

    for (; *text && x + gw <= CYD_LCD_WIDTH; text++, x += gw)
    {
        const uint8_t *rows = &cyd_font6847[glyph_index(*text) * CYD_FONT_H];
        uint16_t *p = s_glyph_buf;
        for (int r = 0; r < CYD_FONT_H; r++)
        {
            uint16_t *line = p;
            for (int col = 0; col < CYD_FONT_W; col++)
            {
                uint16_t px = (rows[r] & (0x80 >> col)) ? f : b;
                for (int k = 0; k < sx; k++)
                    *p++ = px;
            }
            for (int k = 1; k < sy; k++, p += gw)
                memcpy(p, line, gw * 2);
        }
        set_window(x, y, gw, gh);
        push(s_glyph_buf, gw * gh);
    }
}

#endif // FUJINET_CYD
