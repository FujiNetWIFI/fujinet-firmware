#ifndef _TNFS_H
#define _TNFS_H

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
 * TNFS structs and definitions
 *
 * */

#include <stdint.h>
#include <dirent.h>

#ifdef UNIX
#include <arpa/inet.h>
#endif
#ifdef WIN32
#include <windows.h>
#endif

#ifndef in_addr_t
#define in_addr_t uint32_t
#endif

#ifndef socklen_t
#define socklen_t int
#endif

#include "config.h"

/* tnfs command IDs */
#define TNFS_MOUNT	0x00
#define TNFS_UMOUNT	0x01

#define TNFS_OPENDIR	0x10
#define TNFS_READDIR	0x11
#define TNFS_CLOSEDIR	0x12

#define TNFS_OPENFILE	0x20
#define	TNFS_READBLOCK	0x21
#define TNFS_WRITEBLOCK	0x22
#define TNFS_CLOSEFILE	0x23
#define TNFS_STATFILE	0x24
#define TNFS_SEEKFILE	0x25
#define TNFS_UNLINKFILE	0x26
#define TNFS_CHMODFILE	0x27
#define TNFS_RENAMEFILE	0x28

/* command classes etc. */
#define CLASS_SESSION	0x00
#define CLASS_DIRECTORY	0x10
#define CLASS_FILE	0x20
#define NUM_DIRCMDS	5
#define NUM_FILECMDS	10

typedef struct _session
{
	uint16_t sid;			/* session ID */
	in_addr_t ipaddr;		/* client addr */
	uint8_t seqno;			/* last sequence number */
	int fd[MAX_FD_PER_CONN];	/* file descriptors */
	DIR *dhnd[MAX_DHND_PER_CONN];	/* directory handles */
	char *root;			/* requested root dir */
	unsigned char lastmsg[MAXMSGSZ];/* last message sent */
	int lastmsgsz;			/* last message's size inc. hdr */
	uint8_t lastseqno;		/* last sequence number */
	uint8_t isTCP;			/* uses the TCP transport */

} Session;

typedef struct _header
{
	uint16_t sid;			/* session id */
	uint8_t seqno;			/* sequence number */
	uint8_t cmd;			/* command */
	uint8_t status;			/* command's status */
	in_addr_t ipaddr;		/* client address */
	uint16_t port;			/* client port address */
} Header;

typedef	void(*tnfs_cmdfunc)(Header *hdr, Session *sess,
				unsigned char *buf, int bufsz);

#endif

