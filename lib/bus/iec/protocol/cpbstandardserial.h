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

// https://www.pagetable.com/?p=1135
// https://codebase64.org/doku.php?id=base:how_the_vic_64_serial_bus_works
// http://www.zimmers.net/anonftp/pub/cbm/programming/serial-bus.pdf
// https://github.com/0cjs/sedoc/blob/master/8bit/cbm/serial-bus.md
// https://github.com/mist64/cbmsrc/blob/5c5138ff128d289ccd98d260f700af52c4a75521/DOS_1541_05/seratn.src#L6


#ifndef PROTOCOL_CPBSTANDARDSERIAL_H
#define PROTOCOL_CPBSTANDARDSERIAL_H

// Commodore Peripheral Bus: Standard Serial

#include "_protocol.h"

namespace Protocol
{
    class CPBStandardSerial : public IECProtocol
    {
        private:
        uint8_t receiveBits();
        bool sendBits(uint8_t data);


        public:

        /**
         * ESP timer handle for the Interrupt rate limiting timer
         */
        esp_timer_handle_t timer_send_h = nullptr;

        /**
         * @brief ctor
         */
        CPBStandardSerial();

        /**
         * @brief dtor
         */
        virtual ~CPBStandardSerial();
        
        /**
         * @brief receive byte from bus
         * @return The byte received from bus.
         */
        virtual uint8_t receiveByte();
        
        /**
         * @brief send byte to bus
         * @param b Byte to send
         * @param eoi Signal EOI (end of Information)
         * @return true if send was successful.
         */
        virtual bool sendByte(uint8_t data, bool eoi);
    };
};

#endif // PROTOCOL_CPBSTANDARDSERIAL_H