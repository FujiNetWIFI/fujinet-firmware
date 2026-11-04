#ifndef SSD1306_H
#define SSD1306_H

#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)
#define SSD1306_FB_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

// Thin driver for a 128x64 I2C SSD1306 panel: transport, init sequence,
// a page-addressed framebuffer, and the GDDRAM flush. No knowledge of
// text or what a splash screen is; that lives in OledDisplay.
class SSD1306
{
public:
    esp_err_t init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                    uint8_t i2c_addr = 0x3C, uint32_t clock_hz = 400000);
    void clear();
    void set_pixel(int x, int y, bool on);
    void draw_bitmap(int x, int y, int w, int h, const uint8_t *bitmap);
    esp_err_t flush();
    esp_err_t set_display_on(bool on);

private:
    esp_err_t write_cmd(const uint8_t *cmds, size_t len);
    esp_err_t write_data(const uint8_t *data, size_t len);

    i2c_port_t _port = I2C_NUM_1;
    uint8_t _addr = 0x3C;
    uint8_t _fb[SSD1306_FB_SIZE] = {0};
};

#endif // SSD1306_H
