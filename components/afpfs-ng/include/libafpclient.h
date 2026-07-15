
#ifndef __CLIENT_H_
#define __CLIENT_H_

#include <unistd.h>
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
/* Define syslog constants when syslog.h is not available */
#define LOG_EMERG       0
#define LOG_ALERT       1
#define LOG_CRIT        2
#define LOG_ERR         3
#define LOG_WARNING     4
#define LOG_NOTICE      5
#define LOG_INFO        6
#define LOG_DEBUG       7
#endif

#define MAX_CLIENT_RESPONSE 2048


enum loglevels {
        AFPFSD,
};

struct afp_server;
struct afp_volume;

struct libafpclient {
        int (*unmount_volume) (struct afp_volume * volume);
	void (*log_for_client)(void * priv,
        	enum loglevels loglevel, int logtype, const char *message);
	void (*forced_ending_hook)(void);
	int (*scan_extra_fds)(int command_fd,fd_set *set, int * max_fd);
	void (*loop_started)(void);
} ;

extern struct libafpclient * libafpclient;

void libafpclient_register(struct libafpclient * tmpclient);


void signal_main_thread(void);

/* These are logging functions */

#define MAXLOGSIZE 2048

#define LOG_METHOD_SYSLOG 1
#define LOG_METHOD_STDOUT 2

void set_log_method(int m);


void log_for_client(void * priv,
        enum loglevels loglevel, int logtype, char * message,...);

void stdout_log_for_client(void * priv,
	enum loglevels loglevel, int logtype, const char *message);

#endif
