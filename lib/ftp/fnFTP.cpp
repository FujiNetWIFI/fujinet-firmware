/**
 * fnFTP implementation
 */

#include <string.h>
#include "../../include/debug.h"
#include "fnFTP.h"

bool fnFTP::login(string _username, string _password, string _hostname, unsigned short _port)
{
    username = _username;
    password = _password;
    hostname = _hostname;
    control_port = _port;

    Debug_printf("fnFTP::login(%s,%u)\n", hostname.c_str(), control_port);

    // Attempt to open control socket.
    if (control.connect(hostname.c_str(), control_port))
    {
        Debug_printf("Could not log in, errno = %u", errno);
        return true;
    }

    // Wait for banner.
    if (get_response())
    {
        Debug_printf("Timed out waiting for 220 banner.");
        return true;
    }

    if (is_positive_completion_reply() && is_connection())
    {
        // send username.
        USER();
    }
    else
    {
        Debug_printf("Could not send username. Response was: %s\n",controlResponse.c_str());
        return true;
    }
    
    if (get_response())
    {
        Debug_printf("Timed out waiting for 331.\n");
        return true;
    }

    if (is_positive_preliminary_reply() && is_authentication())
    {
        // Send password
        PASS();
    }
    else
    {
        Debug_printf("Could not send password. Response was: %s\n",controlResponse.c_str());
    }
    
    if (get_response())
    {
        Debug_printf("Timed out waiting for 230.\n");
        return true;
    }

    if (is_positive_completion_reply() && is_authentication())
    {
        Debug_printf("Logged in successfully. Setting type.\n");
        TYPE();
    }
    else
    {
        Debug_printf("Could not finish log in. Response was: %s\n",controlResponse.c_str());
        return true;
    }

    if (get_response())
    {
        Debug_printf("Timed out waiting for 200.\n");
        return true;
    }

    if (is_positive_completion_reply() && is_syntax())
    {
        Debug_printf("Logged in\n");
    }
    else
    {
        Debug_printf("Could not set image type. Ignoring.\n");       
    }
    
    return false;
}

bool fnFTP::logout()
{
    if (!control.connected())
    {
        Debug_printf("fnFTP::logout() - called when not connected.");
        return true;
    }

    QUIT();

    if (get_response())
    {
        Debug_printf("Timed out waiting for 221.");
    }

    return true;
}

bool fnFTP::get_response()
{
    char buf[512];
    int num_read;

    memset(buf, 0, sizeof(buf));

    num_read = control.read_until('\n', buf, sizeof(buf));

    if (num_read < 0)
    {
        Debug_printf("fnFTP::get_response() - Could not read from control socket.\n");
        return true;
    }

    controlResponse = string(buf, num_read);

    Debug_printf("fnFTP::get_response() - %s\n", controlResponse.c_str());

    return controlResponse.substr(0, controlResponse.find_first_of(" ")).empty();
}

bool fnFTP::get_data_port()
{
    size_t port_pos_beg, port_pos_end;

    Debug_printf("fnFTP::get_data_port()\n");

    EPSV();

    if (get_response())
    {
        Debug_printf("Timed out waiting for response.\n");
        return;
    }

    if (is_negative_permanent_reply())
    {
        Debug_printf("Server unable to reserve port. Response was: %s\n",controlResponse.c_str());
        return true;
    }

    // At this point, we have a port mapping trapped in (|||1234|), peel it out of there.
    port_pos_beg = controlResponse.find_first_of("|") + 3;
    port_pos_end = controlResponse.find_last_of("|");
    data_port = atoi(controlResponse.substr(port_pos_beg,port_pos_end).c_str());

    Debug_printf("Server gave us data port: %u\n",data_port);

    // Go ahead and connect to data port, so that control port is unblocked, if it's blocked.
    if (data.connect(hostname.c_str(),data_port))
    {
        Debug_printf("Could not open data port %u, errno = %u\n",data_port,errno);
        return true;
    }
    else
    {
        Debug_printf("Data port %u opened.\n",data_port);
    }
    
    return false;
}

/** FTP VERBS **********************************************************************************/

void fnFTP::USER()
{
    control.write("USER " + username + "\r\n");
    control.flush();
}

void fnFTP::PASS()
{
    control.write("PASS " + password + "\r\n");
    control.flush();
}

void fnFTP::TYPE()
{
    control.write("TYPE I\r\n");
    control.flush();
}

void fnFTP::QUIT()
{
    control.write("QUIT\r\n");
    control.flush();
}

void fnFTP::EPSV()
{
    control.write("EPSV\r\n");
    control.flush();
}