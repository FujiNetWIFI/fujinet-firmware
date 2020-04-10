#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <Arduino.h>
#include "networkDeviceSpec.h"
#include "debug.h"

char ret[256];

bool is_num(char *s)
{
    for (int i = 0; i < strlen(s); i++)
    {
        if (!isdigit(s[i]))
            return false;
    }

    return true;
}

networkDeviceSpec::networkDeviceSpec()
{
    clear();
}

void networkDeviceSpec::debug()
{
#ifdef DEBUG
    Debug_printf("NetworkDeviceSpec debug\n");
    Debug_printf("Device: %s\n", device);
    Debug_printf("Protocol: %s\n", protocol);
    Debug_printf("Path: %s\n", path);
    Debug_printf("Port: %d\n", port);
#endif
}

void networkDeviceSpec::clear()
{
    memset(&device, 0, sizeof(device));
    memset(&protocol, 0, sizeof(protocol));
    memset(&path, 0, sizeof(path));
    port = 0;
}

/**
     * Parse input string: N1:TCP:FOO.COM:2000 or N1:TCP:2000 
     * s - Input string
     * Returns: boolean for valid devicespec.
     */
bool networkDeviceSpec::parse(char *s)
{
    char *token;
    int i = 0;

    // Remove EOL
    for (i = 0; i < strlen(s); i++)
        if (s[i] == 0x9b)
            s[i] = 0x00;

    i = 0;

    token = strtok(s, ":");

    while (token != NULL)
    {
        switch (i++)
        {
        case 0:
            strcpy(device, token);
            break;
        case 1:
            strcpy(protocol, token);
            break;
        case 2:
            if (is_num(token))
            {
                port = atoi(token);
            }
            else
            {
                strcpy(path, token);
            }
            break;
        case 3:
            port = atoi(token);
            break;
        }
        token = strtok(NULL, ":");
    }
    debug();

    // Validate devicespec
    if ((protocol[0] == 0x00) || (port == 0))
        return false;

    return true;
}

/**
     * Return devicespec as string "N1:TCP:FOO.COM:2000"
     */
const char *networkDeviceSpec::toChar()
{
    if (strlen(path) > 0)
        sprintf(ret, "%s:%s:%s:%u", device, protocol, path, port);
    else
        sprintf(ret, "%s:%s:%u", device, protocol, port);

    return ret;
}