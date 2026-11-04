#ifdef ENABLE_OLED_DISPLAY

#include "ssd1306.h"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../include/debug.h"

#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

esp_err_t SSD1306::write_cmd(const uint8_t *cmds, size_t len)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, SSD1306_CTRL_CMD, true);
    i2c_master_write(link, cmds, len, true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(_port, link, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(link);
    return err;
}

esp_err_t SSD1306::write_data(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, SSD1306_CTRL_DATA, true);
    i2c_master_write(link, data, len, true);
    i2c_master_stop(link);
    esp_err_t err = i2c_master_cmd_begin(_port, link, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(link);
    return err;
}

esp_err_t SSD1306::init(i2c_port_t port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                         uint8_t i2c_addr, uint32_t clock_hz)
{
    _port = port;
    _addr = i2c_addr;

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda_pin;
    conf.scl_io_num = scl_pin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = clock_hz;

    esp_err_t err = i2c_param_config(_port, &conf);
    if (err != ESP_OK)
    {
        Debug_printf("SSD1306::init() i2c_param_config failed: %d\n", err);
        return err;
    }

    err = i2c_driver_install(_port, conf.mode, 0, 0, 0);
    if (err != ESP_OK)
    {
        Debug_printf("SSD1306::init() i2c_driver_install failed: %d\n", err);
        return err;
    }

    static const uint8_t init_cmds[] = {
        0xAE,       // Display off
        0xD5, 0x80, // Clock divide ratio / osc frequency
        0xA8, 0x3F, // Multiplex ratio = 64
        0xD3, 0x00, // Display offset = 0
        0x40,       // Start line = 0
        0x8D, 0x14, // Charge pump enable
        0x20, 0x02, // Memory addressing mode = page
        0xA1,       // Segment remap
        0xC8,       // COM scan direction, decrementing
        0xDA, 0x12, // COM pins hardware config
        0x81, 0xCF, // Contrast
        0xD9, 0xF1, // Precharge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // Resume to RAM content display
        0xA6,       // Normal (not inverted) display
        0xAF,       // Display on
    };

    err = write_cmd(init_cmds, sizeof(init_cmds));
    if (err != ESP_OK)
    {
        Debug_printf("SSD1306::init() init sequence failed: %d (no ACK from panel at 0x%02X?)\n",
                     err, _addr);
        return err;
    }

    clear();
    return flush();
}

void SSD1306::clear()
{
    memset(_fb, 0, sizeof(_fb));
}

void SSD1306::set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT)
        return;

    uint8_t &byte = _fb[(y / 8) * SSD1306_WIDTH + x];
    uint8_t mask = 1 << (y % 8);
    if (on)
        byte |= mask;
    else
        byte &= ~mask;
}

void SSD1306::draw_bitmap(int x, int y, int w, int h, const uint8_t *bitmap)
{
    int pages = h / 8;
    for (int page = 0; page < pages; page++)
    {
        for (int col = 0; col < w; col++)
        {
            uint8_t byte = bitmap[page * w + col];
            for (int bit = 0; bit < 8; bit++)
                set_pixel(x + col, y + page * 8 + bit, byte & (1 << bit));
        }
    }
}

esp_err_t SSD1306::flush()
{
    for (int page = 0; page < SSD1306_PAGES; page++)
    {
        uint8_t set_page[] = {
            static_cast<uint8_t>(0xB0 | page), // Page address
            0x00,                              // Lower column address = 0
            0x10,                              // Higher column address = 0
        };
        esp_err_t err = write_cmd(set_page, sizeof(set_page));
        if (err != ESP_OK)
            return err;

        err = write_data(&_fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}

esp_err_t SSD1306::set_display_on(bool on)
{
    uint8_t cmd = on ? 0xAF : 0xAE;
    return write_cmd(&cmd, 1);
}

#endif // ENABLE_OLED_DISPLAY
