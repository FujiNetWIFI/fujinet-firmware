#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include <esp_timer.h>

#include "../../include/cbm_defines.h"

#define IEC_RELEASE(pin) ({                     \
      uint32_t _pin = pin;                      \
      uint32_t _mask = 1 << (_pin % 32);        \
      if (_pin >= 32)                           \
        GPIO.enable1_w1tc.val = _mask;          \
      else                                      \
        GPIO.enable_w1tc = _mask;               \
    })
#define IEC_ASSERT(pin) ({                      \
      uint32_t _pin = pin;                      \
      uint32_t _mask = 1 << (_pin % 32);        \
      if (_pin >= 32)                           \
        GPIO.enable1_w1ts.val = _mask;          \
      else                                      \
        GPIO.enable_w1ts = _mask;               \
    })

#ifndef IEC_INVERTED_LINES
#define IEC_IS_ASSERTED(pin) ({                                         \
      uint32_t _pin = pin;                                              \
      !((_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32)));    \
    })
#else
#define IEC_IS_ASSERTED(pin) ({                                         \
      uint32_t _pin = pin;                                              \
      !!(_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32));     \
    })
#endif /* !IEC_INVERTED_LINES */

static uint64_t timer_start_us;
#define timer_start()       timer_start_us = esp_timer_get_time()
#define timer_elapsed()     esp_timer_get_time() - timer_start_us
#define timer_wait(us)      while( (esp_timer_get_time()-timer_start_us) < ((int) (us+0.5)) )
#define timer_timeout(us)   (esp_timer_get_time() - timer_start_us > us) 

namespace Protocol
{
    /**
     * @brief The IEC bus protocol base class
     */
    class IECProtocol
    {
    private:
      uint64_t _transferEnded = 0;

        public:

        // Fast Loader Pair Timing
        std::vector<std::vector<uint8_t>> bit_pair_timing = {
            {0, 0, 0, 0},    // Receive
            {0, 0, 0, 0}     // Send
        };


        /**
         * @brief receive byte from bus
         * @return The byte received from bus.
        */
        virtual uint8_t receiveByte() = 0;

        /**
         * @brief send byte to bus
         * @param b Byte to send
         * @param eoi Signal EOI (end of Information)
         * @return true if send was successful.
        */
        virtual bool sendByte(uint8_t b, bool signalEOI) = 0;

        int waitForSignals(int pin1, int state1, int pin2, int state2, int timeout);
        void transferDelaySinceLast(size_t minimumDelay);
    };
};

#define transferEnd() transferDelaySinceLast(0)

#endif /* IECPROTOCOLBASE_H */
