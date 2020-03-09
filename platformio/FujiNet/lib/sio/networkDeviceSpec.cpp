#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "networkDeviceSpec.h"

char ret[256];

void networkDeviceSpec::debug()
{
#ifdef DEBUG
    Debug_printf("NetworkDeviceSpec debug\n");
    Debug_printf("Device: %s",device);
    Debug_printf("");
#endif
}

/**
     * Parse input string: N1:TCP:FOO.COM:2000 or N1:TCP:2000 
     * s - Input string
     * Returns: boolean for valid devicespec.
     */
bool networkDeviceSpec::parse(char *s)
{
    char *p;
    char i = 0;
    int d = 0;

    p = strtok(s, ":"); // Get Device spec

    if (p[0] != 'N')
        return false;
    else
        strcpy(device, p);

    while (p != NULL)
    {
        i++;
        p = strtok(NULL, ":");
        switch (i)
        {
        case 1:
            strcpy(protocol, p);
            break;
        case 2:
            for (d = 0; d < strlen(p); d++)
                if (!isdigit(p[d]))
                {
                    strcpy(path, p);
                    break;
                }
            port = atoi(p);
            isValid = true;
#ifdef DEBUG
            debug();
#endif
            return true;
        case 3:
            port = atoi(p);
            isValid = true;
#ifdef DEBUG
            debug();
#endif
            return true;
            break;
        default:
            isValid = false;
#ifdef DEBUG
            debug();
#endif
            return false; // Too many parameters.
        }
    }
    return false;
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