#ifndef __AFP_SOCKET_H_
#define __AFP_SOCKET_H_

/* On Windows a socket is not a file descriptor. */
#ifdef _WIN32
#include <winsock2.h>
#define afp_sock_read(fd,buf,len) recv((fd),(char *)(buf),(int)(len),0)
#define afp_sock_write(fd,buf,len) send((fd),(const char *)(buf),(int)(len),0)
#define afp_sock_close(fd) closesocket(fd)
#else
#include <unistd.h>
#define afp_sock_read(fd,buf,len) read((fd),(buf),(len))
#define afp_sock_write(fd,buf,len) write((fd),(buf),(len))
#define afp_sock_close(fd) close(fd)
#endif

#endif
