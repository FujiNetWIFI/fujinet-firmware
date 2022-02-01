/**
 * modem sniffer library for FujiNet
 * logs character streams from MODEM.
 */

#ifndef MODEM_SNIFFER_H
#define MODEM_SNIFFER_H

#include <cstdint>
#include <string>

#include <stdio.h>

#include "fnFS.h"
// #include "fnFsSD.h"
// #include "fnFsSPIFFS.h"

using namespace std;

#define SNIFFER_OUTPUT_FILE "/rs232dump"

class ModemSniffer
{

public:
    /**
     * ctor
     * @param _fs a pointer to the active VFS filesystem object chosen at device start.
     * @param _enable TRUE if modemSniffer enabled.
     */
    ModemSniffer(FileSystem *_fs, bool _enable = false);

    /**
     * dtor
     */
    virtual ~ModemSniffer();

    /**
     * Return the output size of the current dump file
     */
    size_t getOutputSize();

    /**
     * Close the dump output
     */
    void closeOutput();

    /**
     * Dump output to file
     */
    void dumpOutput(uint8_t *buf, unsigned short len);

    /**
     * Dump input to file
     */
    void dumpInput(uint8_t *buf, unsigned short len);

    /**
     * Close output, and return a R/O file handle for web interface.
     */
    FILE *closeOutputAndProvideReadHandle();

    /**
     * Set enable flag
     */
    void setEnable(bool _enable) { enable = _enable; }

    /**
     * Get enable flag
     */
    bool getEnable() { return enable; }

private:
    /**
     * Is sniffer enabled?
     */
    bool enable = false;


    /**
     * indicate I/O direction for logging label.
     */
    enum _direction
    {
        INIT,
        INPUT,
        OUTPUT
    } direction;

protected:
    /**
     * Pointer to ESP32 filesystem
     */
    FileSystem *activeFS = nullptr;

    /**
     * Pointer to currently open file
     */
    FILE *_file = nullptr;

    /**
     * Output buffer
     */
    std::string outputBuffer;

    /**
     * Recreate SNIFFER_OUTPUT_FILE
     */
    void restartOutput();

};

#endif /* MODEM_SNIFFER_H */