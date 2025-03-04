#ifndef _FNIO_H_
#define _FNIO_H_

#include <cstddef>

#if defined(BUILD_ATARI) || defined(BUILD_APPLE) || defined(BUILD_COCO) || defined(BUILD_RS232)
  // ATARI and APPLE was already ported to use fnio
  // set FNIO_IS_STDIO to force stdio

  // for testing/debugging, it is possible to switch to stdio
  // note: TNFS will not work on FN-PC with stdio
  //       FTP and SMB works only with fnio
  //#ifdef ESP_PLATFORM
  //  #define FNIO_IS_STDIO
  //#endif
#else
  // all other platforms use stdio (not yet prepared for fnio)
  #define FNIO_IS_STDIO
#endif


#ifdef FNIO_IS_STDIO
  #include <cstdio>
  #include <unistd.h>  // for fsync
  typedef std::FILE fnFile;
#else
  #include "fnFile.h"
  typedef FileHandler fnFile;
#endif

namespace fnio
{

#ifdef FNIO_IS_STDIO
    static inline size_t fread(void *ptr, size_t size, size_t n, fnFile *f) 
    { return std::fread(ptr, size, n, f); }

    static inline size_t fwrite(const void *ptr, size_t size, size_t n, fnFile *f)
    { return std::fwrite(ptr, size, n, f); }

    static inline int fseek(fnFile *f, long int off, int whence)
    { return std::fseek(f, off, whence); }

    static inline long int ftell(fnFile *f)
    { return std::ftell(f); }

    static inline int feof(fnFile *f)
    { return std::feof(f); }

    static inline int fflush(fnFile *f)
    {
      int ret = std::fflush(f);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    #ifdef ESP_PLATFORM
      ret = fsync(fileno(f)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    #endif
      return ret;
    }

    static inline int fclose(fnFile *f)
    { return std::fclose(f); }

#else
    static inline size_t fread(void *ptr, size_t size, size_t n, fnFile *f) 
    { return f->read(ptr, size, n); }

    static inline size_t fwrite(const void *ptr, size_t size, size_t n, fnFile *f)
    { return f->write(ptr, size, n); }

    static inline int fseek(fnFile *f, long int off, int whence)
    { return f->seek(off, whence); }

    static inline long int ftell(fnFile *f)
    { return f->tell(); }

    static inline int feof(fnFile *f)
    { return f->eof(); }

    static inline int fflush(fnFile *f)
    { return f->flush(); }

    static inline int fclose(fnFile *f)
    { return f->close(); }

#endif

} // namespace fnio

#endif // _FNIO_H_
