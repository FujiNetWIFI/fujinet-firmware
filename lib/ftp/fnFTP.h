/**
 * FTP class for #FujiNet
 */

#ifndef FNFTP_H
#define FNFTP_H

#include <string>
#include "../tcpip/fnTcpClient.h"

using namespace std;

class fnFTP
{
public:

    /**
     * Log into FTP server.
     * @param hostname The host to connect to.
     * @param port the control port # to connect to. Default is 21.
     * @return TRUE on error, FALSE on success
     */
    bool login(string hostname, unsigned short port = 21);

    /**
     * Log out of FTP server, closes control connection.
     * @return TRUE on error, FALSE on success.
     */
    bool logout();

    /**
     * read and parse control response
     * @return the numeric response
     */
    string get_response();

    /**
     * Is response a positive preliminary reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '1'; } 

    /**
     * Is response a positive completion reply?
     * @return true or false.
     */
    bool is_positive_completion_reply() { return controlResponse[0] == '2'; } 

    /**
     * Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '3'; } 

    /**
     * Is response a negative transient reply?
     * @return true or false.
     */
    bool is_negative_transient_reply() { return controlResponse[0] == '4'; } 

    /**
     * Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_negative_permanent_reply() { return controlResponse[0] == '5'; } 

    /**
     * Is response a protected reply?
     * @return true or false.
     */
    bool is_protected_reply() { return controlResponse[0] == '6'; } 

protected:
private:

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

};

#endif /* FNFTP_H */