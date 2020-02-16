#ifndef _TNFS_FILE
#define _TNFS_FILE
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
 * TNFS file functions
 *
 * */

#include "tnfs.h"

#define TNFS_O_RDONLY	0x0001
#define TNFS_O_WRONLY	0x0002
#define TNFS_O_RDWR		0x0003
#define TNFS_O_ACCMODE	0x0003

#define TNFS_O_APPEND	0x0008
#define TNFS_O_CREAT	0x0100
#define TNFS_O_TRUNC	0x0200
#define TNFS_O_EXCL		0x0400

#define ST_MODE_OFFSET	0x00
#define ST_UID_OFFSET	0x02
#define ST_GID_OFFSET	0x04
#define ST_SIZE_OFFSET	0x06
#define ST_ATIME_OFFSET	0x0A
#define ST_MTIME_OFFSET	0x0E
#define ST_CTIME_OFFSET	0x12
#define TNFS_STAT_SIZE	0x16

#define TNFS_SEEK_SET	0x00
#define TNFS_SEEK_CUR	0x01
#define TNFS_SEEK_END	0x02

void tnfs_open_deprecated(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_open(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_read(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_write(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_lseek(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_close(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_stat(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_unlink(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_chmod(Header *hdr, Session *s, unsigned char *buf, int bufsz);
void tnfs_rename(Header *hdr, Session *s, unsigned char *buf, int bufsz);

int tnfs_valid_filename(Session *s,
                        char *fullpath,
                        char *filename, int fnsize);
int tnfs_make_mode(unsigned int flags);
int validate_fd(Header *hdr, Session *s, unsigned char *buf, int bufsz,
		int correctsize);
int getwhence(unsigned char tnfs_whence);

#endif
