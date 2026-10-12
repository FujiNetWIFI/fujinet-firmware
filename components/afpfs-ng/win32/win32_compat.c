#ifdef _WIN32

#include "win32_compat.h"

#include <windows.h>

#include "../lib/afp_socket.h"

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }

/* afp_server_init() copies the result without a NULL check. */
struct passwd *getpwuid(uid_t uid)
{
    static char name[] = "fujinet";
    static char empty[] = "";
    static struct passwd pw = { name, empty, 0, 0, empty, empty, empty };
    (void)uid;
    return &pw;
}

static int no_sockets(const fd_set *set)
{
    return set == NULL || set->fd_count == 0;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const void *sigmask)
{
    struct timeval tv, *tvp = NULL;
    int ret;
    (void)sigmask;

    if (timeout) {
        tv.tv_sec = (long)timeout->tv_sec;
        tv.tv_usec = (long)(timeout->tv_nsec / 1000);
        tvp = &tv;
    }

    /* Winsock's select() fails at once when every set is empty, which would
     * spin the event loop before the first connection.  Wait instead. */
    if (no_sockets(readfds) && no_sockets(writefds) && no_sockets(exceptfds)) {
        if (timeout)
            Sleep((DWORD)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000));
        return 0;
    }

    ret = select(nfds, readfds, writefds, exceptfds, tvp);
    if (ret < 0)
        errno = afp_sock_errno();
    return ret;
}

#endif /* _WIN32 */
