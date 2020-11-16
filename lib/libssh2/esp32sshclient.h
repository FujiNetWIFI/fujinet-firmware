/**************************************************************************************************/
// Project: esp32sshclient
// File: esp32sshclient.h
// Description: Simple abstraction layer for create and use a SSH client from libssh2.
// Created on: 16 jun. 2019
// Last modified date: 29 jun. 2019
// Version: 0.0.1
/**************************************************************************************************/

/* Include Guard */

#ifndef ESP32SSHCLIENT_H_
#define ESP32SSHCLIENT_H_

/**************************************************************************************************/

/* Libraries */
#include "utility/libssh2/libssh2_config.h"
#include "utility/libssh2/libssh2.h"
#include "utility/libssh2/libssh2_sftp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

/**************************************************************************************************/

/* Constants */

#define MAX_SSH_CMD_RESPONSE_LENGTH 4096

#define TIMEOUT_CMD_TX 5000 // Max wait 5s for send a command
#define TIMEOUT_CMD_RX 20000 // Max wait 20s for wait command response
#define TIMEOUT_HANDSHAKE 10000 // Max wait 10s for SSH session handshake
#define TIMEOUT_AUTH_PASS 10000 // Max wait 10s user-pass auth login
#define TIMEOUT_AUTH_PUBKEY 10000 // Max wait 10s publickey auth login
#define TIMEOUT_SESSION_OPEN 5000 // Max wait 5s for SSH session open
#define TIMEOUT_CHANNEL_CLOSE 5000 // Max wait 5s for SSH channel close

/**************************************************************************************************/

class ESP32SSHCLIENT
{
    public:
        ESP32SSHCLIENT(void);
        ~ESP32SSHCLIENT(void);

        int8_t connect(const char* host, const uint16_t port, const char* user, const char* pass);
        int8_t connect(const char* host, const uint16_t port, const char* user, uint8_t* pubkey, 
                size_t pubkeylen, uint8_t* privkey, size_t privkeylen, const char* key_passphrase);
        int8_t connect(const char* host, const uint16_t port, const char* user, const char* pubkey, 
                size_t pubkeylen, const char* privkey, size_t privkeylen, const char* 
                key_passphrase);
        int8_t disconnect(void);
        int8_t is_connected(void);
        int8_t send_cmd(const char* cmd);

    private:
        // Private Attributtes
        struct sockaddr_in sin;
        LIBSSH2_SESSION* session;
        LIBSSH2_CHANNEL* channel;
        int sock, rc, connected;
        char response[MAX_SSH_CMD_RESPONSE_LENGTH];

        // Private Methods
        int8_t init_libssh2(void);
        int8_t socket_connect(const char* host, const uint16_t port);
        int8_t session_create(void);
        int8_t session_open(void);
        int8_t lets_handshake(void);
        int8_t auth_pass(const char* user, const char* pass);
        int8_t auth_publickey(const char* user, const char* passphrase, 
                const char* pubkey, size_t pubkeylen, const char* privkey, size_t privkeylen);
        int8_t channel_close(void);
        void show_server_fingerprint(void);
        void system_reboot(void);
};

/**************************************************************************************************/

#endif
