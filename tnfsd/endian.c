/* The MIT License
 *
 * Copyright (c) 2010 Dylan Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Endian-ness conversions
 * Converts a fragment of a TNFS message into the correct endian int
 * or long
 *
 * */

#include "endian.h"

/* Since most 8 bit micros are little-endian, the TNFS protocol is also
 * little endian. Big endian archs must convert. It also deals with
 * alignment issues you get with certain little endian archs by moving
 * data in a portable manner. (For 8 bit systems it may be worth
 * adding a faster method, since these don't have alignment issues) */
uint16_t tnfs16uint(unsigned char *value)
{
	return (*(value+1) << 8) + *value;
}

uint32_t tnfs32uint(unsigned char *value)
{
	return ((uint32_t)*(value+3) << 24) + 
		((uint32_t)*(value+2) << 16) + 
		((uint32_t)*(value+1) << 8) + *value;
}

void uint16tnfs(unsigned char *buf, uint16_t value)
{
	*buf=value & 0xFF;
	*(buf+1)=(value >> 8) & 0xFF;
}

void uint32tnfs(unsigned char *buf, uint32_t value)
{
	*buf=value & 0xFF;
	*(buf+1)=(value >> 8) & 0xFF;
	*(buf+2)=(value >> 16) & 0xFF;
	*(buf+3)=(value >> 24) & 0xFF;
}
