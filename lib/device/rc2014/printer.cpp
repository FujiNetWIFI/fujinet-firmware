#ifdef BUILD_RC2014

#include "printer.h"

#include <cstring>

#include "../../../include/debug.h"

#include "fnSystem.h"
#include "led.h"

#include "atari_1020.h"
#include "atari_1025.h"
#include "file_printer.h"
#include "html_printer.h"
#include "svg_plotter.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "okimate_10.h"
#include "png_printer.h"
#include "coleco_printer.h"

/* TODO
   - handle streaming mode
*/

std::string buf;
bool taskActive=false;

constexpr const char * const rc2014Printer::printer_model_str[PRINTER_INVALID];

// Constructor just sets a default printer type
rc2014Printer::rc2014Printer(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);
}

rc2014Printer::~rc2014Printer()
{
    delete _pptr;
    _pptr = nullptr;
}

rc2014Printer::printer_type rc2014Printer::match_modelname(std::string model_name)
{
    int i;

    for (i = 0; i < PRINTER_INVALID; i++) {
        if (model_name.compare(rc2014Printer::printer_model_str[i]) == 0)
            break;
    }

    return (printer_type)i;
}

void rc2014Printer::status()
{
    uint8_t c[4] = {};

    Debug_printf("rc2014printer::status()\n");

    _last_ms = fnSystem.millis();

    rc2014_send_ack();

    c[0] = 0; /* error status */
    c[1] = _lastaux1; /* last write length */
    c[2] = 0; /* reserved */
    c[3] = 0; /* reserved */

    rc2014_send_buffer((uint8_t *)c, 4);
    rc2014_send(rc2014_checksum((uint8_t *)c, 4));

    rc2014_send_complete();
}

/**
 * rc2014 Write command
 * Write # of bytes specified by 'aux1' from tx_buffer from the rc2014.
 * 'aux2' for Atari 822 printer might be 0 or 1 in graphics mode
 */
void rc2014Printer::write()
{
    Debug_printf("rc2014Printer::write()\n");
    _lastaux1 = cmdFrame.aux1;
    _lastaux2 = cmdFrame.aux2;
    _last_ms = fnSystem.millis();

    rc2014_send_ack();

    uint16_t num_bytes = cmdFrame.aux1;

    memset(_buffer, 0, sizeof(_buffer)); // clear _buffer
    rc2014_recv_buffer(_buffer, num_bytes);

    // Copy the data to the printer emulator's buffer
    memcpy(_pptr->provideBuffer(), _buffer, num_bytes);
    rc2014_send_ack();

    _last_ms = fnSystem.millis();

    if (_pptr->process(num_bytes, _lastaux1, _lastaux2)) {
        rc2014_send_complete();
    } else {
        rc2014_send_error();
    }
}

void rc2014Printer::ready()
{
    rc2014_send_ack();
}

void rc2014Printer::stream()
{
    Debug_printf("rc2014Printer::stream()\n");

    rc2014_send_ack();
    rc2014Bus.streamDeactivate();
    rc2014Bus.streamDevice(_devnum);
    rc2014_send_complete();
}

void rc2014Printer::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case 'W':
        write();
        break;
    case 'S':
        status();
        break;
    case 'X':
        stream();
        break;
    default:
        Debug_printf("rc2014 process: unimplemented command: 0x%02x", cmdFrame.comnd);
    }
}

void rc2014Printer::rc2014_handle_stream()
{
    int num_bytes = rc2014_recv_available();

    if (num_bytes > 0) {
        int sioBytesRead = rc2014_recv_buffer(_buffer, 
                (num_bytes > TX_BUF_SIZE) ? TX_BUF_SIZE : num_bytes);

        // Copy the data to the printer emulator's buffer
        memcpy(_pptr->provideBuffer(), _buffer, sioBytesRead);

        _last_ms = fnSystem.millis();

        Debug_printf("rc2014Printer::rc2014_handle_stream(): bytes processing %d\n", num_bytes);
        _pptr->process(num_bytes, _lastaux1, _lastaux2);
    }

}

void rc2014Printer::shutdown()
{
}

void rc2014Printer::set_printer_type(printer_type printer_type)
{
    // Destroy any current printer emu object
    if (_pptr != nullptr)
    {
        delete _pptr;
    }

    _ptype = printer_type;
    switch (printer_type)
    {
    case PRINTER_FILE_RAW:
        _pptr = new filePrinter(RAW);
        break;
    case PRINTER_FILE_TRIM:
        _pptr = new filePrinter;
        break;
    case PRINTER_FILE_ASCII:
        _pptr = new filePrinter(ASCII);
        break;
    case PRINTER_ATARI_1020:
        _pptr = new atari1020;
        break;
    case PRINTER_ATARI_1025:
        _pptr = new atari1025;
        break;
    case PRINTER_EPSON:
        _pptr = new epson80;
        break;
    case PRINTER_EPSON_PRINTSHOP:
        _pptr = new epsonTPS;
        break;
    case PRINTER_OKIMATE10:
        _pptr = new okimate10;
        break;
    case PRINTER_PNG:
        _pptr = new pngPrinter;
        break;
    case PRINTER_HTML:
        _pptr = new htmlPrinter;
        break;
    case PRINTER_HTML_ATASCII:
        _pptr = new htmlPrinter(HTML_ATASCII);
        break;
    default:
        _pptr = new filePrinter;
        _ptype = PRINTER_FILE_TRIM;
        break;
    }

    _pptr->initPrinter(_storage);
    //_pptr->setEOL(0x0d);
}

#endif /* NEW_TARGET */
