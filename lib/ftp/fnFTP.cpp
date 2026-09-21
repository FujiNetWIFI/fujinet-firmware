/**
 * fnFTP implementation
 */

#include "fnFTP.h"

#include <cstdio>
#include <string.h>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "Protocol.h"

#include "ftp_dir_parse.h"

fnFTP::fnFTP()
{
    _stor = false;
    _expect_control_response = false;
    control = new fnTcpClient();
    data = new fnTcpClient();
}

fnFTP::~fnFTP()
{
    if (control != nullptr)
        delete control;
    if (data != nullptr)
        delete data;

    control = nullptr;
    data = nullptr;
}

fujiError_t fnFTP::login(const string &_username, const string &_password, const string &_hostname, unsigned short _port)
{
    username = _username;
    password = _password;
    hostname = _hostname;
    control_port = _port;

    Debug_printf("fnFTP::login(%s,%u)\r\n", hostname.c_str(), control_port);

    // Attempt to open control socket.
    if (!control->connect(hostname.c_str(), control_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not log in, errno = %u\r\n", errno);
        _statusCode = 421; // service not available
        return FUJI_ERROR::UNSPECIFIED;
    }

    Debug_printf("Connected, waiting for 220.\r\n");

    // Wait for banner.
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 220 banner.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    Debug_printf("Sending USER.\r\n");

    if (is_positive_completion_reply() && is_connection())
    {
        // send username.
        USER();
    }
    else
    {
        Debug_printf("Could not send username. Response was: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 331 or 230.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_intermediate_reply() && is_authentication())
    {
        Debug_printf("Sending PASS.\r\n");
        // Send password
        PASS();

        if (parse_response() != FUJI_ERROR::NONE)
        {
            Debug_printf("Timed out waiting for 230.\r\n");
            return FUJI_ERROR::UNSPECIFIED;
        }
    }
    else
    {
        Debug_printf("Will not send password. Response was: %s\r\n", controlResponse.c_str());
    }

    if (is_positive_completion_reply() && is_authentication())
    {
        Debug_printf("Logged in successfully. Setting type.\r\n");
        TYPE();
    }
    else
    {
        Debug_printf("Could not finish log in. Response was: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 200.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply() && is_syntax())
    {
        Debug_printf("Logged in\r\n");
    }
    else
    {
        Debug_printf("Could not set image type. Ignoring.\r\n");
    }

    return FUJI_ERROR::NONE;
}

fujiError_t fnFTP::logout()
{
    Debug_printf("fnFTP::logout()\r\n");
    if (!control->connected())
    {
        Debug_printf("Logout called when not connected.\r\n");
        return FUJI_ERROR::NONE;
    }

    if (data->connected())
    {
        ABOR();
        parse_response(); // Ignored.
        data->stop();
    }

    QUIT();

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 221.\r\n");
    }

    control->stop();

    return FUJI_ERROR::NONE;
}

fujiError_t fnFTP::reconnect()
{
    Debug_println("Trying to re-login");
    if (control->connected()) logout();
    return login(username, password, hostname, control_port);
}

int32_t fnFTP::get_file_size(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::get_file_size(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return -1;
    }

    // Send SIZE command
    SIZE(path);

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 213 response.\r\n");
        return -1;
    }

    if (status() == 213)
    {
        // Parse size from response
        int32_t size = strtoull(controlResponse.substr(4).c_str(), nullptr, 10);
        Debug_printf("File size of %s is %lu bytes.\r\n", path.c_str(), size);
        return size;
    }
    else
    {
        Debug_printf("Could not get file size. Response was: %s\r\n", controlResponse.c_str());
        return -1;
    }
}

fujiError_t fnFTP::open_file(string path, bool stor)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_file(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    int retries = 2;
    while (get_data_port() != FUJI_ERROR::NONE)
    {
        if ((is_negative_permanent_reply() || is_negative_transient_reply()) && retries--)
        {
            // recovery attempt
            fnSystem.delay(2000);
            if (reconnect() == FUJI_ERROR::NONE)
                continue; // successfully reconnected
        }
        Debug_printf("fnFTP::open_file(%s, %s) could not get data port. Aborting.\n", path.c_str(), stor ? "STOR" : "RETR");
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Do command
    if (stor == true)
    {
        STOR(path);
    }
    else
    {
        RETR(path);
    }

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for 150 response.\r\n");
        if (_active_mode)
            _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if ((is_positive_preliminary_reply() == FUJI_ERROR::NONE) && is_filesystem_related())
    {
        _stor = stor;
        _expect_control_response = !stor;
        Debug_printf("Server began transfer.\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("Server could not begin transfer. Response was: %s\r\n", controlResponse.c_str());
        if (_active_mode)
            _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }
}

fujiError_t fnFTP::open_directory(string path, string pattern)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_directory(%s%s) attempted while not logged in. Aborting.\r\n", path.c_str(), pattern.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    int retries = 2;
    while (get_data_port() != FUJI_ERROR::NONE)
    {
        if ((is_negative_permanent_reply() || is_negative_transient_reply()) && retries--)
        {
            // recovery attempt
            fnSystem.delay(2000);
            if (reconnect() == FUJI_ERROR::NONE)
                continue; // successfully reconnected
        }
        Debug_printf("fnFTP::open_directory(%s%s) could not get data port, aborting.\n", path.c_str(), pattern.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    // perform LIST
    LIST(path, pattern);

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::open_directory(%s%s) Timed out waiting for 150 response.\r\n", path.c_str(), pattern.c_str());
        if (_active_mode)
            _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }

    Debug_printf("fnFTP::open_directory(%s%s) - %s\r\n", path.c_str(), pattern.c_str(), controlResponse.c_str());

    if ((is_positive_preliminary_reply() == FUJI_ERROR::NONE) && is_filesystem_related())
    {
        // Do nothing.
        Debug_printf("Got our 150\r\n");
    }
    else
    {
        Debug_printf("Didn't get our 150\r\n");
        if (_active_mode)
            _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (accept_active_connection() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::open_directory(%s%s) - active mode connection failed.\r\n", path.c_str(), pattern.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    uint8_t buf[256];

    // if (buf == nullptr)
    // {
    //     Debug_printf("fnFTP::open_directory() - Could not allocate 2048 bytes.\r\n");
    //     return FUJI_ERROR::UNSPECIFIED;
    // }

    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    bool got_response = false;

    // Reset buffer
    dirBuffer.str("");
    dirBuffer.clear();
    // Retrieve listing into buffer.
    do
    {
        if (data->available() == 0)
        {
            if (--tmout_counter == 0)
            {
                // no data & no control message
                Debug_printf("fnFTP::open_directory - Timeout\r\n");
                break;
            }
            fnSystem.delay(50); // wait for more data or control message
        }
        if (data->available() > 0)
        {
            Debug_printf("Retrieving directory list\r\n");
            while (data->available())
            {
                int len = data->available();
                memset(buf, 0, sizeof(buf));
                int num_read = data->read(buf, len > sizeof(buf) ? sizeof(buf) : len);
                dirBuffer << string((const char *)buf, num_read);
            }
            tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
        }
        if (got_response == false && control->available())
        {
            got_response = parse_response() == FUJI_ERROR::NONE;
        }
    } while (data->available() > 0 || data->connected());

    data->stop();

    if (tmout_counter == 0 || (got_response == false && parse_response() != FUJI_ERROR::NONE))
    {
        Debug_printf("fnFTP::open_directory(%s%s) Timed out waiting for 226 response.\r\n", path.c_str(), pattern.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE; // all good.
}

fujiError_t fnFTP::read_directory(string &name, long &filesize, bool &is_dir)
{
    // The parsing lives in ftp_dir_parse so it can be tested without a socket;
    // dirBuffer is filled by open_directory() above.
    return ftp_next_dir_entry(dirBuffer, name, filesize, is_dir);
}

fujiError_t fnFTP::read_file(uint8_t *buf, unsigned short len, unsigned long range_begin, unsigned long range_end)
{
    // Debug_printv("fnFTP::read_file(%p, %u, %lu, %lu)", buf, len, range_begin, range_end);

    // If range parameters are provided and different from current, send RANG command
    if ((range_begin > 0 || range_end > 0) && (range_begin != _range_begin || range_end != _range_end))
    {
        RANG(range_begin, range_end);
        _range_begin = range_begin;
        _range_end = range_end;
    }

    if (!data->connected() && data->available() == 0)
    {
        Debug_printf("fnFTP::read_file(%p,%u) - data socket not connected, aborting.\r\n", buf, len);
        return FUJI_ERROR::UNSPECIFIED;
    }
    return (len != data->read(buf, len)) ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t fnFTP::write_file(uint8_t *buf, unsigned short len)
{
    //Debug_printf("fnFTP::write_file(%p,%u)\r\n", buf, len);
    if (!data->connected())
    {
        Debug_printf("fnFTP::write_file(%p,%u) - data socket not connected, aborting.\r\n", buf, len);
        return FUJI_ERROR::UNSPECIFIED;
    }

    return (len != data->write(buf, len)) ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t fnFTP::close()
{
    fujiError_t res = FUJI_ERROR::NONE;
    Debug_printf("fnFTP::close()\r\n");
    if (_stor)
    {
        if (data->connected())
        {
            data->stop();
        }
        if (parse_response() != FUJI_ERROR::NONE)
        {
            Debug_printf("Timed out waiting for 226.\r\n");
            res = FUJI_ERROR::UNSPECIFIED;
        }
    }
    _stor = false;
    _expect_control_response = false;
    control->flush();
    return res;
}

int fnFTP::status()
{
    return _statusCode;
}

int fnFTP::data_available()
{
    return data->available();
}

fujiError_t fnFTP::data_connected()
{
    if (_expect_control_response && control->available())
        _expect_control_response = parse_response() != FUJI_ERROR::NONE;
    return (_expect_control_response || data->connected())
        ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

bool fnFTP::control_connected()
{
    return control != nullptr && control->connected();
}

/** FTP UTILITY FUNCTIONS **********************************************************************/

fujiError_t fnFTP::parse_response()
{
    char respBuf[384];  // room for control message incl. file path and file size
    int num_read = 0;
    bool multi_line = false;

    controlResponse.clear();

    while(true)
    {
        num_read = read_response_line(respBuf, sizeof(respBuf));
        if (num_read < 0)
        {
            // Timeout
            _statusCode = 421;  // service not available
            return FUJI_ERROR::UNSPECIFIED;        // error
        }
        if (num_read >= 4)
        {
            if (isdigit(respBuf[0]) && isdigit(respBuf[1]) && isdigit(respBuf[2]))
            {
                if (respBuf[3] == ' ')  // done, got NNN<space>
                    break;
                if (respBuf[3] == '-')
                {
                    // head of multi-line response
                    multi_line = true;
                    continue;
                }
            }
        }
        if (multi_line) // ignore body of multi-line response
            continue;
        // error - nothing above
        Debug_printf("fnFTP::parse_response() - failed\r\n");
        _statusCode = 501;  //syntax error
        return FUJI_ERROR::UNSPECIFIED;        // error
    }

    // update control response and status code
    controlResponse = string((char *)respBuf, num_read);
    _statusCode = atoi(controlResponse.substr(0, 3).c_str());
    Debug_printf("fnFTP::parse_response() - %d, \"%s\"\r\n", _statusCode, controlResponse.c_str());

    if (_statusCode >= 400)
        return FUJI_ERROR::UNSPECIFIED;

    return FUJI_ERROR::NONE; // ok
}

int fnFTP::read_response_line(char *buf, int buflen)
{
    int num_read = 0;
    int c;
    int tmout_counter = 1 + FTP_TIMEOUT / 50;

    while(true)
    {
        if (control->available() == 0)
        {
            if (--tmout_counter == 0)
            {
                Debug_printf("fnFTP::read_response_line() - Timeout waiting response\r\n");
                return -1;
            }
            fnSystem.delay(50);
            continue;
        }

        c = control->read(); // singe byte
        if (c < 0)
            break;  // read error

        if(c == '\n' || c == '\r') // almost done, got line
        {
            // eat all line terminators
            if (control->available())
            {
                // test next byte
                c = control->peek();
                if (c == '\n' || c== '\r')
                    continue; // read it
            }
            break; // done
        }
        // store char, ignore rest of too long response
        if (num_read < buflen)
            buf[num_read++] = (char) c;
        tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
    }
    return num_read;
}

fujiError_t fnFTP::get_data_port()
{
    Debug_printf("fnFTP::get_data_port()\r\n");

    _active_mode = false;
    control->flush();

    if (get_data_port_epsv() == FUJI_ERROR::NONE)
        return FUJI_ERROR::NONE; // success

    Debug_printf("EPSV failed (%s), falling back to PASV.\r\n", controlResponse.c_str());

    if (get_data_port_pasv() == FUJI_ERROR::NONE)
        return FUJI_ERROR::NONE; // success

    Debug_printf("PASV failed (%s), falling back to PORT (active mode).\r\n", controlResponse.c_str());

    return get_data_port_port();
}

fujiError_t fnFTP::get_data_port_epsv()
{
    size_t port_pos_beg, port_pos_end;

    Debug_printf("fnFTP::get_data_port_epsv()\r\n");

    control->flush();
    EPSV();

    Debug_printf("Did EPSV, getting response.\r\n");

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for response.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

/*
    if (is_negative_permanent_reply())
    {
        Debug_printf("Server unable to reserve port. Response was: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_negative_transient_reply())
    {
        Debug_printf("Cannot get data port. Response was: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_negative_transient_reply())
    {
        Debug_printf("Cannot get data port. Response was: %s\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
*/

    // accept only 229 response: Entering Extended Passive Mode (|||nnnn|)
    if (_statusCode != 229)
    {
        Debug_printf("Cannot get data port. Response was: %s\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    // At this point, we have a port mapping trapped in (|||1234|), peel it out of there.
    port_pos_beg = controlResponse.find_first_of("|") + 3;
    port_pos_end = controlResponse.find_last_of("|");
    data_port = atoi(controlResponse.substr(port_pos_beg, port_pos_end).c_str());

    Debug_printf("Server gave us data port: %u\r\n", data_port);

    // Go ahead and connect to data port, so that control port is unblocked, if it's blocked.
    if (!data->connect(hostname.c_str(), data_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not open data port %u, errno = %u\r\n", data_port, errno);
        return FUJI_ERROR::UNSPECIFIED;
    }
    else
    {
        Debug_printf("Data port %u opened (EPSV).\r\n", data_port);
    }

    return FUJI_ERROR::NONE;
}

fujiError_t fnFTP::get_data_port_pasv()
{
    PASV();

    Debug_printf("Did PASV, getting response.\r\n");

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for response.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    // accept only 227 response: Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    if (_statusCode != 227)
    {
        Debug_printf("Cannot get data port. Response was: %s\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    size_t paren_beg = controlResponse.find('(');
    size_t paren_end = controlResponse.find(')', paren_beg == string::npos ? 0 : paren_beg);
    if (paren_beg == string::npos || paren_end == string::npos)
    {
        Debug_printf("Could not parse PASV response: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    string nums = controlResponse.substr(paren_beg + 1, paren_end - paren_beg - 1);
    unsigned int h1, h2, h3, h4, p1, p2;
    if (sscanf(nums.c_str(), "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
    {
        Debug_printf("Could not parse PASV address: %s\r\n", nums.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", h1, h2, h3, h4);
    data_port = (uint16_t)((p1 << 8) | p2);

    Debug_printf("Server gave us data address %s:%u\r\n", ip_str, data_port);

    // Note: some servers behind NAT report an internal/unreachable IP here;
    // we use it as given, per RFC 959.
    if (!data->connect(ip_str, data_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not open data port %s:%u, errno = %u\r\n", ip_str, data_port, errno);
        return FUJI_ERROR::UNSPECIFIED;
    }
    else
    {
        Debug_printf("Data port %s:%u opened (PASV).\r\n", ip_str, data_port);
    }
    return FUJI_ERROR::NONE;
}


fujiError_t fnFTP::get_data_port_port()
{
    if (!_active_server.begin(0))
    {
        Debug_printf("Could not start listening socket for active mode.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    in_addr_t local_ip = control->localIP();
    uint16_t local_port = _active_server.port();
    const uint8_t *ip_bytes = (const uint8_t *)&local_ip;

    PORT(ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3], local_port);

    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("Timed out waiting for response.\r\n");
        _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (!is_positive_completion_reply())
    {
        Debug_printf("Server rejected PORT. Response was: %s\r\n", controlResponse.c_str());
        _active_server.stop();
        return FUJI_ERROR::UNSPECIFIED;
    }

    _active_mode = true;
    Debug_printf("Listening on port %u for server to connect (PORT).\r\n", local_port);

    return FUJI_ERROR::NONE;
}

fujiError_t fnFTP::accept_active_connection()
{
    if (!_active_mode)
        return FUJI_ERROR::NONE; // nothing to do, EPSV/PASV already connected

    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    while (!_active_server.hasClient())
    {
        if (--tmout_counter == 0)
        {
            Debug_printf("fnFTP::accept_active_connection() - timed out waiting for server to connect.\r\n");
            _active_server.stop();
            return FUJI_ERROR::UNSPECIFIED;
        }
        fnSystem.delay(50);
    }

    *data = _active_server.client();
    _active_server.stop(); // done listening, we only needed the one connection

    Debug_printf("fnFTP::accept_active_connection() - server connected.\r\n");
    return FUJI_ERROR::NONE;
}

/** FTP VERBS **********************************************************************************/

void fnFTP::USER()
{
    control->write("USER " + username + "\r\n");
}

void fnFTP::PASS()
{
    control->write("PASS " + password + "\r\n");
}

void fnFTP::TYPE()
{
    Debug_printf("fnFTP::TYPE()\r\n");
    control->write("TYPE I\r\n");
}

void fnFTP::QUIT()
{
    Debug_printf("fnFTP::QUIT()\r\n");
    control->write("QUIT\r\n");
}

void fnFTP::EPSV()
{
    Debug_printf("fnFTP::EPSV()\r\n");
    control->write("EPSV\r\n");
}

void fnFTP::PASV()
{
    Debug_printf("fnFTP::PASV()\r\n");
    control->write("PASV\r\n");
}

void fnFTP::PORT(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4, uint16_t port)
{
    Debug_printf("fnFTP::PORT(%u.%u.%u.%u:%u)\r\n", h1, h2, h3, h4, port);
    control->write("PORT " + std::to_string(h1) + "," + std::to_string(h2) + "," +
                   std::to_string(h3) + "," + std::to_string(h4) + "," +
                   std::to_string(port >> 8) + "," + std::to_string(port & 0xff) + "\r\n");
}

void fnFTP::RETR(string path)
{
    Debug_printf("fnFTP::RETR(%s)\r\n",path.c_str());
    control->write("RETR " + path + "\r\n");
}

void fnFTP::CWD(string path)
{
    Debug_printf("fnFTP::CWD(%s)\r\n",path.c_str());
    control->write("CWD " + path + "\r\n");
}

void fnFTP::LIST(string path, string pattern)
{
    Debug_printf("fnFTP::LIST(%s,%s)\r\n",path.c_str(),pattern.c_str());
    control->write("LIST " + path + pattern + "\r\n");
}

void fnFTP::ABOR()
{
    Debug_printf("fnFTP::ABOR()\r\n");
    control->write("ABOR\r\n");
}

void fnFTP::STOR(string path)
{
    Debug_printf("fnFTP::STOR(%s)\r\n",path.c_str());
    control->write("STOR " + path + "\r\n");
}

void fnFTP::RANG(unsigned long start, unsigned long end)
{
    Debug_printf("fnFTP::RANG(%lu,%lu)\r\n", start, end);
    control->write("RANG " + std::to_string(start) + "-" + std::to_string(end) + "\r\n");
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::RANG - error response from server\r\n");
    }
}

void fnFTP::SIZE(string path)
{
    Debug_printf("fnFTP::SIZE(%s)\r\n",path.c_str());
    control->write("SIZE " + path + "\r\n");
}

void fnFTP::NOOP()
{
    Debug_printf("fnFTP::NOOP\r\n");
    control->write("NOOP\r\n");
}

fujiError_t fnFTP::keep_alive()
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::keep_alive() attempted while not logged in. Aborting.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    NOOP();
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::keep_alive - timeout\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::keep_alive - successful\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("fnFTP::keep_alive - error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
}


fujiError_t fnFTP::delete_file(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::delete_file(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    DELE(path);
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::delete_file - timeout\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::delete_file - file deleted\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("fnFTP::delete_file - error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
}

fujiError_t fnFTP::rename_file(string pathFrom, string pathTo)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::rename_file(%s -> %s) attempted while not logged in. Aborting.\r\n", pathFrom.c_str(), pathTo.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    RNFR(pathFrom);
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::rename_file - timeout on RNFR\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (!is_positive_intermediate_reply())
    {
        Debug_printf("fnFTP::rename_file - RNFR error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    RNTO(pathTo);
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::rename_file - timeout on RNTO\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::rename_file - file renamed\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("fnFTP::rename_file - RNTO error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
}

fujiError_t fnFTP::make_directory(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::make_directory(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    MKD(path);
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::make_directory - timeout\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::make_directory - directory created\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("fnFTP::make_directory - error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
}

fujiError_t fnFTP::remove_directory(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::remove_directory(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    RMD(path);
    if (parse_response() != FUJI_ERROR::NONE)
    {
        Debug_printf("fnFTP::remove_directory - timeout\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::remove_directory - directory removed\r\n");
        return FUJI_ERROR::NONE;
    }
    else
    {
        Debug_printf("fnFTP::remove_directory - error: %s\r\n", controlResponse.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }
}

void fnFTP::DELE(string path)
{
    Debug_printf("fnFTP::DELE(%s)\r\n", path.c_str());
    control->write("DELE " + path + "\r\n");
}

void fnFTP::RNFR(string pathFrom)
{
    Debug_printf("fnFTP::RNFR(%s)\r\n", pathFrom.c_str());
    control->write("RNFR " + pathFrom + "\r\n");
}

void fnFTP::RNTO(string pathTo)
{
    Debug_printf("fnFTP::RNTO(%s)\r\n", pathTo.c_str());
    control->write("RNTO " + pathTo + "\r\n");
}

void fnFTP::MKD(string path)
{
    Debug_printf("fnFTP::MKD(%s)\r\n", path.c_str());
    control->write("MKD " + path + "\r\n");
}

void fnFTP::RMD(string path)
{
    Debug_printf("fnFTP::RMD(%s)\r\n", path.c_str());
    control->write("RMD " + path + "\r\n");
}
