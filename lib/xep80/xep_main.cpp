#include "xep_main.h"
#include "soft9bitUART.h"
#include "xep80.h"

//#include "led.h"
//#include "debug.h"

// #include "cstring"

/** thinking about state machine
 * boolean states:
 *      file mounted or not
 *      motor activated or not 
 *      (play/record button?)
 * state variables:
 *      baud rate
 *      file position (offset)
 * */

soft9UART xepUART;

// copied from fuUART.cpp - figure out better way
//#define UART2_RX 33

// TODO: reinstate ISR? the XEP ISR should look for the start bit, record the time it was received, and set a flag.

//unsigned long last = 0;
//unsigned long delta = 0;

/* static void IRAM_ATTR cas_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == UART2_RX)
    {
        unsigned long now = fnSystem.micros();
        boxcar[boxidx++] = now - last; // interval between current and last ISR call
        if (boxidx > BOXLEN)
            boxidx = 0; // circular buffer action
        delta = 0; // accumulator for boxcar filter
        for (uint8_t i = 0; i < BOXLEN; i++)
        {
            delta += boxcar[i]; // accumulate internvals for averaging
        }
        delta /= BOXLEN; // normalize accumulator to make mean
        last = now; // remember when this was (maybe move up to right before if statement?)
    }
}
 */

void xep_main::receive_word()
{
    uint8_t input_level = 0;
    // Debug_printf("%d", input_level);

    while (!xepUART.available()) //
    {
        input_level = fnSystem.digital_read(PIN_UART2_RX);
        xepUART.service(input_level & 0x01);
    }
//     uint16_t b = xepUART.read(); //
// #ifdef DEBUG
//     Debug_printf("%03x ", b);
// #endif

//     return b;
}

void xep_main::process_word(uint16_t W)
{
    OutputWord(W); // call the parsing function in xep80.c
}

bool xep_main::service()
{
    uint16_t w;
    if (!xepUART.available())
        receive_word();

    w = xepUART.read();
    Debug_printf("processing command: %03x", w);
    process_word(w);

    return true;
}