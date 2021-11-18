#ifdef BUILD_ADAM

#include "../../include/atascii.h"
#include "printer.h"
#include <deque>

#include "file_printer.h"
#include "html_printer.h"
#include "svg_plotter.h"
#include "epson_80.h"
#include "epson_tps.h"
#include "okimate_10.h"
#include "png_printer.h"
#include "coleco_printer.h"

static std::deque<uint8_t> dq;
static std::deque<uint8_t> dq_b;

static unsigned long _t;

void vColecoPrinterTask(void *pvParameter)
{
    char numBytes = 0;

    adamPrinter *p = (adamPrinter *)pvParameter;

    while (1)
    {
        if (fnSystem.millis() - _t > 100)
        {
            if (!dq.empty())
            {
                for (uint8_t i = 0; i < 40; i++)
                {
                    if (!dq.empty())
                    {
                        p->getPrinterPtr()->provideBuffer()[i] = dq.front();
                        dq.pop_front();
                    }
                    numBytes = i;
                }
                p->getPrinterPtr()->process(numBytes, 0, 0);
                numBytes = 0;
            }
        }
        vTaskDelay(10);
    }
}

// Constructor just sets a default printer type
adamPrinter::adamPrinter(FileSystem *filesystem, printer_type print_type)
{
    _storage = filesystem;
    set_printer_type(print_type);
    xTaskCreatePinnedToCore(vColecoPrinterTask, "colprint", 4096, this, 1, &ioTask, 0);
}

adamPrinter::~adamPrinter()
{
    vTaskDelete(ioTask);
    delete _pptr;
}

adamPrinter::printer_type adamPrinter::match_modelname(std::string model_name)
{
    const char *models[PRINTER_INVALID] =
        {
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "ADAM Printer",
            "Epson 80",
            "Epson PrintShop",
            "HTML printer",
            "HTML ATASCII printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}

void adamPrinter::adamnet_control_status()
{
    uint8_t c[6] = {0x82, 0x10, 0x00, 0x00, 0x00, 0x10};

    adamnet_send_buffer(c, sizeof(c));
}

void adamPrinter::adamnet_control_send()
{
    unsigned short s = adamnet_recv_length();

    for (unsigned short i = 0; i < s; i++)
    {
        uint8_t b = adamnet_recv();

        if (b == 0x0e)
        {
            _backwards = true;
        }
        else if (b == 0x0f)
        {
            _backwards = false;
            if (!dq_b.empty())
            {
                while (!dq_b.empty())
                {
                    dq.push_back(dq_b.front());
                    dq_b.pop_front();
                }
            }
        }
        else if (_backwards == true)
            dq_b.push_front(b);
        else
            dq.push_back(b);
    }

    fnSystem.delay_microseconds(220);
    adamnet_send_buffer((uint8_t *)"\x92", 1); // ACK
}

void adamPrinter::adamnet_control_ready()
{
    fnSystem.delay_microseconds(80);
    adamnet_send_buffer((uint8_t *)"\x92", 1); // ACK
}

void adamPrinter::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    _t = fnSystem.millis();

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