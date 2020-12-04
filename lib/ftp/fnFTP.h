/**
 * FTP class for #FujiNet
 */

#ifndef FNFTP_H
#define FNFTP_H

#include <string>
#include <sstream>
#include "../tcpip/fnTcpClient.h"
#include "list-parse.h"

using namespace std;

class fnFTP
{
public:

    /**
     * Log into FTP server.
     * @param username username for login
     * @param password password for login
     * @param hostname host to login
     * @param port port to login (default 21)
     * @return TRUE on error, FALSE on success
     */
    bool login(string _username, string _password, string _hostname, unsigned short _port = 21);

    /**
     * Log out of FTP server, closes control connection.
     * @return TRUE on error, FALSE on success.
     */
    bool logout();

    /**
     * Open file on FTP server
     * @param path to file to open.
     * @return TRUE if error, FALSE if successful.
     */
    bool open_file(string path);

    /**
     * Open directory on FTP server, grab it, and return back.
     * @param path directory to retrieve.
     * @param pattern pattern to retrieve.
     * @return TRUE if error, FALSE if successful.
     */
    bool open_directory(string path, string pattern);

    /**
     * Read and return one parsed line of directory
     * @param name pointer to output name
     * @param filesize pointer to output filesize
     * @return TRUE if error, FALSE if successful
     */
    bool read_directory(string& name, long& filesize);

    /**
     * Read file from data socket into buffer.
     * @param buf target buffer
     * @param len length of target buffer
     * @return TRUE if error, FALSE if successful.
     */
    bool read_file(uint8_t* buf, unsigned short len);

    /**
     * @brief close data and/or control sockets.
     */
    void close();

protected:
private:
    /**
     * The hostname
     */
    string hostname;

    /**
     * The port number. (21 by default)
     */
    unsigned short control_port = 21;

    /**
     * The fnTCP client used for control connection
     */
    fnTcpClient control;

    /**
     * The fnTCP client used for data connection
     */
    fnTcpClient data;

    /**
     * last response from control connection.
     */
    string controlResponse;

    /**
     * Username
     */
    string username;

    /**
     * Password
     */
    string password;

    /**
     * Directory buffer stream
     */
    stringstream dirBuffer;

    /**
     * The data port returned by EPSV
     */
    unsigned short data_port;

    /**
     * read and parse control response
     * @return true on error, false on success.
     */
    bool get_response();

    /**
     * Ask server to prepare a data port for us in extended passive mode.
     * Port is set and returned in data_port variable.
     * @return TRUE if error, FALSE if successful.
     */
    bool get_data_port();

    /**
     * @brief Is response a positive preliminary reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '1'; }

    /**
     * @brief Is response a positive completion reply?
     * @return true or false.
     */
    bool is_positive_completion_reply() { return controlResponse[0] == '2'; }

    /**
     * @brief Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_positive_intermediate_reply() { return controlResponse[0] == '3'; }

    /**
     * @brief Is response a negative transient reply?
     * @return true or false.
     */
    bool is_negative_transient_reply() { return controlResponse[0] == '4'; }

    /**
     * @brief Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_negative_permanent_reply() { return controlResponse[0] == '5'; }

    /**
     * @brief Is response a protected reply?
     * @return true or false.
     */
    bool is_protected_reply() { return controlResponse[0] == '6'; }

    /**
     * @brief Is response a syntax error?
     * @return true or false.
     */
    bool is_syntax() { return controlResponse[1] == '0'; }

    /**
     * @brief Is response informational?
     * @return true or false.
     */
    bool is_informational() { return controlResponse[1] == '1'; }

    /**
     * @brief Is response referring to a change in connection state?
     * @return true or false.
     */
    bool is_connection() { return controlResponse[1] == '2'; }

    /**
     * @brief Is response referring to an authoeization/authentication issue?
     * @return true or false.
     */
    bool is_authentication() { return controlResponse[1] == '3'; }

    /**
     * @brief IS response filesystem related?
     * @return true or false.
     */
    bool is_filesystem_related() { return controlResponse[1] == '5'; }

    /**
     * @brief Perform USER command on open control connection
     */
    void USER();

    /**
     * @brief Perform PASS command on open control connection
     */
    void PASS();

    /**
     * @brief Perform TYPE I command on open control connection
     */
    void TYPE();

    /**
     * @brief Log out.
     */
    void QUIT();

    /**
     * @brief Enter extended passive mode (RFC 2428)
     */
    void EPSV();

    /**
     * @brief Ask server to retrieve path
     * @param path path to retrieve.
     */
    void RETR(string path);

    /**
     * @brief change current directory to path.
     * @param path path to change directory to.
     */
    void CWD(string path);

    /**
     * @brief ask server for directory listing.
     * @param path path of directory listing
     * @param pattern requested pattern
     */
    void LIST(string path, string pattern);

    /**
     * @brief ask server to abort current transfer
     */
    void ABOR();

};

#endif /* FNFTP_H */