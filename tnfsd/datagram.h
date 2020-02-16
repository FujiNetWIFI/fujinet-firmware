#ifndef _DATAGRAM_H
#define _DATAGRAM_H

/* The MIT License;
 * Copyright (c) 2010 Dylan Smith
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
 * TNFS daemon datagram handler
 *
 * */

#include <sys/types.h>
#include <sys/select.h>

#ifdef UNIX
#include <arpa/inet.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

#ifndef in_addr_t
#define in_addr_t uint32_t
#endif

/* definition of FD_COPY macro used in OpenBSD */
#ifndef FD_COPY
#define FD_COPY(f, t)   memcpy(t, f, sizeof(*(f)))
#endif

#include "tnfs.h"

/* Handle the socket interface */
void tnfs_sockinit();
void tnfs_mainloop();
void tnfs_handle_udpmsg();
void tcp_accept(int *fdlist);
void tnfs_handle_tcpmsg(int cli_fd);
void tnfs_decode(struct sockaddr_in *cliaddr, 
		int rxbytes, unsigned char *rxbuf);
void tnfs_badcommand(Header *hdr, Session *sess);
void tnfs_send(Session *sess, Header *hdr, unsigned char *msg, int msgsz);
void tnfs_resend(Session *sess, struct sockaddr_in *cliaddr);

#endif
