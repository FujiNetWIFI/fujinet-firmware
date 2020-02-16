#ifndef _DIRECTORY_H
#define _DIRECTORY_h
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
 * TNFS daemon directory functions
 *
 * */

#include "tnfs.h"

/* initialize and set the root dir */
int tnfs_setroot(char *rootdir);

/* validates a path points to an actual directory */
int validate_dir(Session *s, const char *path);
void normalize_path(char *dst, char *src, int pathsz);

/* get the root directory for the given session */
void get_root(Session *s, char *buf, int bufsz);

/* open, read, close directories */
void tnfs_opendir(Header *hdr, Session *s, unsigned char *databuf, int datasz);
void tnfs_readdir(Header *hdr, Session *s, unsigned char *databuf, int datasz);
void tnfs_closedir(Header *hdr, Session *s, unsigned char *databuf, int datasz);

/* create and remove directories */
void tnfs_mkdir(Header *hdr, Session *s, unsigned char *databuf, int datasz);
void tnfs_rmdir(Header *hdr, Session *s, unsigned char *databuf, int datasz);
#endif
