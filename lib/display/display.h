
#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <stdio.h>
#include <string.h>
#include <vector>

#include "../../include/pinmap.h"
#include "../../include/debug.h"
typedef struct {
    union {
        struct {
            union {
                uint8_t r;
                uint8_t red;
            };

            union {
                uint8_t g;
                uint8_t green;
            };

            union {
                uint8_t b;
                uint8_t blue;
            };
        };

        uint8_t raw[3];
        uint32_t num;
    };
} CRGB;

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t spi;
    int dma_chan;
    spi_device_interface_config_t devcfg;
    spi_bus_config_t buscfg;
} spi_settings_t;

typedef enum {
    WS2812B = 0,
    WS2815,
} led_strip_model_t;



//static QueueHandle_t display_evt_queue = NULL;
    
class Display
{

    enum Mode {
        MODE_STATUS  = -1,
        MODE_IDLE    = 0,
        MODE_SEND    = 1,
        MODE_RECEIVE = 2,
        MODE_CUSTOM  = 3
    };

private:
    //BaseType_t m_task_handle;
    uint8_t m_statusCode = 0;
    esp_err_t init(int pin, led_strip_model_t model, int num_of_leds);

    // Array of segements that contain index and length
    std::vector<std::pair<uint8_t, uint8_t>> segments;

    // Use properties to change state and let the task call these functions
    void show_progress();
    void show_activity();
    void blink();
    void rotate();
    void fill_all(CRGB color);

    void meatloaf();

public:
    Mode mode = MODE_IDLE;
    uint16_t speed = 300;
    uint8_t progress = 100;
    bool activity = false;
    uint8_t direction = 1;  // 0 = left (RECEIVE), 1 = right (SEND)
    uint8_t brightness = 8;
    uint8_t segment = 0;    // Segment 0 is the first 5 LEDs

    Display() {
        add_segment(0, 5);  // Add default segment
    }

    void start(void);
    void service();
    esp_err_t update();

    void idle(void) { mode = MODE_IDLE;  meatloaf(); activity = false; progress = 100; speed = 1000; direction = 0; };
    void send(void) { mode = MODE_SEND; direction = 0; };
    void receive(void) { mode = MODE_RECEIVE; direction = 1; };
    void status(uint8_t code) { 
        mode = MODE_STATUS;
        m_statusCode = code;
    };

    // Returns segment index
    uint8_t add_segment(uint8_t index, uint8_t length) { 
        segments.push_back(std::make_pair(index, length)); 
        return segments.size() - 1;
    }
    void clear_segments() { segments.clear(); }

    void set_pixel(uint16_t index, CRGB color);
    void set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
    void set_pixels(uint16_t index, CRGB *colors, uint16_t count);

};

extern Display DISPLAY;
#endif // DISPLAY_H