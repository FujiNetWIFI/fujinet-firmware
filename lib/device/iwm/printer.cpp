#ifdef BUILD_APPLE
#include "printer.h"

#include "file_printer.h"
#include "html_printer.h"
#include "epson_80.h"

void iwmPrinter::send_status_reply_packet()
{
    uint8_t data[4];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = 0;

    IWM.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR_NOERROR, data, 4);
}

void iwmPrinter::send_extended_status_reply_packet()
{
    uint8_t data[5];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = data[4] = 0;

    IWM.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR_NOERROR, data, 5);
}

void iwmPrinter::send_status_dib_reply_packet()
{
    uint8_t data[25];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = 0;
    data[4] = 7;
    data[5] = 'P';
    data[6] = 'R';
    data[7] = 'I';
    data[8] = 'N';
    data[9] = 'T';
    data[10] = 'E';
    data[11] = 'R';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';
    data[21] = SP_TYPE_BYTE_FUJINET_PRINTER;
    data[22] = SP_SUBTYPE_BYTE_FUJINET_PRINTER;
    data[23] = 0x00;
    data[24] = 0x01;

    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

void iwmPrinter::send_extended_status_dib_reply_packet()
{
    uint8_t data[25];

    data[0] = 0b01110000;
    data[1] = data[2] = data[3] = 0;
    data[4] = 7;
    data[5] = 'P';
    data[6] = 'R';
    data[7] = 'I';
    data[8] = 'N';
    data[9] = 'T';
    data[10] = 'E';
    data[11] = 'R';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';
    data[21] = SP_TYPE_BYTE_FUJINET_PRINTER;
    data[22] = SP_SUBTYPE_BYTE_FUJINET_PRINTER;
    data[23] = 0x00;
    data[24] = 0x01;

    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

void iwmPrinter::iwm_status(iwm_decoded_cmd_t cmd)
{
    switch (get_status_code(cmd))
    {
        case IWM_STATUS_STATUS:
            send_status_reply_packet();
            return;
            break;
        case IWM_STATUS_DIB:
            send_status_dib_reply_packet();
            return;
            break;
    }
}

void iwmPrinter::iwm_open(iwm_decoded_cmd_t cmd)
{
    // Not sure what to put here yet.
}

void iwmPrinter::iwm_close(iwm_decoded_cmd_t cmd)
{
    // Not sure what to put here yet.
}

void iwmPrinter::iwm_write(iwm_decoded_cmd_t cmd)
{
    uint16_t num_bytes = get_numbytes(cmd);
    uint32_t addy = get_address(cmd); // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    
    Debug_printf("\nWrite %u bytes to address %04x\n", num_bytes);
    
    data_len = BLOCK_DATA_LEN;
    
    if (IWM.iwm_read_packet_timeout(100, (unsigned char *)data_buffer, data_len))
    {
        Debug_printf("\r\nTIMEOUT in read packet!");
        return;
    }

    if (data_len == -1)
    {
        iwm_return_ioerror();
        return;
    }

    uint16_t offset = 0;

    while (data_len > 0)
    {
        uint8_t l = (data_len > 80 ? 80 : data_len);
        memcpy(_pptr->provideBuffer(),&data_buffer[offset],l);
        _pptr->process(l,8,0);
        data_len -= l;
        offset += l;
    }

    send_reply_packet(SP_ERR_NOERROR);
}

void iwmPrinter::process(iwm_decoded_cmd_t cmd)
{
    switch (cmd.command)
    {
    case 0x00: // status
        iwm_status(cmd);
        break;
    case 0x06: // open
        iwm_open(cmd);
        break;
    case 0x07: // close
        iwm_close(cmd);
        break;
    case 0x09: // write
        iwm_write(cmd);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    }
}

void iwmPrinter::set_printer_type(iwmPrinter::printer_type printer_type)
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