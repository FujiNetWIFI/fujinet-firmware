#include "soft9bitUART.h"

//#include "led.h"
//#include "../include/debug.h"

//#include "cstring"

// copied from fuUART.cpp - figure out better way
// #define UART2_RX 33
// #define ESP_INTR_FLAG_DEFAULT 0

// unsigned long last = 0;
// unsigned long delta = 0;


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

#define DEADFACTOR 4


uint8_t soft9UART::available()
{
    return index_in - index_out;
}

void soft9UART::set_baud(uint16_t b)
{
    baud = b;
    period = 1000000 / baud;
};

uint16_t soft9UART::read()
{
    return buffer[index_out++];
}

int8_t soft9UART::service(uint8_t b)
{
    unsigned long t = fnSystem.micros();
    if (state_counter == STARTBIT)
    {
        if (b == 0)
        { // found start bit - sync up clock
            state_counter++;
            received_byte = 0; // clear data
            baud_clock = t;    // approx beginning of start bit
// #ifdef DEBUG
//             Debug_println("Start bit received!");
// #endif
        }
    }
    else if (t > baud_clock + period * state_counter + period / DEADFACTOR)
    {
        if (t < baud_clock + period * state_counter + STOPBIT * period / DEADFACTOR)
        {
            if (state_counter == STOPBIT)
            {
                buffer[index_in++] = received_byte;
                state_counter = STARTBIT;
// #ifdef DEBUG
//                 Debug_printf("received %02X\n", received_byte);
// #endif
                if (b != 1)
                {
#ifdef DEBUG
                    Debug_println("Stop bit invalid!");
#endif
                    return -1; // frame sync error
                }
            }
            else
            {
                uint8_t bb = (b == 0) ? 0 : 1;
                received_byte |= (bb << (state_counter - 1));
                state_counter++;
// #ifdef DEBUG
//                 // Debug_printf("bit %u ", state_counter - 1);
//                 Debug_printf("%u ", b);
// #endif
            }
        }
        else
        {
#ifdef DEBUG
            Debug_println("Bit slip error!");
#endif
            state_counter = STARTBIT;
            return -1; // frame sync error
        }
    }
    return 0;
}
