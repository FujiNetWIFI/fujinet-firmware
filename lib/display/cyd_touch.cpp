#ifdef FUJINET_CYD

#include "cyd_touch.h"
#include "cyd_lcd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "../../include/pinmap.h"
#include "../../include/debug.h"

#define FT6336_ADDR        0x38
#define FT6336_REG_MODE    0x00
#define FT6336_REG_STATUS  0x02 // touch count, then P1 XH, XL, YH, YL
#define FT6336_REG_THRESH  0x80
#define FT6336_REG_RATE    0x88
#define FT6336_REG_CHIPID  0xA3

// Same settings as ESPHome's ft63x6 driver, which works on this board
#define FT6336_THRESHOLD   22
#define FT6336_ACTIVE_RATE 14
#define I2C_TIMEOUT_MS     20

// The panel reports portrait coordinates (240x320); these map them onto
// the landscape LCD (MADCTL 0x28) of the ES3C28P.
#ifndef CYD_TOUCH_SWAP_XY
#define CYD_TOUCH_SWAP_XY 1
#endif
#ifndef CYD_TOUCH_INVERT_X
#define CYD_TOUCH_INVERT_X 0
#endif
#ifndef CYD_TOUCH_INVERT_Y
#define CYD_TOUCH_INVERT_Y 1
#endif

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;
static bool s_ready = false;

// Set by the INT line; the chip is only read after an interrupt or while a
// touch is in progress, like ESPHome's driver, instead of polled constantly
static volatile bool s_irq_pending = false;
static bool s_touching = false;

static void IRAM_ATTR touch_isr(void *arg)
{
    s_irq_pending = true;
}

static bool setup_interrupt()
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_TOUCH_INT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&io) != ESP_OK)
        return false;

    // FujiNet normally has the ISR service installed already
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
        return false;
    return gpio_isr_handler_add(PIN_TOUCH_INT, touch_isr, nullptr) == ESP_OK;
}

static bool create_device()
{
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = -1; // any free port
    bus.sda_io_num = PIN_TOUCH_SDA;
    bus.scl_io_num = PIN_TOUCH_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &s_bus) != ESP_OK)
    {
        Debug_println("CYD touch: I2C bus init failed");
        return false;
    }

    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = FT6336_ADDR;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(s_bus, &dev, &s_dev) != ESP_OK)
    {
        Debug_println("CYD touch: add device failed");
        return false;
    }
    return true;
}

static bool read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, I2C_TIMEOUT_MS) == ESP_OK;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS) == ESP_OK;
}

static void reset_controller()
{
    gpio_reset_pin(PIN_TOUCH_RST);
    gpio_set_direction(PIN_TOUCH_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300)); // controller boot time
}

// Safe to call again after a failure; it resets the controller and re-probes
bool cyd_touch_init()
{
    if (s_ready)
        return true;
    if (s_dev == nullptr)
    {
        if (!create_device())
            return false;
        if (!setup_interrupt())
        {
            Debug_println("CYD touch: INT setup failed");
            return false;
        }
    }

    reset_controller();

    uint8_t chip_id, old_thresh;
    if (!read_reg(FT6336_REG_CHIPID, &chip_id) || !read_reg(FT6336_REG_THRESH, &old_thresh))
    {
        Debug_println("CYD touch: FT6336 not responding");
        return false;
    }

    // Working mode, touch threshold and active scan rate as ESPHome sets them
    if (!write_reg(FT6336_REG_MODE, 0x00) || !write_reg(FT6336_REG_THRESH, FT6336_THRESHOLD)
        || !write_reg(FT6336_REG_RATE, FT6336_ACTIVE_RATE))
    {
        Debug_println("CYD touch: FT6336 config write failed");
        return false;
    }

    s_ready = true;
    Debug_printf("CYD touch: FT6336 ready (chip id 0x%02x, threshold %u -> %u)\r\n",
                 chip_id, old_thresh, FT6336_THRESHOLD);
    return true;
}

bool cyd_touch_read(int *x, int *y)
{
    uint8_t reg = FT6336_REG_STATUS, buf[5];
    if (!s_ready || !(s_irq_pending || s_touching))
        return false;
    s_irq_pending = false;

    if (i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), I2C_TIMEOUT_MS) != ESP_OK)
    {
        s_touching = false;
        return false;
    }

    int touches = buf[0] & 0x0F;
    s_touching = touches > 0 && touches <= 2;
    if (!s_touching)
        return false;

    int raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
    int raw_y = ((buf[3] & 0x0F) << 8) | buf[4];

    int tx = CYD_TOUCH_SWAP_XY ? raw_y : raw_x;
    int ty = CYD_TOUCH_SWAP_XY ? raw_x : raw_y;
    if (CYD_TOUCH_INVERT_X)
        tx = CYD_LCD_WIDTH - 1 - tx;
    if (CYD_TOUCH_INVERT_Y)
        ty = CYD_LCD_HEIGHT - 1 - ty;

    *x = tx < 0 ? 0 : (tx >= CYD_LCD_WIDTH ? CYD_LCD_WIDTH - 1 : tx);
    *y = ty < 0 ? 0 : (ty >= CYD_LCD_HEIGHT ? CYD_LCD_HEIGHT - 1 : ty);
    return true;
}

#endif // FUJINET_CYD
