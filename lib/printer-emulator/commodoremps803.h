#ifndef _COMMODOREMPS803_H
#define _COMMODOREMPS803_H

#include "printer.h"

#include "pdf_printer.h"

class commodoremps803 : public pdfPrinter
{
protected:
    
// Tab Setting
//     the Print Head      CHR$(16); "nHnL"
// Repeat Graphic
//     Selected            CHRS(26);CHR$(n);CHR$(Bit Image Data) from page 37
// Specify Dot Address     CHRS(27);CHR$(16);CHR$(nH)CHR$(nL)
    struct mps_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N[3] = {0,0,0};
        uint8_t ctr = 0;
    } mps_cmd;
    bool ctrlMode = false;

    struct mps_modes_t
    {
    // need some state variables
        bool enhanced = false;
        bool reverse = false;
        bool bitmap = false;
        bool local_char_set = false; 
        bool business_char_set = false;
    //      enhanced mode
    //      reverse mode
    //      bitmap graphics mode
    //      local char set switch mode - see page 34 or chr$(17) and chr$(145)
    } mps_modes;

    virtual void pdf_clear_modes() override;
    virtual void post_new_file() override;
    void mps_set_font(uint8_t F);
    void mps_update_font();
    void pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2) override; // need a custom one to handle sideways printing
    void reset_cmd();
    void mps_print_bitmap(uint8_t c);
public:
    const char *modelname()  override 
    { 
        #ifdef BUILD_IEC
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_COMMODORE_MPS803];
        #elif BUILD_ATARI
            return sioPrinter::printer_model_str[sioPrinter::PRINTER_ATARI_820];
        #elif BUILD_CBM
            return iecPrinter::printer_model_str[iecPrinter::PRINTER_ATARI_820];
        #elif BUILD_ADAM
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_820];
        #elif NEW_TARGET
            return adamPrinter::printer_model_str[adamPrinter::PRINTER_ATARI_820];
        #else
            return PRINTER_UNSUPPORTED;
        #endif
    };
};

#endif // _COMMODOREMPS803_H