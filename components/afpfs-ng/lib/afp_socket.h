#ifndef __AFP_SOCKET_H_
#define __AFP_SOCKET_H_

#include <errno.h>
#include <stddef.h>

#ifdef _WIN32

/* On Windows a socket is not a file descriptor, and Winsock reports errors
 * outside errno, which is where the library looks. */
#include <winsock2.h>

static inline int afp_sock_errno(void)
{
	switch (WSAGetLastError()) {
	case WSAECONNREFUSED: return ECONNREFUSED;
	case WSAETIMEDOUT: return ETIMEDOUT;
	case WSAECONNRESET:
	case WSAECONNABORTED:
	case WSAESHUTDOWN: return EPIPE;
	case WSAENOTSOCK:
	case WSAEBADF: return EBADF;
	case WSAEINTR: return EINTR;
	default: return EIO;
	}
}

static inline int afp_sock_read(int fd, void * buf, size_t len)
{
	int ret=recv(fd,(char *) buf,(int) len,0);
	if (ret<0) errno=afp_sock_errno();
	return ret;
}

static inline int afp_sock_write(int fd, const void * buf, size_t len)
{
	int ret=send(fd,(const char *) buf,(int) len,0);
	if (ret<0) errno=afp_sock_errno();
	return ret;
}

static inline void afp_sock_prepare(int fd)
{
	(void) fd;
}

#define afp_sock_close(fd) closesocket(fd)

#else

#include <sys/socket.h>
#include <unistd.h>

#define afp_sock_errno() (errno)
#define afp_sock_read(fd,buf,len) read((fd),(buf),(len))
#define afp_sock_close(fd) close(fd)

#ifdef ESP_PLATFORM
#define afp_sock_write(fd,buf,len) write((fd),(buf),(len))
#else
/* A server that hangs up must fail the write, not raise SIGPIPE in the host. */
#ifdef MSG_NOSIGNAL
#define afp_sock_write(fd,buf,len) send((fd),(buf),(len),MSG_NOSIGNAL)
#else
#define afp_sock_write(fd,buf,len) send((fd),(buf),(len),0)
#endif
#endif

/* Where send() has no MSG_NOSIGNAL, the same is asked for per socket. */
static inline void afp_sock_prepare(int fd)
{
#ifdef SO_NOSIGPIPE
	int on=1;
	setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#else
	(void) fd;
#endif
}

#endif

#endif
