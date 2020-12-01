/**
 * fnFTP implementation
 */

#include <string.h>
#include "../../include/debug.h"
#include "fnFTP.h"

bool fnFTP::login(string hostName, unsigned short port)
{
    Debug_printf("fnFTP::login(%s,%u)\n", hostName.c_str(), port);

    // Attempt to open control socket.
    if (control.connect(hostName.c_str(), port))
    {
        Debug_printf("Could not log in, errno = %u",errno);
        return true;
    }



    return true;
}

bool fnFTP::logout()
{
    return true;
}

string fnFTP::get_response()
{
    char buf[512];
    int num_read;

    memset(buf,0,sizeof(buf));

    num_read = control.read_until('\n',buf,sizeof(buf));

    if (num_read<0)
    {
        Debug_printf("fnFTP::get_response() - Could not read from control socket.\n");
        return string();
    }

    controlResponse = string(buf,num_read);

    Debug_printf("fnFTP::get_response() - %s\n",controlResponse.c_str());

    return controlResponse.substr(0,controlResponse.find_first_of(" "));
}