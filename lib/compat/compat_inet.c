#include <string.h>
#include <stdio.h>

#include "compat_inet.h"

char *compat_inet_ntoa(in_addr_t in)
{
    struct in_addr sin;
    sin.s_addr = in;
    return inet_ntoa(sin);
}

// NOTE: This is not thread safe function
// TODO: Thread safe variant - compat_sockstrerror_r
const char *compat_sockstrerror(int err)
{
#if defined(_WIN32)
    static char msgbuf[256];    // for a message up to 255 bytes.
    msgbuf [0] = '\0';          // Microsoft doesn't guarantee this on man page.
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,  // flags
        NULL,            // lpsource
        err,             // message id
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),  // languageid
        msgbuf,          // output buffer
        sizeof(msgbuf),  // size of msgbuf, bytes
        NULL);           // va_list of arguments
    // strip new line
    char *c = strrchr(msgbuf, '\r');
    if (c) *c = '\0';
    // provide error # if no string available
    if (! *msgbuf)
        sprintf (msgbuf, "%d", err);
    return msgbuf;
#else
    return strerror(err);
#endif
}
