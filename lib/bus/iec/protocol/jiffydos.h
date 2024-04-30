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
//
// https://github.com/MEGA65/open-roms/blob/master/doc/Protocol-JiffyDOS.md
// http://www.nlq.de/
// http://www.baltissen.org/newhtm/sourcecodes.htm
// https://www.amigalove.com/viewtopic.php?t=1734
// https://ar.c64.org/rrwiki/images/4/48/The_Transactor_Vol09_03_1989_Feb_JD_review.pdf
// https://web.archive.org/web/20090826145226/http://home.arcor.de/jochen.adler/ajnjil-t.htm
// https://web.archive.org/web/20220423162959/https://sites.google.com/site/h2obsession/CBM/C128/JiffySoft128
// https://www.c64-wiki.com/wiki/SJLOAD
// https://github.com/mist64/cbmbus_doc/blob/cb021f3454b499c579c265859ce67ba99e85652b/7%20JiffyDOS.md
// https://ar.c64.org/rrwiki/images/4/48/The_Transactor_Vol09_03_1989_Feb_JD_review.pdf
// https://c65gs.blogspot.com/2023/10/hardware-accelerated-iec-serial.html?m=0
// https://c65gs.blogspot.com/2023/12/hardware-accelerated-iec-serial.html?m=0
// https://c65gs.blogspot.com/2024/01/hardware-accelerated-iec-serial-bus.html?m=0
// https://c65gs.blogspot.com/2024/01/hardware-accelerated-iec-controller.html?m=0
//

#ifndef PROTOCOL_JIFFYDOS_H
#define PROTOCOL_JIFFYDOS_H

#include "_protocol.h"

#define TIMING_JIFFY_BITPAIR
#define TIMING_JIFFY_BYTE

namespace Protocol
{
	class JiffyDOS : public IECProtocol
	{
		public:
			JiffyDOS() {

			};

		protected:
			uint8_t loadmode = 0;
			uint8_t skipeoi = 0;
			int16_t receiveByte(void) override;
			bool sendByte(uint8_t data, bool eoi) override;
			bool sendByte(uint8_t data, bool eoi, uint8_t loadflags);
	};
};

#endif // PROTOCOL_JIFFYDOS_H
