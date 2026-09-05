/*
 * mio.c - Colin Kelley  1-18-87
 *   routines to emulate temporary file handling with memory instead
 *
 * rjm 980212 - changed to use memcpy rather than bcopy
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define UNUSED(var) ((void) var)

#define MALLOC_SIZE	10000
#define JUNK_FPTR	stdin	/* junk ptr to return (unused) */

static unsigned char *mhead;	/* pointer to start of malloc()d area */
static unsigned char *mend;	/* pointer to current (just beyond) EOF*/
static unsigned char *mptr;	/* pointer to current position */
static unsigned int msize;	/* size of chunk mhead points to */

FILE *mfopen(char *filename,char *mode)
{
	UNUSED (filename);
	UNUSED (mode);
	if ((mhead = malloc(MALLOC_SIZE)) == NULL) {
		msize = 0;
		return (NULL);
	}
	msize = MALLOC_SIZE;
	mend = mptr = mhead;
	return (JUNK_FPTR);				/* not used */
}

int mfclose(FILE *f)
{
	UNUSED (f);
	if (mhead) {
		free(mhead);
		return (0);
	}
	else
		return (-1);
}

int mfputc(unsigned int c,FILE *f)
{
	unsigned char *p;
	UNUSED (f);
	while (mptr >= mhead + msize) {
		if ((p = realloc(mhead,msize+MALLOC_SIZE)) == (unsigned char *)-1) {
			fputs("mio: out of memory\n",stderr);
			return (-1);
		}
		else {
			msize += MALLOC_SIZE;
			mptr = (unsigned char *) (p + (unsigned int)(mptr - mhead));
			mhead = p;
		}
	}
	*mptr = c & 255;
	mend = ++mptr;
	return c;
}

int mfgetc(FILE *f)
{
	UNUSED (f);
	if (mptr >= mend)		/* no characters left */
		return (-1);
	else
		return (*mptr++);
}

int mfseek(FILE *f,long loc,int origin)
{
	UNUSED (f);
	if (origin != 0) {
		fputs("mseek() only implemented with 0 origin",stderr);
		return (-1);
	}
	mptr = mhead + loc;
	return (0);
}

int mfread(char *ptr, unsigned int size, unsigned int nitems, FILE *f)
{
	unsigned int i = 0;
	UNUSED (f);
	while (i < nitems) {
		if ((mptr + size) > mend)
			break;
		memcpy(ptr,mptr,size);
		ptr += size;
		mptr += size;
		i++;
	}
	return (i);
}

int mfwrite(char *ptr, int size, int nitems, FILE *f)
{
	int i = 0;
	unsigned char *p;
	UNUSED (f);
	while (i < nitems) {
		while (mptr + size >= mhead + msize) {
			if ((p = realloc(mhead,msize+MALLOC_SIZE)) == (unsigned char *)-1){
				fputs("mio: out of memory\n",stderr);
				return (-1);
			}
			else {
				msize += MALLOC_SIZE;
				mptr = (unsigned char *) (p + (unsigned int)(mptr - mhead));
				mhead = p;
			}
		}
		if ((mptr + size) > mhead + msize)
			break;
		memcpy(mend,ptr,size);
		ptr += size;
		mend += size;
		mptr = mend;
	}
	return (i);
}
