#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "fujitcp.h"
#include "fujimail.h"
#include "fuji_mailbox.h"

/* Must hold a SLIP-encoded 512-byte DBC push frame; undersizing this
 * silently truncates every ROM push (the 1088 trap, paid for once on the
 * Intellivision). */
#define RX_RAW_MAX 1088

static int fd = -1;

bool fujitcp_active(void)
{
    return fd >= 0;
}

int fujitcp_init(const char *hostport)
{
    char host[256], *colon;
    int port = 9995, one = 1;
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];

    if (hostport == NULL)
        hostport = getenv("FUJINET_TCP");
    if (hostport == NULL)
        hostport = "127.0.0.1:9995";
    snprintf(host, sizeof host, "%s", hostport);
    colon = strrchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }
    if (host[0] == '\0')
        snprintf(host, sizeof host, "127.0.0.1");
    snprintf(portstr, sizeof portstr, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) {
        fprintf(stderr, "fujinet: cannot resolve %s:%d\n", host, port);
        return -1;
    }
    /* Walk every result: "localhost" usually resolves to ::1 first, while
     * fujinet-pc's BoIP listener binds 127.0.0.1 only. */
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    if (fd < 0) {
        fprintf(stderr, "fujinet: cannot connect to %s:%d (%s)\n",
                host, port, strerror(errno));
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fprintf(stderr, "fujinet: connected to %s:%d\n", host, port);
    return 0;
}

void fujitcp_close(void)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

/* Read one complete SLIP frame (two 0xC0 delimiters) or time out. */
static fb_status_t read_frame(uint8_t *buf, size_t cap, size_t *out_len,
                              int secs)
{
    size_t n = 0;
    int ends = 0;
    struct timeval deadline, now;

    gettimeofday(&deadline, NULL);
    deadline.tv_sec += secs;

    for (;;) {
        fd_set rf;
        struct timeval tv;
        long remain;

        gettimeofday(&now, NULL);
        remain = (deadline.tv_sec - now.tv_sec) * 1000000L
               + (deadline.tv_usec - now.tv_usec);
        if (remain <= 0)
            return FB_ETIMEOUT;
        tv.tv_sec = remain / 1000000L;
        tv.tv_usec = remain % 1000000L;

        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0)
            return FB_ETIMEOUT;

        while (n < cap) {
            unsigned char c;
            ssize_t r = recv(fd, &c, 1, MSG_DONTWAIT);

            if (r <= 0)
                break;
            buf[n++] = c;
            if (c == 0xC0 && ++ends == 2) {
                *out_len = n;
                return FB_OK;
            }
        }
        if (n >= cap)
            return FB_ETOOBIG;
    }
}

fb_status_t fujitcp_transact(uint8_t device, uint8_t command,
                             const fb_param_t *params, unsigned nparams,
                             const uint8_t *payload, uint16_t payload_len,
                             uint32_t timeout_ms, fb_reply_t *reply)
{
    static uint8_t req[FN_TX_MAX + 64];
    static uint8_t raw[RX_RAW_MAX];
    size_t reqlen, rawlen;
    fb_status_t st;
    int secs = (int)((timeout_ms + 999) / 1000);

    if (!fujitcp_active())
        return FB_ENOLINK;

    reqlen = fujibus_build_request(device, command, params, nparams,
                                   payload, payload_len, req, sizeof req);
    if (reqlen == 0)
        return FB_ETOOBIG;
    if (send(fd, req, reqlen, MSG_NOSIGNAL) != (ssize_t) reqlen)
        return FB_ENOLINK;

    for (;;) {
        st = read_frame(raw, sizeof raw, &rawlen, secs);
        if (st != FB_OK)
            return st;
        if (!fujibus_parse_reply(raw, rawlen, reply))
            return FB_EBADFRAME;
        /* Push frames arrive interleaved with the reply we are waiting for;
         * consuming one proves the link is alive, so the deadline restarts
         * rather than counting down. */
        if (!fujimail_inbound(reply))
            return FB_OK;
    }
}

void fujitcp_send_bare(uint8_t device, uint8_t command,
                       const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[64];
    size_t n = fujibus_build_request(device, command, NULL, 0,
                                     payload, payload_len, frame,
                                     sizeof frame);

    if (n && fd >= 0)
        send(fd, frame, n, MSG_NOSIGNAL);
}
