// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// This file is part of Meatloaf but adapted for use in the FujiNet project
// https://github.com/FujiNetWIFI/fujinet-platformio
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

// https://github.com/MEGA65/open-roms/blob/master/doc/Protocol-JiffyDOS.md

#ifndef PROTOCOL_JIFFYDOS_H
#define PROTOCOL_JIFFYDOS_H

#include <Arduino.h>

#include "cbmstandardserial.h"

class JiffyDOS : public CBMStandardSerial
{

protected:
	int16_t receiveByte(void) override;
	bool sendByte(uint8_t data, bool signalEOI) override;
};

#endif
