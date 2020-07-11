#ifndef OKIMATE_10_H
#define OKIMATE_10_H

#include "atari_1025.h"

class okimate10 : public atari1025
{
protected:
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

    void esc_not_implemented();
    void cmd_not_implemented(uint8_t c);
    void reset_cmd();
    void print_7bit_gfx(uint8_t c);
    uint16_t okimate_cmd_ascii_to_int(uint8_t c);
    void set_mode(uint8_t m);
    void clear_mode(uint8_t m);
    void fprint_color_array(uint8_t font_mask);
    void okimate_handle_font(); // change typeface and/or color

    virtual void pdf_clear_modes() override;
    virtual void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Okimate 10"; };
};

#endif
