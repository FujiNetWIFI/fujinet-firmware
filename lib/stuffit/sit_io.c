/*
 * sit_io.c - see sit_io.h.
 */
#include "sit_io.h"

void sit_io_init(sit_io *io, FILE *f, long start, long len)
{
    io->f = f;
    io->start = start;
    io->end = start + len;
    io->pos = start;
    io->buflen = 0;
    io->bufpos = 0;
    io->error = 0;
}

int sit_io_getbyte(sit_io *io)
{
    if (io->bufpos >= io->buflen) {
        if (io->error) return -1;
        if (io->pos >= io->end) return -1;

        if (fseek(io->f, io->pos, SEEK_SET) != 0) {
            io->error = 1;
            return -1;
        }

        long want = io->end - io->pos;
        if (want > (long)SIT_IO_BUFSIZE) want = (long)SIT_IO_BUFSIZE;

        size_t got = fread(io->buf, 1, (size_t)want, io->f);
        io->buflen = got;
        io->bufpos = 0;

        if (got == 0) {
            io->error = 1;
            return -1;
        }
    }

    int c = io->buf[io->bufpos++];
    io->pos++;
    return c;
}

long sit_io_bytes_left(const sit_io *io)
{
    return io->end - io->pos + (long)(io->buflen - io->bufpos);
}

int sit_io_error(const sit_io *io)
{
    return io->error;
}
