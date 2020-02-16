#ifndef _CONFIG_H
#define _CONFIG_H
/* The MIT License
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
 * tnfs daemon compile time configuration */

#define TNFSD_PORT	16384	/* UDP port to listen on */
#define MAXMSGSZ	532	/* maximum size of a TNFS message */
#define MAX_FD_PER_CONN	16	/* maximum open file descriptors per client */
#define MAX_DHND_PER_CONN 8	/* max open directories per client */
#define MAX_CLIENTS	256	/* maximum number of UDP clients */
#define MAX_TCP_CONN	256	/* Maximum number of TCP clients */
#define TNFS_HEADERSZ	4	/* minimum header size */
#define MAX_TNFSPATH	256	/* maximum path length */
#define MAX_FILEPATH	384	/* Maximum path + filename */
#define MAX_ROOT	128	/* maximum root dir length */
#define PROTOVERSION_LSB 0x00	/* Protocol version, LSB */
#define PROTOVERSION_MSB 0x01	/* Protocol version, MSB */
#define TIMEOUT_LSB	0xE8	/* Timeout LSB (1 sec) */
#define TIMEOUT_MSB	0x03	/* Timeout MSB (1 sec) */
#define MAX_FILENAME_LEN 256	/* longest filename supported */
#define MAX_IOSZ	512	/* maximum size of an IO operation */


#endif
