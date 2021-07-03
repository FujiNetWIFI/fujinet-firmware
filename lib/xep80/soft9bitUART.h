#ifndef SOFT9BIT_H
#define SOFT9BIT_H

#include "fnSystem.h"

//#include <driver/ledc.h>
//#include "sio.h"
//#include "../tcpip/fnUDP.h"

#define XEP_BAUD 15700
#define BLOCK_LEN 128

#define STARTBIT 0
#define STOPBIT 10

// OLD CONOPS FROM CASSETTE
// software uart conops for cassette
// wait for falling edge and set fsk_clock
// find next falling edge and compute period
// check if period different than last (reset denoise counter)
// if not different, increment denoise counter if < denoise threshold
// when denoise counter == denoise threshold, set demod output

// NEW CONOPS FOR XEP SERIAL DATA FROM JOYSTICK PORT
// watch for rising/falling edges
// set state based on input pin value
// can check multiple times and denoise if necessary

// UART decoding
// if state counter == 0, check demod output for start bit edge (low voltage, logic high)
// if start bit edge, record time in baud_clock;
// wait 1/2 period and then read demod output (check it is start bit)
// wait 1 period and get (next) first bit, (shift received byte to right) store it in received_byte;
// increment state counter; go back and wait
// when all 9 bits received wait one more period and check for stop bit
// if not stop bit, throw a frame sync error
// if stop bit, store byte in buffer, reset some stuff

class soft9UART
{
protected:
    uint64_t baud_clock;
    uint16_t baud = XEP_BAUD;             // bps
    uint32_t period = 1000000 / XEP_BAUD; // microseconds

    // uint8_t demod_output;
    // uint8_t denoise_counter;
    // uint8_t denoise_threshold = 3;

    uint16_t received_byte;
    uint16_t state_counter;

    uint16_t buffer[256];
    uint8_t index_in = 0;
    uint8_t index_out = 0;

public:
    uint8_t available();
    void set_baud(uint16_t b);
    uint16_t get_baud() { return baud; };
    uint16_t read();
    void write(uint16_t W);
    void push(uint16_t w);
    int8_t service(uint8_t b);
};

extern soft9UART xepUART;

#endif
