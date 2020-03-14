#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <Arduino.h>
#include "networkDeviceSpec.h"
#include "debug.h"

char ret[256];

bool is_num(char* s)
{
    for (int i=0;i<strlen(s);i++)
    {
        if (!isdigit(s[i]))
            return false;
    }

    return true;
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

/**
     * Parse input string: N1:TCP:FOO.COM:2000 or N1:TCP:2000 
     * s - Input string
     * Returns: boolean for valid devicespec.
     */
bool networkDeviceSpec::parse(char *s)
{
    char *token = strtok(s, ":");
    int i = 0;

    while (token != NULL)
    {
        switch (i++)
        {
        case 0:
            strcpy(device,token);
            break;
        case 1:
            strcpy(protocol,token);
            break;
        case 2:
            if (is_num(token))
            {
                port=atoi(token);
            }
            else
            {
                strcpy(path,token);
            }
            break;
        case 3:
            port=atoi(token);
            break;
        }
        token = strtok(NULL,":");
    }
    debug();
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