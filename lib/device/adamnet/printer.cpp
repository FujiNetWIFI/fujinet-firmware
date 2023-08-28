#ifdef BUILD_ADAM

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

#define PRINTER_PRIORITY 10

constexpr const char *const adamPrinter::printer_model_str[PRINTER_INVALID];

void printerTask(void *param)
{
    printer_emu *p = (printer_emu *)param;

    while (true)
    {
        if (uxQueueMessagesWaiting(pxq))
        {
            PrintItem pi;

            fnLedManager.set(LED_BT,true);
            xQueueReceive(pxq,&pi,portMAX_DELAY);
            memcpy(p->provideBuffer(),&pi.buf,pi.len);
            p->process(pi.len,0,0);
            fnLedManager.set(LED_BT,false);
        }

        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}

// Constructor just sets a default printer type
adamPrinter::adamPrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);

    pxq = xQueueCreate(16,sizeof(PrintItem));

    xTaskCreate(printerTask, "ptsk", 4096, _pptr, PRINTER_PRIORITY, &thPrinter);
}

adamPrinter::~adamPrinter()
{
    if (thPrinter != nullptr)
        vTaskDelete(thPrinter);

    if (_pptr != nullptr)
        delete _pptr;

    vQueueDelete(pxq);
}

void adamPrinter::start_printer_task()
{
}

adamPrinter::printer_type adamPrinter::match_modelname(std::string model_name)
{
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(adamPrinter::printer_model_str[i]) == 0)
            break;

    return (printer_type)i;
}

void adamPrinter::adamnet_control_status()
{
    uint8_t c[6] = {0x82, 0x10, 0x00, 0x00, 0x00, 0x10};

    adamnet_send_buffer(c, sizeof(c));
}

void adamPrinter::perform_print()
{
}

void adamPrinter::adamnet_control_send()
{
    PrintItem pi;

    pi.len = adamnet_recv_length();
    adamnet_recv_buffer(pi.buf, pi.len);
    adamnet_recv(); // ck

    // AdamNet.start_time = esp_timer_get_time();
    adamnet_response_ack();

    xQueueSend(pxq,&pi,portMAX_DELAY);

    _last_ms = fnSystem.millis();
}

void adamPrinter::adamnet_control_ready()
{
    // AdamNet.start_time = esp_timer_get_time();

    if (!uxQueueSpacesAvailable(pxq))
    {
        adamnet_response_nack();
    }
    else   
        adamnet_response_ack();
}

void adamPrinter::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

void adamPrinter::shutdown()
{
}

void adamPrinter::set_printer_type(printer_type printer_type)
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
    case PRINTER_COLECO_ADAM:
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

#endif /* BUILD_ADAM */