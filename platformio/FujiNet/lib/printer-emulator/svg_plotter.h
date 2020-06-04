#ifndef SVG_PLOTTER_H
#define SVG_PLOTTER_H

/* This is really a virtual class, as it's not meant to be instantiated on its own, but
 inherited from by other, full-fledged printer classes (e.g. Atari 820/822)
*/
#include <string>

#include "printer_emulator.h"
#include "../../include/atascii.h"

class svgPlotter : public printer_emu
{
protected:
    bool BOLflag = true;
    float svg_X = 0.;
    float svg_Y = 0.;
    float svg_Y_min = -100;
    float svg_Y_max = 100;
    size_t svg_filepos[3];
    float svg_X_home = 0.;
    float svg_Y_home = 0.;
    float svg_text_y_offset = 0.;
    float pageWidth = 550.;
    float printWidth = 480.;
    double leftMargin = 0.0;
    float charWidth = 12.;
    float lineHeight = 20.8;
    float fontSize = 20.8;
    int svg_rotate = 0;
    //float svg_gfx_fontsize;
    //float svg_gfx_charWidth;
    int svg_color_idx = 0;
    std::string svg_colors[4] = {"Black", "Blue", "Green", "Red"};
    int svg_line_type = 0;
    int svg_arg[3] = {0, 0, 0};

    bool escMode = false;
    bool escResidual = false;
    bool textMode = true;
    bool svg_home_flag = true;

    std::string shortname;

    void svg_update_bounds();
    int svg_compute_weight(float fsize);
    void svg_new_line();
    void svg_end_line();
    void svg_plot_line(float x1, float x2, float y1, float y2);
    void svg_abs_plot_line();
    void svg_rel_plot_line();
    void svg_set_text_size(int s);
    void svg_put_text(std::string S);
    void svg_plot_axis();
    void svg_get_arg(std::string S, int n);
    void svg_get_2_args(std::string S);
    void svg_get_3_args(std::string S);
    void svg_header();
    void svg_footer();

    void graphics_command(int n);

    void svg_handle_char(unsigned char c);                                    //virtual void svg_handle_char(unsigned char c) = 0; //     virtual void pdf_handle_char(byte c, byte aux1, byte aux2) = 0;
    virtual bool process_buffer(byte linelen, byte aux1, byte aux2) override; //void svg_add(int n);
    virtual void pre_close_file() override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Atari 1020"; };
    svgPlotter() { _paper_type = SVG; };
};

#endif // guard
