// http://sta.c64.org/cbmpar.html
// https://ist.uwaterloo.ca/~schepers/c64par.html
// https://ist.uwaterloo.ca/~schepers/1541port.html
//

#ifndef MEATLOAF_BUS_PARALLEL
#define MEATLOAF_BUS_PARALLEL
#ifdef PARALLEL_BUS

#include "../../gpiox/gpiox.h"

#include <stdint.h>

// C64, 128, VIC20
// User Port to pin mapping
// #define FLAG2  P07  // B
// #define CNT1   P06  // 4
// #define SP1    P05  // 5
// #define CNT2   P04  // 6
// #define SP2    P03  // 7
// #define PC2    P02  // 8
// #define ATN    P01  // 9
// #define PA2    P00  // M

// #define PB0    P10   // C
// #define PB1    P11   // D
// #define PB2    P12   // E
// #define PB3    P13   // F
// #define PB4    P14   // H - G
// #define PB5    P15   // J - H
// #define PB6    P16   // K - I
// #define PB7    P17   // L - J

#define USERPORT_FLAGS GPIOX_PORT0
#define USERPORT_DATA  GPIOX_PORT1

// C64, 128, VIC20
typedef enum {
    FLAG2 = P07,  // B
    CNT1  = P06,  // 4
    SP1   = P05,  // 5
    CNT2  = P04,  // 6
    SP2   = P03,  // 7
    PC2   = P02,  // 8
    ATN   = P01,  // 9
    PA2   = P00,  // M - K

    PB0   = P10,  // C
    PB1   = P11,  // D
    PB2   = P12,  // E
    PB3   = P13,  // F
    PB4   = P14,  // H - G
    PB5   = P15,  // J - H
    PB6   = P16,  // K - I
    PB7   = P17,  // L - J
} user_port_pin_t;


// // Plus4
// typedef enum {
//     PB0   = P07,  // B      // P0   DATA 0
//     CNT1  = P06,  // 4      // P2   DATA 2 / Cassette Sense
//     SP1   = P05,  // 5      // P3   DATA 3
//     CNT2  = P04,  // 6      // P4   DATA 4
//     SP2   = P03,  // 7      // P5   DATA 5
//     PC2   = P02,  // 8      // 
//     ATN   = P01,  // 9
//     PA2   = P00,  // M - K

//     PB0   = P10,  // C
//     PB1   = P11,  // D
//     PB2   = P12,  // E
//     PB3   = P13,  // F
//     PB4   = P14,  // H - G
//     PB5   = P15,  // J - H
//     PB6   = P16,  // K - I
//     PB7   = P17,  // L - J
// } user_port_plus4_pin_t;

// Return values for service:
typedef enum
{
    PBUS_OFFLINE = -3, // Bus is empty
    PBUS_RESET = -2,   // The bus is in a reset state (RESET line).    
    PBUS_ERROR = -1,   // A problem occoured, reset communication
    PBUS_IDLE = 0,     // Nothing recieved of our concern
    PBUS_ACTIVE = 1,   // ATN is pulled and a command byte is expected
    PBUS_PROCESS = 2,  // A command is ready to be processed
} pbus_state_t;

typedef enum {
    MODE_SEND = 0,
    MODE_RECEIVE = 1
} parallel_mode_t;

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
    (byte & 0x80 ? '1' : '0'), \
    (byte & 0x40 ? '1' : '0'), \
    (byte & 0x20 ? '1' : '0'), \
    (byte & 0x10 ? '1' : '0'), \
    (byte & 0x08 ? '1' : '0'), \
    (byte & 0x04 ? '1' : '0'), \
    (byte & 0x02 ? '1' : '0'), \
    (byte & 0x01 ? '1' : '0')

class parallelBus
{
    public:
        void setup();
        void reset();
        void service();

        void setMode(parallel_mode_t mode);
        void handShake();
        uint8_t readByte();
        void writeByte( uint8_t byte );
        bool status( user_port_pin_t pin );

        uint8_t flags = 0x00;
        uint8_t data = 0;
        parallel_mode_t mode = MODE_RECEIVE;
        pbus_state_t state;
        bool enabled = true;
};

extern parallelBus PARALLEL;

#endif // PARALLEL_BUS
#endif // MEATLOAF_BUS_PARALLEL