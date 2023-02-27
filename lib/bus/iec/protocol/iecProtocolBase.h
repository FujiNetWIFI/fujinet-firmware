#ifndef IECPROTOCOLBASE_H
#define IECPROTOCOLBASE_H

#include <cstdint>
#include <cstddef>
#include "../../include/iecdefines.h"

/**
 * @brief The IEC bus protocol base class
 */
class IecProtocolBase
{
    public:
    
    /**
     * @brief receive byte from bus
     * @return The byte received from bus.
    */
    virtual int16_t receiveByte() = 0;

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
     * @param wait timeout period in microseconds (default is 1 millisecond)
     * @param watch_atn also abort if ATN is asserted? (default is true)
     * @return elapsed time in microseconds, or -1 if ATN pulled, or -1 if timeout breached.
     * 
     */
    virtual int16_t timeoutWait(uint8_t pin, bool target_status, size_t wait = TIMEOUT_DEFAULT, bool watch_atn = true);

    /**
     * @brief Wait for specified milliseconds, or until ATN status changes
     * @param wait # of milliseconds to wait
     * @param start The previously set start millisecond time.
     */
    virtual bool wait(size_t wait, uint64_t start = 0);
};

#endif /* IECPROTOCOLBASE_H */