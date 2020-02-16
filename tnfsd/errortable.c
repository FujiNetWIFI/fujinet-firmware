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
 * Create an error table and provide a method for error lookup.
 *
 * */
#include <string.h>
#include <errno.h>

#include "errortable.h"

#ifdef NEED_ERRTABLE
unsigned char etable[256];

void tnfs_init_errtable()
{
	memset((unsigned char *)etable, 0xFF, sizeof(etable));
	etable[EPERM]=TNFS_EPERM;
	etable[ENOENT]=TNFS_ENOENT;
	etable[EIO]=TNFS_EIO;
	etable[ENXIO]=TNFS_ENXIO;
	etable[E2BIG]=TNFS_E2BIG;
	etable[EBADF]=TNFS_EBADF;
	etable[EAGAIN]=TNFS_EAGAIN;
	etable[ENOMEM]=TNFS_ENOMEM;
	etable[EACCES]=TNFS_EACCES;
	etable[EBUSY]=TNFS_EBUSY;
	etable[EEXIST]=TNFS_EEXIST;
	etable[ENOTDIR]=TNFS_ENOTDIR;
	etable[EISDIR]=TNFS_EISDIR;
	etable[EINVAL]=TNFS_EINVAL;
	etable[ENFILE]=TNFS_ENFILE;
	etable[EMFILE]=TNFS_EMFILE;
	etable[EFBIG]=TNFS_EFBIG;
	etable[ENOSPC]=TNFS_ENOSPC;
	etable[ESPIPE]=TNFS_ESPIPE;
	etable[EROFS]=TNFS_EROFS;
	etable[ENOSYS]=TNFS_ENOSYS;
	etable[ENAMETOOLONG]=TNFS_ENAMETOOLONG;
	etable[ENOTEMPTY]=TNFS_ENOTEMPTY;
#ifdef ELOOP
	etable[ELOOP]=TNFS_ELOOP;
#endif
#ifdef ENODATA
	etable[ENODATA]=TNFS_ENODATA;
#endif
#ifdef ENOSTR
	etable[ENOSTR]=TNFS_ENOSTR;
#endif
#ifdef EPROTO
	etable[EPROTO]=TNFS_EPROTO;
#elif EPROTOTYPE
	etable[EPROTOTYPE]=TNFS_EPROTO;
#endif
#ifdef EALREADY
	etable[EALREADY]=TNFS_EALREADY;
#endif
#ifdef ESTALE
	etable[ESTALE]=TNFS_ESTALE;
#endif
}
#endif

unsigned char tnfs_error(int converr)
{
	if(converr >= 0 && converr <= 0xFF)
		return etable[converr];
	return 0xFF;
}

