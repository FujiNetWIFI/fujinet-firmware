#ifndef NETWORKDEVICESPEC_H
#define NETWORKDEVICESPEC_H

class networkDeviceSpec
{
public:

    char device[4];         // Device Name/Number: N1:
    char protocol[16];      // Protocol: TCP
    char path[234];         // Path: FOO.COM
    unsigned short port;    // Port: 2000
    bool isValid;           // is devicespec valid?

    /**
     * ctor to create devicespec from string
     */
    networkDeviceSpec(char* s) { parse(s); }

    /**
     * Dump devicespec to debug
     */
    void debug();

    /**
     * Parse input string: N1:TCP:FOO.COM:2000 or N1:TCP:2000 
     * s - Input string
     * Returns: boolean for valid devicespec.
     */
    bool parse(char* s);

    /**
     * Return devicespec as string "N1:TCP:FOO.COM:2000"
     */
    const char* toChar();
};

#endif /* NETWORKDEVICESPEC_H */