#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include "../../include/cbm_defines.h"

namespace Protocol
{
    /**
     * @brief The IEC bus protocol base class
     */
    class IECProtocol
    {
        public:

        // 2bit Fast Loader Pair Timing
        uint8_t bit_pair_timing[2][4] = {
            {13, 11, 13, 13},    // Receive
            {14, 10, 11, 11}     // Send
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

        /**
         * @brief Wait until target status, or timeout is reached.
         * @param pin IEC pin to watch
         * @param target_status break if target state reached
         * @param wait_us timeout period in microseconds (default is 1 millisecond)
         * @param watch_atn also abort if ATN status changes (default is true)
         * @return elapsed time in microseconds, or -1 if ATN pulled, or -1 if timeout breached.
         * 
         */
        virtual uint64_t timeoutWait(uint8_t pin, bool target_status, size_t wait_us = TIMEOUT_DEFAULT, bool watch_atn = true);

        /**
         * @brief Wait for specified milliseconds, or until ATN status changes
         * @param wait_us # of milliseconds to wait
         * @param start The previously set start millisecond time.
         * @param watch_atn also abort if ATN status changes? (default is false)
         */
        virtual bool wait(size_t wait_us, bool watch_atn = false);
        virtual bool wait(size_t wait_us, uint64_t start, bool watch_atn = false);
    };
};

#endif /* IECPROTOCOLBASE_H */