/*
 * sit_io.h - bounded streaming byte source over a FILE*.
 *
 * Every decompressor pulls its compressed input one byte at a time
 * through this small (few-KB) buffer, never loading a whole compressed
 * fork into memory. No upstream equivalent - this replaces the calling
 * convention of CSByteStreamHandle.h/.m (produceByteAtOffset:,
 * resetByteStream) with a simpler forward-only pull interface, which is
 * all a streaming decoder that writes to a sink callback needs.
 */
#ifndef FN_SIT_IO_H
#define FN_SIT_IO_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define SIT_IO_BUFSIZE 4096

typedef struct {
    FILE *f;
    long start;          /* absolute offset of window start */
    long end;             /* absolute offset one past the window end */
    long pos;             /* absolute offset of the next unread byte */
    uint8_t buf[SIT_IO_BUFSIZE];
    size_t buflen;         /* valid bytes currently in buf */
    size_t bufpos;         /* next unread index in buf */
    int error;             /* set to 1 after a read error (short read before end) */
} sit_io;

/* Bind io to the byte range [start, start+len) of f. Does not itself
 * seek or read; the first sit_io_getbyte() call performs the fseek. */
void sit_io_init(sit_io *io, FILE *f, long start, long len);

/* Returns the next byte (0-255), or -1 at end of window or on error;
 * check sit_io_error() to tell the two apart. */
int sit_io_getbyte(sit_io *io);

/* Bytes remaining in the window, including anything already buffered. */
long sit_io_bytes_left(const sit_io *io);

int sit_io_error(const sit_io *io);

#endif /* FN_SIT_IO_H */
