#ifdef ENABLE_DISPLAY

#include "display.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>
#include "freertos/task.h"

#include "../../include/pinmap.h"
#include "../../include/debug.h"

//#include "WS2812FX/WS2812FX.h"

#define ST_OK                  0
#define ST_SCRATCHED           1
#define ST_WRITE_ERROR        25
#define ST_WRITE_PROTECT      26
#define ST_SYNTAX_ERROR_31    31
#define ST_SYNTAX_ERROR_33    33
#define ST_FILE_NOT_OPEN      61
#define ST_FILE_NOT_FOUND     62
#define ST_FILE_EXISTS        63
#define ST_FILE_TYPE_MISMATCH 64
#define ST_NO_CHANNEL         70
#define ST_SPLASH             73
#define ST_DRIVE_NOT_READY    74

Display DISPLAY;

uint16_t *dma_buffer;
CRGB *ws28xx_pixels;
static int n_of_leds, reset_delay, dma_buf_size;
led_strip_model_t led_model;

static spi_settings_t spi_settings = {
    .host = SPI3_HOST,
    .dma_chan = SPI_DMA_CH_AUTO,
    .devcfg =
        {
            .command_bits = 0,
            .address_bits = 0,
            .mode = 0,                           // SPI mode 0
            .clock_speed_hz = (int)(3.2 * 1000 * 1000), // Clock out at 3.2 MHz
            .spics_io_num = -1,                  // CS pin
            .flags = SPI_DEVICE_TXBIT_LSBFIRST,
            .queue_size = 1,
        },
    .buscfg =
        {
            .miso_io_num = -1,
            .sclk_io_num = -1,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        },
};

static const uint16_t timing_bits[16] = {
    0x1111, 0x7111, 0x1711, 0x7711, 0x1171, 0x7171, 0x1771, 0x7771,
    0x1117, 0x7117, 0x1717, 0x7717, 0x1177, 0x7177, 0x1777, 0x7777};


static void display_task(void *args)
{
    Display *d = (Display *)args;
    while (1) {
        d->service();
        vTaskDelay(d->speed / portTICK_PERIOD_MS);
    }
}

void Display::service()
{
    switch(mode)
    {
        case MODE_IDLE:
        case MODE_SEND:
        case MODE_RECEIVE:
            // MODE_IDLE
            rotate();
            break;
        case MODE_STATUS:
            activity = false;
            switch( m_statusCode )
            {
                case ST_OK             :
                case ST_SCRATCHED      :
                case ST_SPLASH         :
                    mode = MODE_IDLE;
                    meatloaf();
                    break;
                default                :
                    blink();
                    break;
            }
            break;
        case MODE_CUSTOM:
            mode = MODE_IDLE;
            break;
    }
    if ( progress < 100 ) {
        show_progress();
    }
    else if ( activity ) {
        show_activity();
    }
    update();
}

esp_err_t Display::init(int pin, led_strip_model_t model, int num_of_leds)
{
    esp_err_t err = ESP_OK;
    n_of_leds = num_of_leds;
    led_model = model;
    // Increase if something breaks. values are less than recommended in
    // datasheets but seem stable
    reset_delay = (model == WS2812B) ? 3 : 30;
    // 12 bytes for each led + bytes for initial zero and reset state
    dma_buf_size = n_of_leds * 12 + (reset_delay + 1) * 2;
    ws28xx_pixels = (CRGB*)malloc(sizeof(CRGB) * n_of_leds);
    if (ws28xx_pixels == NULL) {
        return ESP_ERR_NO_MEM;
    }

    spi_settings.buscfg.mosi_io_num = pin;
    spi_settings.buscfg.max_transfer_sz = dma_buf_size;
    err = spi_bus_initialize(spi_settings.host, &spi_settings.buscfg,
                             spi_settings.dma_chan);
    if (err != ESP_OK) {
        free(ws28xx_pixels);
        return err;
    }
    err = spi_bus_add_device(spi_settings.host, &spi_settings.devcfg,
                             &spi_settings.spi);
    if (err != ESP_OK) {
        free(ws28xx_pixels);
        return err;
    }
    // Critical to be DMA memory.
    dma_buffer = (uint16_t*)heap_caps_malloc(dma_buf_size, MALLOC_CAP_DMA);
    if (dma_buffer == NULL) {
        free(ws28xx_pixels);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Display::set_pixel(uint16_t index, CRGB color) { ws28xx_pixels[index] = color; };
void Display::set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) { ws28xx_pixels[index] = (CRGB){.r=r, .g=g, .b=b}; };

void Display::fill_all(CRGB color) 
{
    for (int i = 0; i < n_of_leds; i++) {
        ws28xx_pixels[i] = color;
    }
}

void Display::show_activity()
{
    static uint8_t curr_led = 3;

    // Set all leds to black
    fill_all((CRGB){.r=0, .g=0, .b=0});


    // // Number of leds to light up
    ws28xx_pixels[curr_led] = (CRGB){.r=0, .g=100, .b=0};

    if ( direction)
        curr_led++;
    else
        curr_led--;

    if ( (curr_led == 0) )
        direction = 1;

    if (curr_led == (n_of_leds - 1))
        direction = 0;
}

void Display::show_progress()
{
    static bool led_state_off = false;

    speed = 25;
    activity = false;

    // Set all leds to black
    fill_all((CRGB){.r=0, .g=0, .b=0});

    // Number of leds to light up
    int n = (n_of_leds * progress) / 100;
    for (int i = 0; i < n; i++) {
        // rotate all elements of the array
        ws28xx_pixels[i] = (CRGB){.r=0, .g=100, .b=0};
    }

    led_state_off = !led_state_off;
    if (led_state_off) 
    {
        ws28xx_pixels[n] = (CRGB){.r=200, .g=200, .b=200};
    }
    else
    {
        ws28xx_pixels[n] = (CRGB){.r=0, .g=200, .b=0};
    }
    //if (n != 4)
    //    ws28xx_pixels[4] = (CRGB){.r=200, .g=200, .b=200};

    // Set remaining pixels to black
    // for (int i = n + 1; i < n_of_leds; i++) {
    //     ws28xx_pixels[i] = (CRGB){.r=0, .g=0, .b=0};
    // }
}

esp_err_t Display::update() 
{
    esp_err_t err;
    int n = 0;
    memset(dma_buffer, 0, dma_buf_size);
    dma_buffer[n++] = 0;
    for (int i = 0; i < n_of_leds; i++) {
        // Data you want to write to each LEDs
        uint32_t temp = ws28xx_pixels[i].num;
        if (led_model == WS2815) {
            // Red
            dma_buffer[n++] = timing_bits[0x0f & (temp >> 4)];
            dma_buffer[n++] = timing_bits[0x0f & (temp)];

            // Green
            dma_buffer[n++] = timing_bits[0x0f & (temp >> 12)];
            dma_buffer[n++] = timing_bits[0x0f & (temp) >> 8];
        } else {
            // Green
            dma_buffer[n++] = timing_bits[0x0f & (temp >> 12)];
            dma_buffer[n++] = timing_bits[0x0f & (temp) >> 8];

            // Red
            dma_buffer[n++] = timing_bits[0x0f & (temp >> 4)];
            dma_buffer[n++] = timing_bits[0x0f & (temp)];
        }
        // Blue
        dma_buffer[n++] = timing_bits[0x0f & (temp >> 20)];
        dma_buffer[n++] = timing_bits[0x0f & (temp) >> 16];
    }
    for (int i = 0; i < reset_delay; i++) {
        dma_buffer[n++] = 0;
    }
    spi_transaction_t tx_conf = {
        .length = (size_t)(dma_buf_size * 8),
        .tx_buffer = dma_buffer,
    };
    err = spi_device_transmit(spi_settings.spi, &tx_conf);
    return err;
}

void Display::start(void)
{
    init(PIN_LED_RGB, WS2812B, RGB_LED_COUNT);
    idle();

    // Start DISPLAY task
    if ( xTaskCreatePinnedToCore(display_task, "display_task", 1024, this, 4, NULL, 0) != pdTRUE)
    {
        Debug_printv("Could not start DISPLAY task!");
    }
}


void Display::blink(void) 
{
    static bool led_state_off = false;
    speed = 100;

    led_state_off = !led_state_off;
    for(int i = 0; i < RGB_LED_COUNT; i++) {
        if (led_state_off) 
            ws28xx_pixels[i] = (CRGB){.r=0, .g=0, .b=0};
        else 
            ws28xx_pixels[i] = (CRGB){.r=20, .g=0, .b=0};
    }
}

void Display::rotate()
{
    if ( !direction )
    {
        // rotate left
        auto temp = ws28xx_pixels[0];
        memmove(ws28xx_pixels, ws28xx_pixels + 1, sizeof(CRGB) * (n_of_leds - 1));
        ws28xx_pixels[n_of_leds - 1] = temp;
    }
    else
    {
        // rotate right
        auto temp = ws28xx_pixels[n_of_leds - 1];
        memmove(ws28xx_pixels + 1, ws28xx_pixels, sizeof(CRGB) * (n_of_leds - 1));
        ws28xx_pixels[0] = temp;
    }
    memcpy(ws28xx_pixels, ws28xx_pixels, sizeof(CRGB) * n_of_leds);

}

void Display::meatloaf()
{
    // Pastel Meatloaf Pixels
    ws28xx_pixels[0] = (CRGB){.r=87, .g=145, .b=178};   // BLUE
    ws28xx_pixels[1] = (CRGB){.r=128, .g=178, .b=58};   // GREEN
    ws28xx_pixels[2] = (CRGB){.r=178, .g=168, .b=58};   // YELLOW
    ws28xx_pixels[3] = (CRGB){.r=178, .g=102, .b=51};   // ORANGE
    ws28xx_pixels[4] = (CRGB){.r=178, .g=61, .b=53};    // RED

    // // Vibrant Meatloaf Pixels
    // ws28xx_pixels[0] = (CRGB){.r=0, .g=0, .b=255};   // BLUE
    // ws28xx_pixels[1] = (CRGB){.r=0, .g=255, .b=0};   // GREEN
    // ws28xx_pixels[2] = (CRGB){.r=255, .g=255, .b=0};   // YELLOW
    // ws28xx_pixels[3] = (CRGB){.r=255, .g=114, .b=0};   // ORANGE
    // ws28xx_pixels[4] = (CRGB){.r=255, .g=0, .b=0};    // RED

    update();
}

#endif // ENABLE_DISPLAY