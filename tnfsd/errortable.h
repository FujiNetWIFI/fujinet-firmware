#ifndef _ERRORTABLE_H
#define _ERRORTABLE_H
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
 * Error table
 *
 * */

#define TNFS_SUCCESS	0x00
#define TNFS_EPERM	0x01
#define TNFS_ENOENT	0x02
#define TNFS_EIO	0x03
#define TNFS_ENXIO	0x04
#define TNFS_E2BIG	0x05
#define TNFS_EBADF	0x06
#define TNFS_EAGAIN	0x07
#define TNFS_ENOMEM	0x08
#define TNFS_EACCES	0x09
#define TNFS_EBUSY	0x0A
#define TNFS_EEXIST	0x0B
#define TNFS_ENOTDIR	0x0C
#define TNFS_EISDIR	0x0D
#define TNFS_EINVAL	0x0E
#define TNFS_ENFILE	0x0F
#define TNFS_EMFILE	0x10
#define TNFS_EFBIG	0x11
#define TNFS_ENOSPC	0x12
#define TNFS_ESPIPE	0x13
#define TNFS_EROFS	0x14
#define TNFS_ENAMETOOLONG 0x15
#define TNFS_ENOSYS	0x16
#define TNFS_ENOTEMPTY	0x17
#define TNFS_ELOOP	0x18
#define TNFS_ENODATA	0x19
#define TNFS_ENOSTR	0x1A
#define TNFS_EPROTO	0x1B
#define TNFS_EBADFD	0x1C
#define TNFS_EUSERS	0x1D
#define TNFS_ENOBUFS	0x1E
#define TNFS_EALREADY	0x1F
#define TNFS_ESTALE	0x20
#define TNFS_EOF	0x21

void tnfs_init_errtable();
unsigned char tnfs_error(int econv);

#endif
