#ifndef COLECO_PRINTER_H
#define COLECO_PRINTER_H

#include "../../include/debug.h"

#include "printer.h"

#include "pdf_printer.h"

class adamBidiBuffer
{
private:
    char _line[80];
    uint8_t _column = 0;
    bool _backwards = false;
    bool isVT = true;

    void put_bs()
    {
        Debug_printf("BS\n");
        isVT = false;
        _column--;
    }

    void put_lf()
    {
        isVT = false;
    }

    void put_vt()
    {
    }

    void put_cr()
    {
        isVT = false;
        _column=0;
    }

    void put_default(char c)
    {
        isVT = false;
        _line[_column] = c;

        if (_backwards)
        {
            if (_column > 0)
            {
                _column--;
            }
        }
        else
        {
            if (_column < 80)
            {
                _column++;
            }
        }
    }

    void put_reverse()
    {
        isVT = false;
        _backwards = true;
    }

    void put_normal()
    {
        isVT = false;
        _backwards = false;
    }

public:

    bool getIsVT()
    {
        return isVT;
    }

    void setIsVT(bool _isVT)
    {
        isVT = _isVT;
    }

    void reset()
    {
        _column = 0;
    }

    void clear()
    {
        memset(_line,0x20,sizeof(_line));
    }

    char *get()
    {
        return _line;
    }

    void put(char c)
    {
        switch (c)
        {
        case 0x08: // BS
            put_bs();
            break;
        case 0x0A: // LF
            put_lf();
            break;
        case 0x0B: // VT
            put_vt();
            break;
        case 0x0D: // CR
            put_cr();
            break;
        case 0x0E: // SO (TO REVERSE)
            put_reverse();
            break;
        case 0x0F: // SI (TO NORMAL)
            put_normal();
            break;
        case 0x7F: // DEL
            break;
        default:
            put_default(c);
            break;
        }

    }
};

class colecoprinter : public pdfPrinter
{
private:
    void line_output();

protected:
    adamBidiBuffer bdb;
   virtual void pdf_clear_modes() override {};
    void pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2);
    virtual void post_new_file() override;
public:
    const char *modelname()  override 
    { 
        #if BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_COLECO_ADAM];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_COLECO_ADAM];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };
};

#endif
