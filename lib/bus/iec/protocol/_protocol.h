#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include <esp_timer.h>

#include "../../include/cbm_defines.h"

#ifndef IEC_ASSERT_RELEASE_AS_FUNCTIONS
#define IEC_RELEASE(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.enable1_w1tc.val = _mask;		\
      else					\
	GPIO.enable_w1tc = _mask;		\
    })
#define IEC_ASSERT(pin) ({			\
      uint32_t _pin = pin;			\
      uint32_t _mask = 1 << (_pin % 32);	\
      if (_pin >= 32)				\
	GPIO.enable1_w1ts.val = _mask;		\
      else					\
	GPIO.enable_w1ts = _mask;		\
    })

#ifndef IEC_INVERTED_LINES
#define IEC_IS_ASSERTED(pin) ({						\
      uint32_t _pin = pin;						\
      !((_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32)));	\
    })
#else
#define IEC_IS_ASSERTED(pin) ({						\
      uint32_t _pin = pin;						\
      !!(_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32));	\
    })
#endif /* !IEC_INVERTED_LINES */

#else
#define IEC_IS_ASSERTED(x)  status(x)
#define IEC_ASSERT(x)       pull(x)
#define IEC_RELEASE(x)      release(x)
#endif

namespace Protocol
{
    /**
     * @brief The IEC bus protocol base class
     */
    class IECProtocol
    {
        public:

        // 2bit Fast Loader Pair Timing
        std::vector<std::vector<uint8_t>> bit_pair_timing = {
            {0, 0, 0, 0},    // Receive
            {0, 0, 0, 0}     // Send
        };

        bool timer_timedout = false;
        uint64_t timer_started = 0;
        uint64_t timer_elapsed = 0;



        /**
         * ESP timer handle for the Interrupt rate limiting timer
         */
        esp_timer_handle_t timer_handle = nullptr;

        /**
         * @brief ctor
         */
        IECProtocol();

        /**
         * @brief dtor
         */
        ~IECProtocol();

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

        /*
         * @brief Start timer
        */
        void timer_start(uint64_t timeout);
        void timer_stop();

#ifdef COMPLEX_WAIT
        /**
         * @brief Wait until target status, or timeout is reached.
         * @param pin IEC pin to watch
         * @param target_status break if target state reached
         * @param wait_us timeout period in microseconds (default is 1 millisecond)
         * @param watch_atn also abort if ATN status changes (default is true)
         * @return elapsed time in microseconds, or -1 if ATN pulled, or -1 if timeout breached.
         * 
         */
        virtual int16_t timeoutWait(uint8_t pin, bool target_status, size_t wait_us = TIMEOUT_DEFAULT, bool watch_atn = true);

        /**
         * @brief Wait for specified milliseconds, or until ATN status changes
         * @param wait_us # of milliseconds to wait
         * @param start The previously set start millisecond time.
         * @param watch_atn also abort if ATN status changes? (default is false)
         */
        virtual bool wait(size_t wait_us, bool watch_atn = false);
        virtual bool wait(size_t wait_us, uint64_t start, bool watch_atn = false);
#else // !COMPLEX_WAIT
      int waitForSignals(int pin1, int state1, int pin2, int state2, int delay);
#endif // COMPLEX_WAIT
    };
};

#endif /* IECPROTOCOLBASE_H */
