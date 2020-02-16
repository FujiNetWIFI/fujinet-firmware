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
 * TNFS daemon logging functions
 *
 * */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "log.h"
#include "tnfs.h"

void die(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(-1);
}

void TNFSMSGLOG(Header *hdr, const char *msg)
{
	unsigned char *ip=(unsigned char *)&hdr->ipaddr;
	fprintf(stderr, "Cli: %d.%d.%d.%d Session: %x : %s\n",
			ip[0], ip[1], ip[2], ip[3],
			hdr->sid, msg);
#ifdef WIN32
	fflush(stderr);
#endif
}

void MSGLOG(in_addr_t ipaddr, const char *msg)
{
	unsigned char *ip=(unsigned char *)&ipaddr;
	fprintf(stderr, "Cli: %d.%d.%d.%d: %s\n",
			ip[0], ip[1], ip[2], ip[3],
			msg);
#ifdef WIN32
	fflush(stderr);
#endif
}
