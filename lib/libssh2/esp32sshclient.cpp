/**************************************************************************************************/
// Project: esp32sshclient
// File: esp32sshclient.h
// Description: Simple abstraction layer for create and use a SSH client from libssh2.
// Created on: 16 jun. 2019
// Last modified date: 15 Nov 2020
// Version: 0.0.2
/**************************************************************************************************/

/* Libraries */

#include "../../../../include/debug.h"
#include "esp32sshclient.h"

/**************************************************************************************************/

/* Macros */

#define _millis() (unsigned long)(esp_timer_get_time()/1000)
#define _delay(x) vTaskDelay(x/portTICK_PERIOD_MS);

/**************************************************************************************************/

static int socket_wait(int socket_fd, LIBSSH2_SESSION *session);

/**************************************************************************************************/

/* Constructor & Destructor */

// SSH Client constructor
ESP32SSHCLIENT::ESP32SSHCLIENT(void)
{
    session = NULL;
    channel = NULL;
    sock = -1;
    rc = -1;
    connected = 0;
    memset(response, '\0', MAX_SSH_CMD_RESPONSE_LENGTH);

    init_libssh2();
}

// SSH Client destructor
ESP32SSHCLIENT::~ESP32SSHCLIENT(void)
{
    if(connected)
    {
        libssh2_channel_free(channel);
        channel = NULL;
        libssh2_session_disconnect(session, "Iteration end, releasing ssh resources...");
        libssh2_session_free(session);
        session = NULL;
        close(sock);
    }
    libssh2_exit();
}

/**************************************************************************************************/

/* Public Methods */

// Get SSH connection status
int8_t ESP32SSHCLIENT::is_connected(void)
{
    return connected;
}

// Connect to server by password
int8_t ESP32SSHCLIENT::connect(const char* host, const uint16_t port, const char* user, 
        const char* pass)
{
    if(!connected)
    {
        Debug_printf("Connecting to Server \"%s:%d\"...\n", host, port);
        if(socket_connect(host, port) == -1)
            return -1;
        session_create();
        lets_handshake();
        auth_pass(user, pass);
        Debug_printf("Connected to SSH Server.\n");
        connected = 1;
    }
    return connected;
}

// Connect to server by public key certificate (uint8_t* args)
int8_t ESP32SSHCLIENT::connect(const char* host, const uint16_t port, const char* user, 
        uint8_t* pubkey, size_t pubkeylen, uint8_t* privkey, size_t privkeylen, 
        const char* key_passphrase)
{
    if(!connected)
    {
        Debug_printf("Connecting to Server \"%s:%d\"...\n", host, port);
        if(socket_connect(host, port) == -1)
            return -1;
        session_create();
        lets_handshake();
        auth_publickey(user, key_passphrase, (const char*)pubkey, pubkeylen, (const char*)privkey, 
                privkeylen);
        connected = 1;
        Debug_printf("Connected to SSH Server.\n");
    }
    return connected;
}

// Connect to server by public key certificate (const char* args)
int8_t ESP32SSHCLIENT::connect(const char* host, const uint16_t port, const char* user, 
        const char* pubkey, size_t pubkeylen, const char* privkey, size_t privkeylen, 
        const char* key_passphrase)
{
    if(!connected)
    {
        Debug_printf("Connecting to Server \"%s:%d\"...\n", host, port);
        if(socket_connect(host, port) == -1)
            return -1;
        session_create();
        lets_handshake();
        auth_publickey(user, key_passphrase, pubkey, pubkeylen, privkey, privkeylen);
        connected = 1;
        Debug_printf("Connected to SSH Server.\n");
    }
    return connected;
}

// Disconnect from server and close and release SSH session and socket
int8_t ESP32SSHCLIENT::disconnect(void)
{
    if(connected)
    {
        channel_close();
        libssh2_channel_free(channel);
        channel = NULL;
        libssh2_session_disconnect(session, "Iteration end, releasing ssh resources...");
        libssh2_session_free(session);
        session = NULL;
        close(sock);
        connected = 0;
        Debug_printf("Disconnected from SSH Server.\n");
    }
    return !connected;
}

int8_t ESP32SSHCLIENT::send_cmd(const char* cmd)
{
    int bytecount = 0;
    unsigned long t0, t1;

    if(!connected)
        return -1;

    Debug_printf("Executing command in SSH Server:\n%s\n", cmd);
    session_open();
    rc = libssh2_channel_exec(channel, cmd);
    t0 = _millis();
    while(rc == LIBSSH2_ERROR_EAGAIN)
    {
        ::socket_wait(sock, session);
        rc = libssh2_channel_exec(channel, cmd);

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_CMD_TX)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(rc != 0) {
        Debug_printf("Error: Command send fail (%d).\n", rc);
        disconnect();
        return -1;
    }

    // Wait response
    memset(response, '\0', MAX_SSH_CMD_RESPONSE_LENGTH);
    t0 = _millis();
    rc = 255;
    while(rc != 0)
    {
        if(bytecount >= MAX_SSH_CMD_RESPONSE_LENGTH)
            break;
        
        rc = libssh2_channel_read(channel, response, sizeof(response));
        if(rc > 0)
        {
            bytecount += rc;
            // Show in real time each received byte
            /*for(int i = 0; i < rc; ++i)
                fputc(response[i], stderr);
            Debug_printf("\n");*/
        }
        else
        {
            if((rc != 0) && (rc != LIBSSH2_ERROR_EAGAIN))
            {
                Debug_printf("Error: Read command response fail (%d).\n", rc);
                disconnect();
                return -1;
            }

            _delay(10);
        }

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_CMD_RX)
        {
            rc = -125; // -125 == timeout
            break;
        }
    }
    response[MAX_SSH_CMD_RESPONSE_LENGTH-1] = '\0';
    if(bytecount < MAX_SSH_CMD_RESPONSE_LENGTH)
        response[bytecount] = '\0';
    Debug_printf("Response:\n%s\n", response);

    channel_close();
    libssh2_channel_free(channel);
    channel = NULL;

    return rc;
}

/**************************************************************************************************/

/* Private Methods */

// Initialize libssh2
int8_t ESP32SSHCLIENT::init_libssh2(void)
{
    int rc = libssh2_init(0);
    if(rc != 0)
    {
        Debug_printf("Error: libssh2 initialization failed (%d)\n", rc);
        system_reboot();
    }
    Debug_printf("SSH client initialized.\n");

    return rc;
}

// Initialize socket and connect to host with it
int8_t ESP32SSHCLIENT::socket_connect(const char* host, const uint16_t port)
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);

    rc = ::connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in));
    if(rc != 0)
    {
        Debug_printf("Error: Connection fail (%d).\n", rc);
        return -1;
    }
    Debug_printf("SSH host connection established.\n");
    return rc;
}

// Create SSH session
int8_t ESP32SSHCLIENT::session_create(void)
{
    // Create a session instance and start it up. This will trade welcome banners, 
    // exchange keys, and setup crypto, compression, and MAC layers
    session = libssh2_session_init();
    if(!session)
    {
        Debug_printf("failed to create session!\n");
        system_reboot();
    }
    Debug_printf("SSH session created.\n");

    // Set non-blocking communication
    libssh2_session_set_blocking(session, 0);

    return 0;
}

// Start session handshake
int8_t ESP32SSHCLIENT::lets_handshake(void)
{
    unsigned long t0, t1;

    rc = libssh2_session_handshake(session, sock);

    t0 = _millis();
    while(rc == LIBSSH2_ERROR_EAGAIN)
    {
        rc = libssh2_session_handshake(session, sock);

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_HANDSHAKE)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(rc != 0)
    {
        Debug_printf("Failure establishing SSH session: %d\n", rc);
        system_reboot();
    }
    Debug_printf("SSH host session handshake success.\n");
    
    return rc;
}

// SSH authentication using password
int8_t ESP32SSHCLIENT::auth_pass(const char* user, const char* pass)
{
    unsigned long t0, t1;

    rc = libssh2_userauth_password(session, user, pass);

    t0 = _millis();
    while(rc == LIBSSH2_ERROR_EAGAIN)
    {
        rc = libssh2_userauth_password(session, user, pass);

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_AUTH_PASS)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(rc != 0)
    {
        Debug_printf("  Authentication by password failed (%d)!\n", rc);
        system_reboot();
    }
    Debug_printf("  Authentication by password succeeded.\n");

    return rc;
}

// SSH authentication using publickey
int8_t ESP32SSHCLIENT::auth_publickey(const char* user, const char* passphrase, 
        const char* pubkey, size_t pubkeylen, const char* privkey, size_t privkeylen)
{
    unsigned long t0, t1;

    rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), pubkey, pubkeylen, 
            privkey, privkeylen, passphrase);
    
    t0 = _millis();
    while(rc == LIBSSH2_ERROR_EAGAIN)
    {
        rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), pubkey, pubkeylen, 
            privkey, privkeylen, passphrase);
        
        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_AUTH_PUBKEY)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(rc != 0)
    {
        Debug_printf("  Authentication by public key failed (%d)!\n", rc);
        system_reboot();
    }
    Debug_printf("  Authentication by public key succeeded.\n");

    return rc;
}

int8_t ESP32SSHCLIENT::session_open(void)
{
    unsigned long t0, t1;

    /* Exec non-blocking on the remove host */
    t0 = _millis();
    while((channel = libssh2_channel_open_session(session)) == NULL && 
            libssh2_session_last_error(session, NULL, NULL, 0) == LIBSSH2_ERROR_EAGAIN)
    {
        ::socket_wait(sock, session);

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_SESSION_OPEN)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(channel == NULL)
    {
        Debug_printf("Error: SSH client session open fail.\n");
        system_reboot();
        return -1;
    }

    return 0;
}

int8_t ESP32SSHCLIENT::channel_close(void)
{
    unsigned long t0, t1;
    char* exitsignal = (char*)"none\0";
    int exitcode;

    exitcode = 127;
    rc = libssh2_channel_close(channel);

    t0 = _millis();
    while(rc == LIBSSH2_ERROR_EAGAIN)
    {
        ::socket_wait(sock, session);
        rc = libssh2_channel_close(channel);

        t1 = _millis();
        if(t1 < t0)
            t1 = t0;
        if(t1 - t0 > TIMEOUT_CHANNEL_CLOSE)
        {
            rc = -125; // -125 == timeout
            break;
        }

        _delay(100);
    }
    if(rc == 0)
    {
        exitcode = libssh2_channel_get_exit_status(channel);
        libssh2_channel_get_exit_signal(channel, &exitsignal, NULL, NULL, NULL, NULL, NULL);
    }
    if(exitsignal)
    {
        Debug_printf("Exit signal: %s\n", exitsignal);
        return -1;
    }
    else
    {
        Debug_printf("Exit code: %d\n", exitcode);
        return 0;
    }
}

// Wait for socket
static int socket_wait(int socket_fd, LIBSSH2_SESSION *session)
{
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    FD_ZERO(&fd);

    FD_SET(socket_fd, &fd);

    /* now make sure we wait in the correct direction */
    dir = libssh2_session_block_directions(session);

    if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        readfd = &fd;

    if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        writefd = &fd;

    rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

    return rc;
}

// Get and show host fingerprint
void ESP32SSHCLIENT::show_server_fingerprint(void)
{
    const char* fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
    Debug_printf("Server Fingerprint: ");
    for(int i = 0; i < 20; i++)
        Debug_printf("%02X ", (unsigned char)fingerprint[i]);
    Debug_printf("\n");
}

// Softreboot device
void ESP32SSHCLIENT::system_reboot(void)
{
    Debug_printf("Rebooting system now.\n\n");
    fflush(stdout);
    esp_restart();
}
