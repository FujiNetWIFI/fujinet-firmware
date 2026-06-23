#ifdef BUILD_APPLE
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "epson_80.h"
#include "fnSystem.h"
#include "../../hardware/led.h"

constexpr const char *const iwmPrinter::printer_model_str[PRINTER_INVALID];

iwmPrinter::iwmPrinter(FileSystem *filesystem, printer_type printer_type)
{
    _storage = filesystem;
    set_printer_type(printer_type);
}

iwmPrinter::~iwmPrinter()
{
    delete _pptr;
    _pptr = nullptr;
}

void iwmPrinter::send_status_reply_packet()
{
    uint8_t data[4];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = 0;

    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR::NOERROR, data, 4);
}

void iwmPrinter::send_extended_status_reply_packet()
{
    uint8_t data[5];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = data[4] = 0;

    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR::NOERROR, data, 5);
}

void iwmPrinter::send_status_dib_reply_packet()
{
    Debug_printf("\r\nPRINTER: Sending DIB reply\r\n");
    std::vector<uint8_t> data = create_dib_reply_packet(
        "PRINTER",                                                          // name
        0b01110000,                                                         // status
        { 0, 0, 0 },                                                        // block size
        { SP_TYPE_BYTE_FUJINET_PRINTER, SP_SUBTYPE_BYTE_FUJINET_PRINTER },  // type, subtype
        { 0x00, 0x01 }                                                      // version.
    );
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR::NOERROR, data.data(), data.size());
}

void iwmPrinter::send_extended_status_dib_reply_packet()
{
    send_status_dib_reply_packet();
}

void iwmPrinter::iwm_status(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\n[PRINTER]: Device: %02x Status Code %02x\r\n", id(), cmd.control_status.fuji.command);
    switch (cmd.control_status.code)
    {
    case SP_STAT_DEVICE:
        send_status_reply_packet();
        break;
    case SP_STAT_DIB:
        send_status_dib_reply_packet();
        break;
    default:
      send_reply_packet(SP_ERR::BADCMD);
      break;
    }
}

void iwmPrinter::iwm_open(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\nPrinter: Open\n");
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmPrinter::iwm_close(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\nPrinter: Close\n");
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmPrinter::iwm_write(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\nPrinter: Write %u bytes\n", cmd.char_rw.length);

    SYSTEM_BUS.iwm_decode_data_packet((unsigned char *)data_buffer, data_len);

    if (data_len == -1)
    {
        iwm_return_ioerror();
        return;
    }

    uint16_t offset = 0;

    while (data_len > 0)
    {
        uint8_t l = (data_len > 80 ? 80 : data_len);
        memcpy(_pptr->provideBuffer(), &data_buffer[offset], l);
        _pptr->process(l, 8, 0);
        data_len -= l;
        offset += l;
    }

    _last_ms = fnSystem.millis();
    send_reply_packet(SP_ERR::NOERROR);
}

/**
 * Print from CP/M, which is one character...at...a...time...
 */
void iwmPrinter::print_from_cpm(uint8_t c)
{
    _pptr->provideBuffer()[_llen++]=c;

    if (c == 0x0D || c == 0x0a || _llen == 80)
    {
        _last_ms = fnSystem.millis();
        _pptr->process(_llen, 0, 0);
        _llen = 0;
    }
}

void iwmPrinter::set_printer_type(iwmPrinter::printer_type printer_type)
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
    case PRINTER_EPSON:
        _pptr = new epson80;
        break;
    case PRINTER_HTML:
        _pptr = new htmlPrinter;
        break;
    default:
        _pptr = new filePrinter;
        _ptype = PRINTER_FILE_RAW;
        break;
    }

    _pptr->initPrinter(_storage);
}

iwmPrinter::printer_type iwmPrinter::match_modelname(std::string model_name)
{
    const char *models[PRINTER_INVALID] =
        {
            "file printer (RAW)",
            "file printer (TRIM)",
            "file printer (ASCII)",
            "Epson 80",
            "HTML printer"};
    int i;
    for (i = 0; i < PRINTER_INVALID; i++)
        if (model_name.compare(models[i]) == 0)
            break;

    return (printer_type)i;
}
#endif /* BUILD_APPLE */
