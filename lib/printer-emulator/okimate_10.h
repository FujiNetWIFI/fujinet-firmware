#ifndef OKIMATE_10_H
#define OKIMATE_10_H

#include "atari_1025.h"

class okimate10 : public atari1025
{
protected:

    // 10 CPI, 17 CPI, 5 CPI, 8.5 CPI
    const double font_widths[4] = {
        100.,                  // standard
        100. * 80. / 136.,     // compressed
        2. * 100.,             // wide
        2. * 100. * 80. / 136. // bold
    };

    struct okimate_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t n = 0;
        uint8_t data = 0;
        uint16_t ctr = 0;
    } okimate_cmd;
    bool cmdMode = false;
    bool escMode = false;

    uint8_t color_buffer[480][4];
    uint16_t color_counter;
    const uint8_t invalid_font = 0xff;
    const uint8_t skip_me = 0xf7; // gfx pos is 0 for 0x90 h-tab command
    
    const uint8_t fnt_compressed = 0x01;
    const uint8_t fnt_expanded = 0x02;
    const uint8_t fnt_inverse = 0x04;
    const uint8_t fnt_gfx = 0x08;
    const uint8_t fnt_C = 0x10;
    const uint8_t fnt_M = 0x20;
    const uint8_t fnt_Y = 0x40;
    const uint8_t fnt_K = 0x80;

    uint8_t okimate_current_fnt_mask = 0x80; // black normal typeface
    uint8_t okimate_new_fnt_mask = 0x80;     // black normal typeface
    uint8_t okimate_old_fnt_mask = 0x80; 

    void esc_not_implemented();
    void cmd_not_implemented(uint8_t c);
    void reset_cmd();
    void print_7bit_gfx(uint8_t c);
    uint16_t okimate_cmd_ascii_to_int(uint8_t c);
    void set_mode(uint8_t m);
    void clear_mode(uint8_t m);
    void fprint_color_array(uint8_t font_mask);
    void okimate_set_char_width();
    void okimate_handle_font(); // change typeface and/or color
    void okimate_output_color_line(); 
    void okimate_init_colormode();
    void okimate_next_color();
    
    virtual void pdf_clear_modes() override;
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Okimate 10"; };
};

#endif

/** exposition on color mode
 * 
 * the three color ribbon passes Y-M-C are buffered in an array
 * i did not support mixed spaces and horizontal positioning between passes 
 * i assumed one would always follow the same pattern of text and graphics between passes
 * the color demo on D:LEARN does not do this
 * 
 * so ...
 * 
 * the color arrays will always be 480 dot indicies
 * letters will be allowed to start at any dot index
 * there will be some sort of padding between letters
 * padding method options
 * option 1: make padding GFX spaces, back up after a letter is printed by charWidth-1.2 pts (need to figure out math for font coordinate system)
 * option 2: make padding some special code (not FF, maybe FE?) that the loop just skips over. Need to pad out to charWidth in dot spaces.
 * 
 * option 2 makes more sense
 * if receive a char, insert it into current dot position. Determine dot width=charWidth/1.2
 **/