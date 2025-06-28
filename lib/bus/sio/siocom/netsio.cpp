#ifndef ESP_PLATFORM

#ifdef BUILD_ATARI

#include "netsio.h"
#include "netsio_proto.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "compat_string.h"
#include <sys/time.h>
#include <unistd.h> // write(), read(), close()
#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"


/* alive response timeout in seconds
 *  device sends in regular intervals (2 s) alive messages (NETSIO_ALIVE_REQUEST) to NetSIO HUB
 *  if the device will not receive alive response (NETSIO_ALIVE_RESPONSE) within ALIVE_TIMEOUT period
 *  the connection to the HUB is considered as expired/broken and new connection attempt will
 *  be made
 */
#define ALIVE_RATE_MS       1000
#define ALIVE_TIMEOUT_MS    5000
// While debugging:
// #define ALIVE_RATE_MS       200000
// #define ALIVE_TIMEOUT_MS    600000

// Constructor
NetSioPort::NetSioPort() :
    _host{0},
    _ip(IPADDR_NONE),
    _port(NETSIO_PORT),
    _baud(SIOPORT_DEFAULT_BAUD),
    _baud_peer(SIOPORT_DEFAULT_BAUD),
    _fd(-1),
    _initialized(false),
    _command_asserted(false),
    _motor_asserted(false),
    _rxhead(0),
    _rxtail(0),
    _rxfull(false),
    _sync_request_num(-1),
    _sync_write_size(-1),
    _errcount(0),
    _credit(3)
{}

NetSioPort::~NetSioPort()
{
    end();
}

void NetSioPort::begin(int baud)
{
    if (_initialized) 
    {
        end();
    }

    _baud = baud;
    _resume_time = 0;

    _command_asserted = false;
    _motor_asserted = false;
    rxbuffer_flush();

    // Wait for WiFi
    int suspend_ms = _errcount < 5 ? 400 : 2000;
    if (!fnWiFi.connected())
    {
        Debug_println("NetSIO: No WiFi!");
        _errcount++;
        suspend(suspend_ms);
		return;
	}

    //
    // Connect to hub
    //

    suspend_ms = _errcount < 5 ? 1000 : 5000;
    Debug_printf("Setting up NetSIO (%s:%d)\n", _host, _port);
    _fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (_fd < 0)
    {
        Debug_printf("Failed to create NetSIO socket: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
		return;
	}
    
    _ip = get_ip4_addr_by_name(_host);
    if (_ip == IPADDR_NONE)
    {
        Debug_println("Failed to resolve NetSIO host name");
        _errcount++;
        suspend(suspend_ms);
		return;
    }

    // Set remote IP address (no real connection is created for UDP socket)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = _ip;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    if (connect(_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        // should not happen (UDP)
        Debug_printf("Failed to connect NetSIO socket: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
		return;
    }

#if defined(_WIN32)
    unsigned long on = 1;
    ioctlsocket(_fd, FIONBIO, &on);
#else
    fcntl(_fd, F_SETFL, O_NONBLOCK);
#endif

    // Fast ping hub
    if (ping(2, 50, 50) < 0)
    {
        _errcount++;
        suspend(suspend_ms);
        return;
    }

    // Connect device
    uint8_t connect = NETSIO_DEVICE_CONNECT;
    send(_fd, (char *)&connect, 1, 0);

    _alive_request = _alive_time = fnSystem.millis();

    Debug_printf("### NetSIO initialized ###\n");
    // Set initialized.
    _initialized = true;
    _errcount = 0;
    set_baudrate(baud);
}

void NetSioPort::end()
{
    if (_fd >= 0)
    {
        uint8_t disconnect = NETSIO_DEVICE_DISCONNECT;
        send(_fd, (char *)&disconnect, 1, 0);
        closesocket(_fd);
        _fd  = -1;
        fnSystem.delay(50); // wait a while, otherwise wifi may turn off too quickly (during shutdown)
        Debug_printf("### NetSIO stopped ###\n");
    }
    _initialized = false;
}

bool NetSioPort::poll(int ms)
{
    if (_initialized)
    {
        if (handle_netsio() > 0)
            return true;
        return (wait_sock_readable(ms));
    }
    fnSystem.delay(ms);
    return false;
}

void NetSioPort::suspend(int ms)
{
    Debug_printf("Suspending NetSIO for %d ms\n", ms);
    _resume_time = fnSystem.millis() + ms;
    end();
}

int NetSioPort::ping(int count, int interval_ms, int timeout_ms, bool fast)
{
    uint8_t ping;
    uint64_t t1, t2;
    int wait_ms;
    ssize_t result;
    int rtt_sum = 0;
    int ok_count =0;
    int rtt;

    for (int i = 0; i < count; i++)
    {
        // perform ping
        rtt = -1;
        if (wait_sock_writable(timeout_ms))
        {
            ping = NETSIO_PING_REQUEST;
            result = send(_fd, (char *)&ping, 1, 0);
            t1 = fnSystem.micros();
            do 
            {
                wait_ms = timeout_ms - (fnSystem.micros() - t1) / 1000;
                if (result == 1 && wait_sock_readable(wait_ms))
                {
                    t2 = fnSystem.micros();
                    result = recv(_fd, (char *)&ping, 1, 0);
                    if (result == 1 && ping == NETSIO_PING_RESPONSE) 
                        rtt = (int)(t2 - t1);
                }
                wait_ms = timeout_ms - (fnSystem.micros() - t1) / 1000;
            } while (rtt < 0 && wait_ms > 0);
        }

        if (rtt >= 0)
        {
            Debug_printf("NetSIO ping %s time=%.3f ms\n", _host, (double)(rtt)/1000.0);
            rtt_sum += rtt;
            ok_count++;
            if (fast)
                break;
            if (i+1 < count)
                fnSystem.delay(interval_ms);
        }
        else
        {
            Debug_printf("NetSIO ping %s timeout\n", _host);
            if (i+1 < count && interval_ms - timeout_ms > 0)
                fnSystem.delay(interval_ms - timeout_ms);
        }
    }

    return ok_count ? rtt_sum / ok_count : -1;
}

bool NetSioPort::rxbuffer_empty()
{
    return (_rxhead == _rxtail && !_rxfull);
}

bool NetSioPort::rxbuffer_put(uint8_t b) 
{
    _rxbuf[_rxhead++] = b;
    _rxhead %= sizeof(_rxbuf);
    if (_rxfull) {
        // tail byte was overwritten / lost
        _rxtail = _rxhead;
        return true;
    }
    _rxfull = (_rxhead == _rxtail);
    return false;
}

int NetSioPort::rxbuffer_get() 
{
    int b;
    if (rxbuffer_empty())
        return -1;
    b = _rxbuf[_rxtail++];
    _rxtail %= sizeof(_rxbuf);
    _rxfull = false;
    return b;
}

int  NetSioPort::rxbuffer_available() 
{
    int avail = _rxhead - _rxtail;
    if ((avail < 0) || (avail == 0 && _rxfull))
        avail += sizeof(_rxbuf);
    return avail;
}

void NetSioPort::rxbuffer_flush() 
{
    _rxtail = _rxhead;
    _rxfull = false;
}

bool NetSioPort::resume_test()
{
    if (!_initialized)
    {
        // suspended ?
        if (_resume_time != 0)
        {
            if (_resume_time > fnSystem.millis())
                return false;
            // time to resume
            begin(_baud);
        }
    }
    return _initialized;
}

bool NetSioPort::keep_alive()
{
    ssize_t result;
    uint64_t ms = fnSystem.millis();

    // if ALIVE_RATE_MS time passed since last alive request was sent
    if (ms - _alive_request >= ALIVE_RATE_MS)
    {
        // if alive request was send but we did not received any response then we lost connection
        if (ms - _alive_request < ALIVE_TIMEOUT_MS && ms - _alive_time >= ALIVE_TIMEOUT_MS)
        {
            Debug_println("NetSIO connection lost");
            // Debug_printf("> %lu %lu %lu  %lu %lu\n", ms, _alive_request, _alive_time, ms-_alive_request, ms-_alive_time);
            // ping hub
            if (ping(2, 1000, 2000) < 0)
            {
                // no ping response
                suspend(5000);
                return false;
            }
            // reconnect on ping response
            end();
            begin(_baud);
        }
        // if nothing received for longer than ALIVE_RATE_MS, keep sending alive requests at ALIVE_RATE_MS rate
        else if (ms - _alive_time >= ALIVE_RATE_MS)
        {
            _alive_request = ms;
            uint8_t alive = NETSIO_ALIVE_REQUEST;
            result = send(_fd, (char *)&alive, 1, 0);
            // Debug_printf("Alive %lu %ld\n", ms, result);
        }
    }
    return _initialized;
}

/* read NetSIO message from socket and update internal variables */
int NetSioPort::handle_netsio()
{
    uint8_t rxbuf[514]; // must be able to hold whole netsio datagram, i.e. >= rxbuffer_len+2 defined in netsio.atdevice
    uint8_t b;
    int received;

    if (!resume_test())
        return 0;

    received = recv(_fd, (char *)rxbuf, sizeof(rxbuf), 0);
    if (received > 0)
    {
#ifdef VERBOSE_SIO
        Debug_printf("NetSIO RECV <%i> BYTES\n\t", received);
        for (int i = 0; i < received; i++)
            Debug_printf("%02x ", rxbuf[i]);
        Debug_print("\n");
#endif
        _alive_time = fnSystem.millis(); // update last received
        switch (rxbuf[0])
        {
            case NETSIO_DATA_BYTE_SYNC:
                if (received >= 3)
                    _sync_request_num = rxbuf[2];
                // [[fallthrough]]; // > No warning

            case NETSIO_DATA_BYTE:
                b = rxbuf[1];
                if (_baud_peer < _baud * 90 / 100 || _baud_peer > _baud * 110 / 100)
                    b ^= (uint8_t)_baud_peer ^ (uint8_t)_baud; // corrupt byte
                if (rxbuffer_put(b))
                    Debug_println("NetSIO rxbuffer overrun");
                break;

            case NETSIO_DATA_BLOCK:
                if (received >= 2)
                {
                    for (int i = 1; i < received-1; i++) // TODO received-1, to test packet SNs
                    // for (int i = 1; i < received; i++)
                    {
                        b = rxbuf[i];
                        if (_baud_peer < _baud * 90 / 100 || _baud_peer > _baud * 110 / 100)
                            b ^= (uint8_t)_baud_peer ^ (uint8_t)_baud; // corrupt byte
                        if (rxbuffer_put(b))
                            Debug_println("NetSIO rxbuffer overrun");
                    }
                }
                break;

            case NETSIO_COMMAND_OFF_SYNC:
                if (received >= 2) 
                    _sync_request_num = rxbuf[1]; // sync request sequence number
                // [[fallthrough]]; // > No warning

            case NETSIO_COMMAND_OFF:
                _command_asserted = false;
                break;

            case NETSIO_COMMAND_ON:
                _command_asserted = true;
                _sync_request_num = -1; // cancel any sync request
                _sync_write_size = -1;
                rxbuffer_flush();   // flush any stray input data
                break;

            case NETSIO_MOTOR_OFF:
                _motor_asserted = false;
                break;

            case NETSIO_MOTOR_ON:
                _motor_asserted = true;
                break;

            case NETSIO_SPEED_CHANGE:
                // speed change notification
                if (received >= 5)
                {
                    _baud_peer = rxbuf[1] | (rxbuf[2] << 8) | (rxbuf[3] << 16) | (rxbuf[4] << 24);
                    Debug_printf("NetSIO peer baudrate: %d\n", _baud_peer);
                }
                break;

            case NETSIO_CREDIT_UPDATE:
                _credit = rxbuf[1];
                break;

            case NETSIO_COLD_RESET:
                // emulator cold reset, do fujinet restart
#ifndef DEBUG_NO_REBOOT
                fnSystem.reboot();
#endif
                break;

            default:
                break;
        }
    }

    keep_alive();

    return received;
}

timeval NetSioPort::timeval_from_ms(const uint32_t millis)
{
  timeval tv;
  tv.tv_sec = millis / 1000;
  tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
  return tv;
}

bool NetSioPort::wait_sock_readable(uint32_t timeout_ms)
{
    timeval timeout_tv;
    fd_set readfds;
    int result;
    
    for(;;)
    {
        // Setup a select call to block for socket data or a timeout
        timeout_tv = timeval_from_ms(timeout_ms);
        FD_ZERO(&readfds);
        FD_SET(_fd, &readfds);
        result = select(_fd + 1, &readfds, nullptr, nullptr, &timeout_tv);

        // select error
        if (result < 0)
        {
            int err = compat_getsockerr();
#if defined(_WIN32)
            if (err == WSAEINTR)
#else
            if (err == EINTR) 
#endif
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("NetSIO wait_sock_readable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(_fd, &readfds))
        {
            Debug_println("NetSIO wait_sock_readable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

bool NetSioPort::wait_sock_writable(uint32_t timeout_ms)
{
    timeval timeout_tv;
    fd_set writefds;
    int result;

    for(;;)
    {
        timeout_tv = timeval_from_ms(timeout_ms);
        FD_ZERO(&writefds);
        FD_SET(_fd, &writefds);
        result = select(_fd + 1, nullptr, &writefds, nullptr, &timeout_tv);

        // select error
        if (result < 0) 
        {
            int err = compat_getsockerr();
#if defined(_WIN32)
            if (err == WSAEINTR)
#else
            if (err == EINTR) 
#endif
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("NetSIO wait_sock_writable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(_fd, &writefds)) 
        {
            Debug_println("NetSIO wait_sock_writable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

ssize_t NetSioPort::write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    if (!wait_sock_writable(timeout_ms))
    {
        Debug_println("NetSIO write_sock() TIMEOUT");
        return -1;
    }

    ssize_t result = send(_fd, (char *)buffer, size, 0);
    if (result < 0)
    {
        Debug_printf("NetSIO write_sock() send error %d: %s\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
    }
    return result;
}

bool NetSioPort::wait_for_data(uint32_t timeout_ms)
{
    while (rxbuffer_empty())
    {
        if (!wait_sock_readable(timeout_ms))
            return false;  // timeout
        handle_netsio();
        // TODO adjust timeout_ms
    }
    // data available for read
    return true;
}

bool NetSioPort::wait_for_credit(int needed)
{
    uint8_t txbuf[2];
    txbuf[0] = NETSIO_CREDIT_STATUS;
    txbuf[1] = (uint8_t)_credit;

    // wait for credit
    while (needed > _credit)
    {
        if (!_initialized) 
            return false; // disconnected
        // inform HUB we need more credit
        send(_fd, (char *)txbuf, sizeof(txbuf), 0);
        //Debug_printf("waiting for credit %d\n", _credit);
        wait_sock_readable(500);
        handle_netsio();
    }
    // consume credit
    _credit -= needed;
    //Debug_printf("credit %d\n", _credit);
    return true;
}

/* Discards anything in the input buffer
*/
void NetSioPort::flush_input()
{
    if (_initialized)
        rxbuffer_flush();
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void NetSioPort::flush()
{
    if (_initialized)
    {
        flush_input();
        wait_sock_writable(500);
    }
}

/* Returns number of bytes available in receive buffer or -1 on error
*/
int NetSioPort::available()
{
    if (rxbuffer_empty())
        handle_netsio();
    return rxbuffer_available();
}

/* Changes baud rate
*/
void NetSioPort::set_baudrate(uint32_t baud)
{
    Debug_printf("NetSIO set_baudrate: %d\n", baud);

    if (!_initialized)
        return;

    uint8_t txbuf[5];
    txbuf[0] = NETSIO_SPEED_CHANGE;
    txbuf[1] = baud & 0xff;
    txbuf[2] = (baud >> 8) & 0xff;
    txbuf[3] = (baud >> 16) & 0xff;
    txbuf[4] = (baud >> 24) & 0xff;
    wait_for_credit(1);
    send(_fd, (char *)txbuf, sizeof(txbuf), 0);
    _baud = baud;
}

uint32_t NetSioPort::get_baudrate()
{
    return _baud;
}

bool NetSioPort::command_asserted(void)
{
    // process NetSIO message, if any
    handle_netsio();
    return _command_asserted;
}

bool NetSioPort::motor_asserted(void)
{
    handle_netsio();
    return _motor_asserted;
}

void NetSioPort::set_proceed(bool level)
{
    static int last_level = -1; // 0,1 or -1 for unknown
    int new_level = level ? 0 : 1;

    if (!_initialized)
        return;
    if (last_level == new_level)
        return;

    Debug_print(level ? "_" : "-");
    last_level = new_level;

    wait_for_credit(1);
    uint8_t cmd = level ? NETSIO_PROCEED_ON : NETSIO_PROCEED_OFF;
    write_sock(&cmd, 1);
}

void NetSioPort::set_interrupt(bool level)
{
    static int last_level = -1; // 0,1 or -1 for unknown
    int new_level = level ? 0 : 1;

    if (!_initialized)
        return;
    if (last_level == new_level)
        return;

    Debug_print(level ? "\\" : "/");
    last_level = new_level;

    wait_for_credit(1);
    uint8_t cmd = level ? NETSIO_INTERRUPT_ON : NETSIO_INTERRUPT_OFF;
    write_sock(&cmd, 1);
}

void NetSioPort::bus_idle(uint16_t ms)
{
    uint8_t cmd[3];
    cmd[0] = NETSIO_BUS_IDLE;
    cmd[1] = ms & 0xff;
    cmd[2] = (ms >> 8) & 0xff;

    wait_for_credit(1);
    write_sock(cmd, sizeof(cmd));
}


/* Returns a single byte from the incoming stream
*/
int NetSioPort::read(void)
{
    if (!_initialized)
        return -1;

    if (!wait_for_data(500))
    {
        Debug_println("NetSIO read() - TIMEOUT");
        return -1;
    }
    return rxbuffer_get();
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
*  character for ReadBytes(), we override with a single call to uart_read_bytes
*/
size_t NetSioPort::read(uint8_t *buffer, size_t length)
{
    if (!_initialized)
        return 0;

    if (_sync_request_num >= 0 && _sync_write_size >= 0)
    {
        // handle pending sync request
        // send late ACK byte
        send_sync_response(NETSIO_ACK_SYNC, _sync_ack_byte, _sync_write_size);
        // no delay here, emulator is not running
        // 850 us pre-ACK delay will be added by netsio.atdevice
    }

    int result;
    int rxbytes;
    for (rxbytes=0; rxbytes<length;)
    {
        result = read();
        // Debug_printf("read: %d\n", result);
        if (result < 0)
        {
            break;
        }
        else
        {
            buffer[rxbytes++] = (uint8_t)result;
        }

        if (rxbytes == length)
        {
            // done
            break;
        }
    }
    return rxbytes;
}

/* write single byte via NetSIO */
ssize_t NetSioPort::write(uint8_t c)
{
    uint8_t txbuf[2];

    if (!_initialized)
        return 0;

    if (_sync_request_num >= 0)
    {
        // handle pending sync request
        if (_sync_write_size < 0)
        {
            // SYNC RESPONSE
            // send byte (should be ACK/NAK) bundled in sync response
            return send_sync_response(NETSIO_ACK_SYNC, c, 0);
        }
        else
        {
            // sio_late_ack() was not from whatever reason followed by bus_to_peripheral()
            Debug_println("Warn: NetSIO late ACK without bus_to_peripheral");
            // send late ACK byte
            send_sync_response(NETSIO_ACK_SYNC, _sync_ack_byte, _sync_write_size);
        }
    }


    // DATA BYTE

    if (!wait_for_credit(1))
        return 0;

    // send byte as usually
    txbuf[0] = NETSIO_DATA_BYTE; // byte command
    txbuf[1] = c;                // value
    ssize_t result = write_sock(txbuf, sizeof(txbuf));

    return (result > 0) ? 1 : 0; // amount of data bytes written
}

ssize_t NetSioPort::write(const uint8_t *buffer, size_t size)
{
    int result;
    int to_send;
    int txbytes = 0;
    uint8_t txbuf[513];

    if (!_initialized)
        return 0;

    while (txbytes < size)
    {
        // send block
        to_send = ((size-txbytes) > sizeof(txbuf)-1) ? sizeof(txbuf)-1 : (size-txbytes);
        txbuf[0] = NETSIO_DATA_BLOCK;
        memcpy(txbuf+1, buffer+txbytes, to_send);
        // ? calculate credit based on amount of data ?
        if (!wait_for_credit(1))
            break;
        result = write_sock(txbuf, to_send+1);
        if (result > 0)
            txbytes += result-1;
        else if (result < 0)
            break;
    }
    return txbytes;
}

// specific to NetSioPort
void NetSioPort::set_host(const char *host, int port)
{
    if (host != nullptr)
        strlcpy(_host, host, sizeof(_host));
    else
        _host[0] = 0;

    _port = port;
}

const char* NetSioPort::get_host(int &port)
{
    port = _port;
    return _host;
}

void NetSioPort::set_sync_ack_byte(int ack_byte)
{
    _sync_ack_byte = ack_byte;
    _sync_write_size = 0;
}

void NetSioPort::set_sync_write_size(int write_size)
{
    _sync_write_size = write_size;
}

ssize_t NetSioPort::send_sync_response(uint8_t response_type, uint8_t ack_byte, uint16_t sync_write_size)
{
    uint8_t txbuf[6];

    // SYNC RESPONSE
    // send byte (should be ACK/NAK) bundled in sync response
    txbuf[0] = NETSIO_SYNC_RESPONSE;
    txbuf[1] = (uint8_t)_sync_request_num;
    txbuf[2] = response_type;
    txbuf[3] = ack_byte;
    txbuf[4] = (uint8_t)(sync_write_size & 0xff);
    txbuf[5] = (uint8_t)((sync_write_size >> 8) & 0xff);
    // clear sync request
    _sync_request_num = -1;
    _sync_write_size = -1;

    wait_for_credit(1);
    ssize_t result = write_sock(txbuf, sizeof(txbuf));
    return (result > 0 && response_type != NETSIO_EMPTY_SYNC) ? 1 : 0; // amount of data bytes written
}

void NetSioPort::send_empty_sync()
{
    if (_sync_request_num >= 0)
        send_sync_response(NETSIO_EMPTY_SYNC);
}

#endif // BUILD_ATARI

#endif // !ESP_PLATFORM