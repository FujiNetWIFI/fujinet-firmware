// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
//
// This file is part of Pi1541.
// 
// Pi1541 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Pi1541 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Pi1541. If not, see <http://www.gnu.org/licenses/>.

#ifndef PETSCII_H
#define PETSCII_H

static inline uint8_t ascii2petscii(uint8_t ch)
{
	if (ch > 64 && ch < 91) ch += 128;
	else if (ch > 96 && ch < 123) ch -= 32;
	else if (ch > 192 && ch < 219) ch -= 128;
	else if (ch == 95) ch = 164; // to handle underscore 
	return ch;
}
static inline uint8_t petscii2ascii(uint8_t ch)
{
	if (ch >(64 + 128) && ch < (91 + 128)) ch -= 128;
	else if (ch >(96 - 32) && ch < (123 - 32)) ch += 32;
	else if (ch >(192 - 128) && ch < (219 - 128)) ch += 128;
	else if (ch == 164) ch = 95; // to handle underscore 
	return ch;
}

static inline uint8_t petscii2screen(uint8_t ch)
{
	if ((ch >= 0x40 && ch <= 0x5F) || (ch >= 0xa0 && ch <= 0xbf)) ch -= 0x40;
	else if (ch >= 0xc0 && ch <= 0xdf) ch -= 0x80;
	else if (ch <= 0x1f) ch += 0x80;
	else if ((ch >= 0x60 && ch <= 0x7F) || (ch >= 0x90 && ch <= 0x9f)) ch += 0x40;
	return ch;
}

static inline uint8_t screen2petscii(uint8_t ch)
{
	if (ch <= 0x1F || (ch >= 0x60 && ch <= 0x7f)) ch += 0x40;
	else if (ch >= 0x40 && ch <= 0x5f) ch += 0x80;
	else if (ch >= 0x80 && ch <= 0x9f) ch -= 0x80;
	else if ((ch >= 0xa0 && ch <= 0xbF) || (ch >= 0xd0 && ch <= 0xdf)) ch -= 0x40;
	return ch;
}
#endif // PETSCII_H
