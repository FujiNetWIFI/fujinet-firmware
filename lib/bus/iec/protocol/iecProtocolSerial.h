// Standard Commodore IEC (serial IEEE-488) protocol class

// This code uses code from the Meatloaf Project:
// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef IECPROTOCOLSERIAL_H
#define IECPROTOCOLSERIAL_H

#include "iecProtocolBase.h"

class IecProtocolSerial : public IecProtocolBase
{
    private:
    int16_t receiveBits();



    public:
    /**
     * @brief ctor
     */
    IecProtocolSerial();

    /**
     * @brief dtor
     */
    virtual ~IecProtocolSerial();
    
    /**
     * @brief receive byte from bus
     * @return The byte received from bus.
     */
    virtual int16_t receiveByte();
    
    /**
     * @brief send byte to bus
     * @param b Byte to send
     * @param eoi Signal EOI (end of Information)
     * @return true if send was successful.
     */
    virtual bool sendByte(uint8_t data, bool signalEOI);
};

#endif /* IECPROTOCOLSERIAL_H */