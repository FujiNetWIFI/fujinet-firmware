#ifdef BUILD_LYNX

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

#define PRINTER_PRIORITY 9

constexpr const char *const lynxPrinter::printer_model_str[PRINTER_INVALID];

struct _printItem
{
    uint8_t len;
    uint8_t buf[16];
} pi;

bool need_print = false;

void printerTask(void *param)
{
    lynxPrinter *p = (lynxPrinter *)param;

    while(1)
    {
        if (need_print == true)
        {
            fnLedManager.set(LED_BT,true);
            p->perform_print();
            fnLedManager.set(LED_BT,false);
            need_print=false;
        }

    vTaskDelay(1);

    }
}

// Constructor just sets a default printer type
lynxPrinter::lynxPrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);

    getPrinterPtr()->setEOLBypass(true);
    getPrinterPtr()->setTranslate850(false);
    getPrinterPtr()->setEOL(0x0D);

    xTaskCreate(printerTask, "ptsk", 4096, this, PRINTER_PRIORITY, &thPrinter);
}

lynxPrinter::~lynxPrinter()
{
    if (thPrinter != nullptr)
        vTaskDelete(thPrinter);

    if (_pptr != nullptr)
        delete _pptr;
}

void lynxPrinter::start_printer_task()
{
}

lynxPrinter::printer_type lynxPrinter::match_modelname(std::string model_name)
{
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(lynxPrinter::printer_model_str[i]) == 0)
            break;

    return (printer_type)i;
}

void lynxPrinter::comlynx_control_status()
{
    uint8_t c[6] = {0x82, 0x10, 0x00, 0x00, 0x00, 0x10};

    comlynx_send_buffer(c, sizeof(c));
}

void lynxPrinter::perform_print()
{
    memcpy(_pptr->provideBuffer(),pi.buf,pi.len);
    _pptr->process(pi.len,0,0);
}

void lynxPrinter::comlynx_control_send()
{
    memset(&pi,0,sizeof(pi));
    pi.len = comlynx_recv_length();
    comlynx_recv_buffer(pi.buf, pi.len);
    comlynx_recv(); // ck

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    need_print=true;

    _last_ms = fnSystem.millis();
}

void lynxPrinter::comlynx_control_ready()
{
    ComLynx.start_time = esp_timer_get_time();

    if (need_print==true)
        comlynx_response_nack();
    else
        comlynx_response_ack();
}

void lynxPrinter::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }
}

void lynxPrinter::shutdown()
{
}

void lynxPrinter::set_printer_type(printer_type printer_type)
{
    // Destroy any current printer emu object
    delete _pptr;

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
    case PRINTER_COLECO_LYNX:
        _pptr = new colecoprinter;
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
}

#endif /* BUILD_LYNX */