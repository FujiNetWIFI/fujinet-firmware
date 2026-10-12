#ifndef __MACROMAN_H_
#define __MACROMAN_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MacRoman to UTF-8.  Returns 0, or -1 if dest was too small; dest is then
 * cut on a character boundary. */
int afp_macroman_to_utf8(const char * src, char * dest, size_t dest_len);

#ifdef __cplusplus
}
#endif

#endif
