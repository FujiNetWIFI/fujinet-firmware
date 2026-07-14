
#include <soc/gpio_num.h>

#ifndef PINMAP_DEFAULTS_H
#define PINMAP_DEFAULTS_H

// Defaults
#define PIN_DEBUG		PIN_IEC_SRQ

#ifndef LEDS_INVERTED
#define LEDS_INVERTED           1
#endif

// LED Strip
#ifndef PIN_LED_RGB
#define PIN_LED_RGB             GPIO_NUM_NC // No RGB LED
#endif
#define RGB_LED_BRIGHTNESS      15 // max mA the LED can use determines brightness
#define RGB_LED_COUNT           5
#define RGB_LED_TYPE            WS2812B
#define RGB_LED_ORDER           GRB
#define PIN_LED_RGB_PWR         GPIO_NUM_NC


#endif // PINMAP_DEFAULTS_H