#ifndef _AFPFS_RANDOM_H_
#define _AFPFS_RANDOM_H_

#include <stddef.h>

/* Fills the whole buffer with cryptographic random bytes, or aborts. */
void afp_fill_random(void *buffer, size_t length);

#endif
