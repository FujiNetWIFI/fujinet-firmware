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

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#ifdef UNIX
#include <sys/uio.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include "tnfs.h"
#include "errortable.h"
#include "datagram.h"
#include "tnfs_file.h"
#include "directory.h"
#include "errortable.h"
#include "endian.h"
#include "bsdcompat.h"

char fnbuf[MAX_FILEPATH];
unsigned char iobuf[MAX_IOSZ+2];	/* 2 bytes added for the size param */

void tnfs_open_deprecated(Header *hdr, Session *s, unsigned char *buf,
	int bufsz) {
	unsigned char *bufptr;

	// new format datagram is slightly larger than the deprecated one.
	unsigned char *newbuf=(unsigned char *)malloc(bufsz+2);
	
	// translate deprecated file flags and mode
	*newbuf=*buf;
	memcpy(newbuf+4, buf+2, bufsz-2);
	bufptr=buf+1;

	// mode = 0644
	*(newbuf+2)=0xA4;
	*(newbuf+3)=0x01;

	if(*bufptr & 0x01)
		*newbuf &= 0x08;

	*(newbuf+1)=*bufptr >> 1;
	
	tnfs_open(hdr, s, newbuf, bufsz+2);
	free(newbuf);
}

void tnfs_open(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	int i, fd;
	int flags, mode;
	unsigned char reply[2];

	if(bufsz < 3 ||
	  tnfs_valid_filename(s, fnbuf, (char *)buf+4, bufsz-4) < 0)
	{
		/* filename could not be constructed */
		hdr->status=TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	for(i=0; i<MAX_FD_PER_CONN; i++)
	{
		if(s->fd[i] == 0)
		{
			flags = *buf + (*(buf+1)* 256);
			mode = *(buf+2) + (*(buf+3)* 256);

#ifdef WITH_ZIP
			fd=zipopen(fnbuf, tnfs_make_mode(flags), mode);
#else
			fd=open(fnbuf, tnfs_make_mode(flags), mode);
#endif
#ifdef DEBUG
			fprintf(stderr, "filename: %s\n", (char *)buf+4);
			fprintf(stderr, "flags: %u\n", flags);
			fprintf(stderr, "mode: %o\n", mode);
			fprintf(stderr, "open: fd=%d\n", fd);
#endif
			if(fd <= 0)
			{
				hdr->status=tnfs_error(errno);
				tnfs_send(s, hdr, NULL, 0);
				return;
			}

			s->fd[i]=fd;
			hdr->status=TNFS_SUCCESS;
			reply[0]=(unsigned char)i;
			tnfs_send(s, hdr, reply, 1);
			return;
		}
	}
	hdr->status=TNFS_EMFILE;
	tnfs_send(s, hdr, NULL, 0);
}

void tnfs_read(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	int readsz;
	int requestsz;

	/* incoming data buffer must be 3 bytes, fd + readbytes */
	int fd=validate_fd(hdr, s, buf, bufsz, 3);
	if(!fd) return;

	requestsz=tnfs16uint(buf+1);
	if(requestsz > MAX_IOSZ)
		requestsz=MAX_IOSZ;
	readsz=read(fd, iobuf+2, (size_t)requestsz);
	if(readsz > 0)
	{
		hdr->status=TNFS_SUCCESS;
		uint16tnfs(iobuf, (uint16_t)readsz);

		/* final data buffer is size of read + 2 bytes */
		tnfs_send(s, hdr, iobuf, readsz+2);
	}
	else if(readsz == 0)
	{
#ifdef DEBUG
		fprintf(stderr,"EOF\n");
#endif
		hdr->status=TNFS_EOF;
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
		hdr->status=tnfs_error(errno);
#ifdef DEBUG
		fprintf(stderr, "Bad read: errno=%d tnfs_errno=%d fd=%d\n",
				errno, hdr->status, fd);
#endif
		tnfs_send(s, hdr, NULL, 0);
	}
}

void tnfs_write(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	int writesz;
	unsigned char response[2];

	/* a write must be at least 4 bytes - fd, 16 bit 0x0001, 1 byte
	 * to write out would be the minimum packet */
	int fd=validate_fd(hdr, s, buf, bufsz, 4);
	if(!fd) return;

	writesz=tnfs16uint(buf+1);
	writesz=write(fd, buf+3, (size_t)writesz);
	if(writesz > 0)
	{
		hdr->status=0;
		uint16tnfs(response, (uint16_t)writesz);
		tnfs_send(s, hdr, response, 2);
	}
	else
	{
		hdr->status=tnfs_error(errno);
		tnfs_send(s, hdr, NULL, 0);
	}
}

void tnfs_lseek(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	int32_t offset;
	int whence;
	int result;

	int fd=validate_fd(hdr, s, buf, bufsz, 6);
	if(!fd) return;

	/* should work for all architectures I know of in terms
	 * of signedness */
	offset=(int32_t)tnfs32uint(buf+2);
	whence=getwhence(*(buf+1));
#ifdef DEBUG
	fprintf(stderr, "lseek: offset=%d (%x) whence=%d tnfs_whence=%d\n", 
			offset, offset, whence, *(buf+1));
#endif
	if((result=lseek(fd, (off_t)offset, whence)) < 0)
	{
		hdr->status=tnfs_error(errno);
#ifdef DEBUG
		fprintf(stderr, "lseek: failed, errno=%d tnfs_errno=%d\n",
				errno, hdr->status);
#endif
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
#ifdef DEBUG
		fprintf(stderr,"lseek: New location=%d (%x)\n", result, result);
#endif
		hdr->status=TNFS_SUCCESS;
		tnfs_send(s, hdr, NULL, 0);
	}
}

void tnfs_close(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	int fd=validate_fd(hdr, s, buf, bufsz, 1);
	if(!fd) return;

	if(close(fd) == 0)
	{
		s->fd[*buf]=0;	/* clear the session's descriptor */
		hdr->status=TNFS_SUCCESS;
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
		hdr->status=tnfs_error(errno);
		tnfs_send(s, hdr, NULL, 0);
	}
}

void tnfs_stat(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	struct stat statinfo;
	unsigned char msgbuf[TNFS_STAT_SIZE];
#ifdef DEBUG
	fprintf(stderr, "stat: bufsz=%d buf=%s\n", bufsz, buf);
#endif

	if(bufsz < 2 ||
	  tnfs_valid_filename(s, fnbuf, (char *)buf, bufsz) < 0)
	{
		/* filename could not be constructed */
		hdr->status=TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}
#ifdef DEBUG
	fprintf(stderr, "stat: path=%s\n", fnbuf);
#endif

	if(stat(fnbuf, &statinfo) == 0)
	{
#ifdef DEBUG
		fprintf(stderr, "stat: OK\n");
#endif
		uint16tnfs(msgbuf+ST_MODE_OFFSET, (uint16_t)statinfo.st_mode);
		uint16tnfs(msgbuf+ST_UID_OFFSET, (uint16_t)statinfo.st_uid);
		uint16tnfs(msgbuf+ST_GID_OFFSET, (uint16_t)statinfo.st_gid);
		uint32tnfs(msgbuf+ST_SIZE_OFFSET, (uint32_t)statinfo.st_size);
		uint32tnfs(msgbuf+ST_ATIME_OFFSET, (uint32_t)statinfo.st_atime);
		uint32tnfs(msgbuf+ST_MTIME_OFFSET, (uint32_t)statinfo.st_mtime);
		uint32tnfs(msgbuf+ST_CTIME_OFFSET, (uint32_t)statinfo.st_ctime);
		hdr->status=TNFS_SUCCESS;
		tnfs_send(s, hdr, msgbuf, TNFS_STAT_SIZE);
	}
	else
	{
		hdr->status=tnfs_error(errno);
#ifdef DEBUG
		fprintf(stderr, "stat: Failed with errno=%d (%d)\n", 
				hdr->status, errno);
#endif
		tnfs_send(s, hdr, NULL, 0);
	}
}

void tnfs_unlink(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	if(*(buf+bufsz-1) != 0 ||
	   tnfs_valid_filename(s, fnbuf, (char *)buf, bufsz) < 0)
	{
		hdr->status=TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
		if(unlink(fnbuf) == 0)
		{
			hdr->status=TNFS_SUCCESS;
			tnfs_send(s, hdr, NULL, 0);
		}
		else
		{
			hdr->status=tnfs_error(errno);
			tnfs_send(s, hdr, NULL, 0);
		}
	}
}

void tnfs_chmod(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
}

void tnfs_rename(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	char tobuf[MAX_FILEPATH];
	char *to=memchr(buf, 0x00, bufsz);
	if(to == NULL || to == (char *)buf+bufsz-1 || *(buf+bufsz-1) != 0)
	{
		hdr->status=TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	/* point at byte after the NULL */
	to++;
	if(tnfs_valid_filename(s, fnbuf, (char *)buf, bufsz) < 0 ||
	   tnfs_valid_filename(s, tobuf, to, 
		   (buf+bufsz)-(unsigned char *)to) < 0)
	{
		hdr->status=TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	if(rename((char *)fnbuf, tobuf) < 0)
	{
		hdr->status=tnfs_error(errno);
		tnfs_send(s, hdr, NULL, 0);
#ifdef DEBUG
		fprintf(stderr,
		 "rename: errno=%d status=%d from=%s to=%s\n",
		 errno, hdr->status, buf, to);
#endif
	}
	else
	{
		hdr->status=TNFS_SUCCESS;
		tnfs_send(s, hdr, NULL, 0);
	}
}

int tnfs_valid_filename(Session *s,
		        char *fullpath,
		        char *filename, int fnsize)
{
	if(*(filename+fnsize-1) != 0)
	{
		/* no null terminator */
		return -1;
	}
	if(strstr(filename, "..") != NULL)
	{
		return -1;
	}
	get_root(s, fullpath, MAX_FILEPATH);
	strlcat(fullpath, filename, MAX_FILEPATH);
	normalize_path(fullpath, fullpath, MAX_FILEPATH);
	return 0;
}

int tnfs_make_mode(unsigned int flags)
{
#ifndef WIN32
	int mflags=0;
#else
	/* the python guys seem to have run into this one too, with
	 * win32... */
	int mflags=O_BINARY;
#endif
	if((flags & TNFS_O_ACCMODE) == TNFS_O_RDONLY)
		mflags |= O_RDONLY;
	if((flags & TNFS_O_ACCMODE) == TNFS_O_WRONLY)
		mflags |= O_WRONLY;
	if((flags & TNFS_O_ACCMODE) == TNFS_O_RDWR)
		mflags |= O_RDWR;

	if(flags & TNFS_O_APPEND)
		mflags |= O_APPEND;
	if(flags & TNFS_O_CREAT)
		mflags |= O_CREAT;
	if(flags & TNFS_O_EXCL)
		mflags |= O_EXCL;
	if(flags & TNFS_O_TRUNC)
		mflags |= O_TRUNC;

	return mflags;
}

int validate_fd(Header *hdr, Session *s, unsigned char *buf, int bufsz,
		int propersize)
{
	if(bufsz < propersize ||
	   *buf > MAX_FD_PER_CONN)
	{
#ifdef DEBUG
		fprintf(stderr,"BAD FD: bufsz=%d propersize=%d fd=%d max=%d",
				bufsz, propersize, *buf, MAX_FD_PER_CONN);
#endif
		hdr->status=TNFS_EBADFD;
		tnfs_send(s, hdr, NULL, 0);
		return 0;
	}
	return s->fd[*buf];
}

int getwhence(unsigned char tnfs_whence)
{
	switch(tnfs_whence)
	{
		case TNFS_SEEK_CUR:
			return SEEK_CUR;
		case TNFS_SEEK_END:			
			return SEEK_END;
		default:			
			return SEEK_SET;
	}
}

