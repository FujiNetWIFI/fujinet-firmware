
// https://www.pagetable.com/?p=1135
// https://codebase64.org/doku.php?id=base:how_the_vic_64_serial_bus_works
// http://www.zimmers.net/anonftp/pub/cbm/programming/serial-bus.pdf
// https://github.com/0cjs/sedoc/blob/master/8bit/cbm/serial-bus.md


#ifndef PROTOCOL_CBMSTANDARDSERIAL_H
#define PROTOCOL_CBMSTANDARDSERIAL_H

//#include "../../../include/global_defines.h"

#include "fnSystem.h"

//#define set_pin_mode      fnSystem.set_pin_mode
#define digital_write     fnSystem.digital_write
#define digital_read      fnSystem.digital_read
#define INPUT             gpio_mode_t::GPIO_MODE_INPUT
#define OUTPUT            gpio_mode_t::GPIO_MODE_OUTPUT
#define delayMicroseconds fnSystem.delay_microseconds

// BIT Flags
#define CLEAR           0x00      // clear all flags
#define ATN_PULLED      (1 << 0)  // might be set by iec_receive
#define EOI_RECVD       (1 << 1)
#define COMMAND_RECVD   (1 << 2)
#define JIFFY_ACTIVE    (1 << 3)
#define JIFFY_LOAD      (1 << 4)
#define DOLPHIN_ACTIVE  (1 << 5)
#define ERROR           (1 << 7)  // if this flag is set, something went wrong

// IEC protocol timing consts in microseconds (us)
// IEC-Disected p10-11         // Description              // min    typical    max      // Notes
#define TIMEOUT_Tat    1000    // ATN RESPONSE (REQUIRED)     -      -          1000us      (If maximum time exceeded, device not present error.)
#define TIMING_Th      0       // LISTENER HOLD-OFF           0      -          infinte
#define TIMING_Tne     40      // NON-EOI RESPONSE TO RFD     -      40us       200us       (If maximum time exceeded, EOI response required.)
#define TIMEOUT_Tne    200
#define TIMING_Ts      70      // BIT SET-UP TALKER           20us   70us       -           
#define TIMING_Tv      60      // DATA VALID                  20us   20us       -           (Tv and Tpr minimum must be 60μ s for external device to be a talker. )
#define TIMING_Tf      20      // FRAME HANDSHAKE             0      20us       1000us      (If maximum time exceeded, frame error.)
#define TIMEOUT_Tf     1000
#define TIMING_Tr      20      // FRAME TO RELEASE OF ATN     20us   -          -
#define TIMING_Tbb     100     // BETWEEN BYTES TIME          100us  -          -
#define TIMING_Tye     250     // EOI RESPONSE TIME           200us  250us      -
#define TIMING_Tei     80      // EOI RESPONSE HOLD TIME      60us   -          -           (Tei minimum must be 80μ s for external device to be a listener.)
#define TIMING_Try     30      // TALKER RESPONSE LIMIT       0      30us       60us
#define TIMEOUT_Try    60
#define TIMING_Tpr     60      // BYTE-ACKNOWLEDGE            20us   30us       -           (Tv and Tpr minimum must be 60μ s for external device to be a talker.)
#define TIMING_Ttk     30      // TALK-ATTENTION RELEASE      20us   30us       100us
#define TIMEOUT_Ttk    100
#define TIMING_Tdc     0       // TALK-ATTENTION ACKNOWLEDGE  0      -          -
#define TIMING_Tda     80      // TALK-ATTENTION ACK. HOLD    80us   -          -
#define TIMING_Tfr     60      // EOI ACKNOWLEDGE             60us   -          -

// See timeoutWait
#define TIMEOUT 1000 // 1ms
#define TIMED_OUT -1
#define FOREVER 0

#ifndef IEC_INVERTED_LINES
// Not Inverted
#define PULLED    true
#define RELEASED  false
#define LOW 0x00
#define HIGH 0x01
#else
// Inverted
#define PULLED    false
#define RELEASED  true
#define LOW 0x01
#define HIGH 0x00
#endif

namespace Protocol
{
    class CBMStandardSerial
    {
        public:
            // communication must be reset
            uint8_t flags = CLEAR;

            virtual int16_t receiveByte ( uint8_t device );
            virtual bool sendByte ( uint8_t data, bool signalEOI );
            int16_t timeoutWait ( uint8_t pin, bool target_status, size_t wait = TIMEOUT );
            bool wait ( size_t wait );


            // true => PULL => LOW
            inline void IRAM_ATTR pull ( uint8_t pin )
            {
#ifndef IEC_SPLIT_LINES
                set_pin_mode ( pin, OUTPUT );
#endif
                digital_write ( pin, LOW );
            }

            // false => RELEASE => HIGH
            inline void IRAM_ATTR release ( uint8_t pin )
            {
#ifndef IEC_SPLIT_LINES
                set_pin_mode ( pin, OUTPUT );
#endif
                digital_write ( pin, HIGH );
            }

            inline bool IRAM_ATTR status ( uint8_t pin )
            {
#ifndef IEC_SPLIT_LINES
                set_pin_mode ( pin, INPUT );
#endif
                return gpio_get_level ( ( gpio_num_t ) pin ) ? RELEASED : PULLED;
            }

            inline void IRAM_ATTR set_pin_mode ( uint8_t pin, gpio_mode_t mode )
            {
                static uint64_t gpio_pin_modes;
                uint8_t b_mode = ( mode == 1 ) ? 1 : 0;

                // is this pin mode already set the way we want?
                if ( ( ( gpio_pin_modes >> pin ) & 1ULL ) != b_mode )
                {
                    // toggle bit so we don't change mode unnecessarily
                    gpio_pin_modes ^= ( -b_mode ^ gpio_pin_modes ) & ( 1ULL << pin );

                    gpio_config_t io_conf =
                    {
                        .pin_bit_mask = ( 1ULL << pin ),            // bit mask of the pins that you want to set
                        .mode = mode,                               // set as input mode
                        .pull_up_en = GPIO_PULLUP_DISABLE,          // disable pull-up mode
                        .pull_down_en = GPIO_PULLDOWN_DISABLE,      // disable pull-down mode
                        .intr_type = GPIO_INTR_DISABLE              // interrupt of falling edge
                    };
                    //configure GPIO with the given settings
                    gpio_config ( &io_conf );
                }
            }

    };
};

#endif
