#ifndef COMPAT_STRING_H
#define COMPAT_STRING_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__linux__) || defined(_WIN32) && !defined(ESP_PLATFORM)
#include <sys/types.h>
size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
#endif

#ifdef __cplusplus
}
#endif

#endif // COMPAT_STRING_H