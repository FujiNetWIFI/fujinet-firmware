#ifndef _FNIO_H_
#define _FNIO_H_

#include <cstdio>

#include "fnFile.h"

namespace fnio
{

#ifdef ESP_PLATFORM
    static inline size_t fread(void *ptr, size_t size, size_t n, fnFile *f) 
    { return std::fread(ptr, size, n, f); }
#else
    static inline size_t fread(void *ptr, size_t size, size_t n, fnFile *f) 
    { return f->read(ptr, size, n); }
#endif

#ifdef ESP_PLATFORM
    static inline size_t fwrite(const void *ptr, size_t size, size_t n, fnFile *f)
    { return std::fwrite(ptr, size, n, f); }
#else
    static inline size_t fwrite(const void *ptr, size_t size, size_t n, fnFile *f)
    { return f->write(ptr, size, n); }
#endif

#ifdef ESP_PLATFORM
    static inline int fseek(fnFile *f, long int off, int whence)
    { return std::fseek(f, off, whence); }
#else
    static inline int fseek(fnFile *f, long int off, int whence)
    { return f->seek(off, whence); }
#endif

#ifdef ESP_PLATFORM
    static inline long int ftell(fnFile *f)
    { return std::ftell(f); }
#else
    static inline long int ftell(fnFile *f)
    { return f->tell(); }
#endif

#ifdef ESP_PLATFORM
    static inline int feof(fnFile *f)
    { return std::feof(f); }
#else
    static inline int feof(fnFile *f)
    { return f->eof(); }
#endif

#ifdef ESP_PLATFORM
    static inline int fflush(fnFile *f)
    { return std::fflush(f); }
#else
    static inline int fflush(fnFile *f)
    { return f->flush(); }
#endif

#ifdef ESP_PLATFORM
    static inline int fclose(fnFile *f)
    { return std::fclose(f); }
#else
    static inline int fclose(fnFile *f)
    { return f->close(); }
#endif

} // namespace fnio

#endif // _FNIO_H_
