#include "svg_plotter.h"

#include "../../include/debug.h"
#include "../../include/atascii.h"


void svgPlotter::svg_update_bounds()
{
    if (svg_Y < svg_Y_min)
        svg_Y_min = svg_Y;
    if (svg_Y > svg_Y_max)
        svg_Y_max = svg_Y;
}

int svgPlotter::svg_compute_weight(double fsize)
{
    if (fsize < 20)
        return 700;
    if (fsize < 30)
        return 500;
    if (fsize < 90)
        return 400;
    return 300;
}

void svgPlotter::svg_new_line()
{
    // http://scruss.com/blog/2016/04/23/fifteentwenty-commodore-1520-plotter-font/
    // <text x="0" y="15" fill="red">I love SVG!</text>
    // position new line and start text string array
    if (svg_home_flag)
    {
        svg_text_y_offset = -lineHeight;
        svg_home_flag = false;
    }
    svg_Y += lineHeight;
    svg_update_bounds();
    //svg_X = 0; // always start at left margin? not sure of behavior
    int fontWeight = svg_compute_weight(fontSize);
    fprintf(_file, "<text x=\"%g\" y=\"%g\" ", svg_X, svg_Y + svg_text_y_offset);
    fprintf(_file, "font-size=\"%g\" font-family=\"FifteenTwenty\" ", fontSize);
    fprintf(_file, "font-weight=\"%d\" ", fontWeight);
    fprintf(_file, "fill=\"%s\">", svg_colors[svg_color_idx].c_str());

    BOLflag = false;
}

void svgPlotter::svg_end_line()
{
    // <text x="0" y="15" fill="red">I love SVG!</text>
    fprintf(_file, "</text>\n"); // close the line
    svg_X = 0.;                  // CR
    BOLflag = true;
}

void svgPlotter::svg_plot_line(double x1, double x2, double y1, double y2)
{
    double dash = (double)svg_line_type;
    fprintf(_file, "<line ");
    fprintf(_file, "stroke=\"%s\" ", svg_colors[svg_color_idx].c_str());
    fprintf(_file, "stroke-width=\"1.5\" stroke-linecap=\"round\" ");
    fprintf(_file, "stroke-dasharray=\"%g,%g\" ", dash, dash);
    fprintf(_file, "x1=\"%g\" x2=\"%g\" y1=\"%g\" y2=\"%g\" ", x1, x2, y1, y2);
    fprintf(_file, "/>\n");
}

void svgPlotter::svg_abs_plot_line()
{
    //<line x1="0" x2="100" y1="0" y2="100" style="stroke:rgb(0,0,0);stroke-width:2 />
    double x1 = svg_X;
    double y1 = svg_Y;
    double x2 = x1;
    double y2 = y1;
    if ((double)svg_arg[0] > -1000 && (double)svg_arg[0] < 1000)
        x2 = svg_X_home + (double)svg_arg[0];
    if ((double)svg_arg[1] > -1000 && (double)svg_arg[1] < 1000)
        y2 = svg_Y_home + (double)svg_arg[1]; // the modulo is not right
    svg_X = x2;
    svg_Y = y2;
#ifdef DEBUG
    Debug_printf("abs line: x1=\"%g\" x2=\"%g\" y1=\"%g\" y2=\"%g\" \n", x1, x2, y1, y2);
#endif
    svg_update_bounds();
    svg_plot_line(x1, x2, y1, y2);
}

void svgPlotter::svg_rel_plot_line()
{
    //<line x1="0" x2="100" y1="0" y2="100" style="stroke:rgb(0,0,0);stroke-width:2 />
    double x1 = svg_X;
    double y1 = svg_Y;
    double x2 = x1 + (double)svg_arg[0];
    double y2 = y1 + (double)svg_arg[1];
    svg_X = x2;
    svg_Y = y2;
    svg_update_bounds();
    svg_plot_line(x1, x2, y1, y2);
}

void svgPlotter::svg_set_text_size(int s)
// 0 for 80 col, 1 for 40 col, 3 for 20 col
/*
Text scaling is shown in Atari 1020 Field Service Manual in Fig.3-2 on Page 3-5.
Using the PDF measuring tool in Acrobat Reader, I find the following character sizes:
S(scale)  Actual (inches)
0         cannot tell but looks like 80 column mode
31        1.5
63        3.0
64        0.1 - which is 40 column mode. Why is 64 showing 40 column mode when 0 looks likes 80 col mode?

Formula for fontSize and charWidth vs. scale setting:
80 column mode is fontSize = 10.4 and charWidth = 6
fontSize/charWidth = 1.7333...

lets scale against 63 (3 inches) and 40 column mode (0.1 inches)
so charWidth = 12 is equivalent to S=0.1 inches, which is 30 times smaller than 3.0 inhces 
scale of 63 would be charWidth = 12 * 30 = 360 which is 360/480 = 75% of the printWidth - let's say OK including whitespace
I want charWidth = 6 for s=0 and =360 for s=63
*/
{
    double scale = 6. * (double)(s & 63);
    charWidth = 6. + scale;
    fontSize = 10.4 * charWidth / 6.0;
    lineHeight = fontSize;
}

void svgPlotter::svg_handle_char(unsigned char c)
{
    if (escMode)
    {
        // Atari 1020 escape codes:
        // ESC CTRL-G - graphics mode (simple A returns)
        // ESC CTRL-P - 20 char mode
        // ESC CTRL-N - 40 char mode
        // ESC CTRL-S - 80 char mode
        // ESC CTRL-W - international char set
        // ESC CTRL-X - standard char set
        switch (c)
        {
        case 7:
            textMode = false;
            escMode = false;
#ifdef DEBUG
            Debug_printf("\nentering GRAPHICS mode!\n");
#endif
            return; // short circuit text mode logic
        case 14:
            svg_set_text_size(1);
            break;
        case 16:
            svg_set_text_size(3);
            break;
        case 19:
            svg_set_text_size(0);
            break;
        case 23:
        // set international flag
        case 24:
            // set standard flag
        default:
            break;
        }

        escMode = false;
        escResidual = true;
    }
    else if (c == 27)
        escMode = true;
    else if (c > 31 && c < 127) // simple ASCII printer
    {
        // what characters need to be escaped in SVG text?
        // if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
        //   _file->write(BACKSLASH);
        //     <	less than	&lt;	&#60;
        // >	greater than	&gt;	&#62;
        // &	ampersand	&amp;	&#38;
        // "	double quotation mark	&quot;	&#34;
        // '	single quotation mark (apostrophe)	&apos;	&#39;
        if (c == '<' || c == '>' || c == '&' || c == '\'' || c == '\"')
            fprintf(_file, "&#%2d;", c);
        else if (c == ' ')
            fprintf(_file, "&#160;");
        else
            fputc(c, _file); //_file->write(c);
        switch (svg_rotate)  // update x position - PUT Q ROTATION HERE
        {
        case 0:
            svg_X += charWidth;
            break;
        case 90:
            svg_Y += charWidth;
            svg_update_bounds();
            break;
        case 180:
            svg_X -= charWidth;
            break;
        case 270:
            svg_Y -= charWidth;
            svg_update_bounds();
            break;
        default:
            svg_X += charWidth;
            break;
        }
        //std::cout << svg_X << " ";
    }
}

/*
https://www.atarimagazines.com/v4n10/atari1020plotter.html

INSTRUCTION		FORM			        MODE

GRAPHICS		  ESC ESC CTRL G		-
TEXT			    DEFAULT			      AT CHANNEL OPENING
TEXT			    A			            TEXT FROM GR.
20 COL. TEXT	ESC ESC CTRL P		TEXT
40 COL. TEXT	ESC ESC CTRL N		TEXT
80 COL. TEXT	ESC ESC CTRL S		TEXT
HOME			    H			            GRAPHICS
PEN COLOR		  C (VALUE 0-3)		  GRAPHICS
0	Black
1	Blue 
2	Green
3	Red  
LINE TYPE		  L (VALUE 1-15)		GRAPHICS
0=SOLID		  	-		            	-
DRAW			    DX,Y			        GRAPHICS
MOVE			    MX,Y			        GRAPHICS
ROTATE TEXT		Q(0-3)			      GRAPHICS
(Text to be rotated must start with P)
PRINT TEXT    P(any text)       GRAPHICS
INITIALIZE		I			            GRAPHICS
(Sets current X,Y as HOME or 0,0)
RELATIVE DRAW	JX,Y			        GRAPHICS
(Used with Init.)
RELATIVE MOVE	RX,Y			        GRAPHICS
(Used with Init.)
CHAR. SCALE		S(0-63)			      GRAPHICS
*/

void svgPlotter::svg_put_text(std::string S)
{
    int fontWeight = svg_compute_weight(fontSize);
    fprintf(_file, "<text x=\"%g\" y=\"%g\" ", svg_X, svg_Y);
    fprintf(_file, "font-size=\"%g\" font-family=\"FifteenTwenty\" font-weight=\"%d\" fill=\"%s\" ", fontSize, fontWeight, svg_colors[svg_color_idx].c_str());
    fprintf(_file, "transform=\"rotate(%d %g,%g)\">", svg_rotate, svg_X, svg_Y);
    for (int i = 0; i < S.length(); i++)
    {
        svg_handle_char((unsigned char)S[i]);
    }
    fprintf(_file, "</text>\n"); // close the line
}

void svgPlotter::svg_plot_axis()
{
}

void svgPlotter::svg_get_arg(std::string S, int n)
{
    svg_arg[n] = atoi(S.c_str());
#ifdef DEBUG
    Debug_printf(" (arg %d : %d)\n", n, svg_arg[n]);
#endif
}

void svgPlotter::svg_get_2_args(std::string S)
{
    size_t n = S.find_first_of(',');
    svg_get_arg(S.substr(0, n), 0);
    svg_get_arg(S.substr(n + 1), 1);
    svg_arg[1] *= -1; // y-axis if flipped
}

void svgPlotter::svg_get_3_args(std::string S)
{
    size_t n1 = S.find_first_of(',');
    size_t n2 = S.find_first_of(',', n1 + 1);
    svg_get_arg(S.substr(0, n1 - 1), 0);
    svg_get_arg(S.substr(n1 + 1, n2 - n1), 1);
    svg_get_arg(S.substr(n2 + 1), 2);
}

void svgPlotter::svg_header()
{
    // <!DOCTYPE html>
    // <html>
    // <body>
    // <svg height="210" width="500">

    //<svg version="1.1"
    //  baseProfile="full"
    //  width="300" height="200"
    //  xmlns="http://www.w3.org/2000/svg">

    //fprintf(_file,"<!DOCTYPE html>\n");
    //fprintf(_file,"<html>\n");
    //fprintf(_file,"<body>\n\n");
    fprintf(_file, "<svg version=\"1.1\" height=\"");
    svg_filepos[0] = ftell(_file);
    fprintf(_file, " 400.0mm\" width=\"114mm\" style=\"background-color:white\" ");
    fprintf(_file, "viewBox=\"0 ");
    svg_filepos[1] = ftell(_file);
    fprintf(_file, " -1000 480 ");
    svg_filepos[2] = ftell(_file);
    fprintf(_file, "  2000\" xmlns=\"http://www.w3.org/2000/svg\">\n");
    svg_home_flag = true;
}

void svgPlotter::svg_footer()
{
    size_t here = ftell(_file);
    // go back and rewrite the Y extent
    fseek(_file, svg_filepos[0], 0);
    fprintf(_file, "%6.1f", 20. + (svg_Y_max - svg_Y_min) * 96. / 480.);
    fseek(_file, svg_filepos[1], 0);
    fprintf(_file, "%6d", (int)(svg_Y_min)-50);
    fseek(_file, svg_filepos[2], 0);
    fprintf(_file, "%6d", 100 + (int)(svg_Y_max - svg_Y_min));

    // close out svg file
    fseek(_file, here, 0);
    // </svg>
    // </body>
    // </html>
    if (!BOLflag)
        svg_end_line();
    fprintf(_file, "</svg>\n\n");
    //fprintf(_file,"</body>\n");
    //fprintf(_file,"</html>\n");
}

void svgPlotter::graphics_command(int n)
{
    // parse the graphics command
    // graphics mode commands start with a single LETTER
    // commands have 0, 1 or 2 arg's
    // - or X has 3 args and "P" is followed by a string
    // commands are terminated with a "*"" or EOL
    // a ";" after the args repeats the command CHAR
    //
    // could maybe use regex but going to brute force with a bunch of cases

    std::string S;
    S.assign((char *)buffer, n);

    size_t cmd_pos = 0;
    do
    {
        unsigned char c = buffer[cmd_pos];
        switch (c)
        {
        case 'A': // return to TEXTMODE
            textMode = true;
            svg_X = 0.; //CR
            return;     // get outta here!
        case 'C':       // SELECT COLOR
            // get arg out of S and assign to...
            svg_get_arg(S.substr(cmd_pos + 1), 0);
            svg_color_idx = svg_arg[0];
            break;
        case 'D': // DRAW LINE ABS COORDS
            // get 2 args out of S and draw a line
            svg_get_2_args(S.substr(cmd_pos + 1));
            svg_abs_plot_line();
            break;
        case 'H': // GO HOME
            svg_X = svg_X_home;
            svg_Y = svg_Y_home;
            svg_update_bounds();
            break;
        case 'I': // SET HOME HERE
            svg_X_home = svg_X;
            svg_Y_home = svg_Y;
            svg_home_flag = true;
            break;
        case 'J': // DRAW LINE RELATIVE COORDS
            svg_get_2_args(S.substr(cmd_pos + 1));
            svg_rel_plot_line();
            break;
        case 'L': // SET DASHED LINE TYPE
            // get arg out of S and assign to...
            svg_get_arg(S.substr(cmd_pos + 1), 0);
            svg_line_type = svg_arg[0] & 15;
            break;
        case 'M': // MOVE ABS COORDS
            // get 2 args out of S and ...
            // this behavior when out of bounds is a guess
            // i bet it's don't change it
            svg_get_2_args(S.substr(cmd_pos + 1));
            if (svg_arg[0] > -1000 && svg_arg[0] < 1000) // probably >-1000 && <1000
                svg_X = svg_X_home + (double)svg_arg[0];
            if (svg_arg[1] > -1000 && svg_arg[1] < 1000)
                svg_Y = svg_Y_home + (double)svg_arg[1]; // probably >-1000 && <1000
            svg_update_bounds();
            break;
        case 'P': // PUT TEXT HERE
            svg_put_text(S.substr(cmd_pos + 1));
            break;
        case 'Q': // SET TEXT ROTATION
            svg_get_arg(S.substr(cmd_pos + 1), 0);
            svg_rotate = svg_arg[0] * 90;
            break;
        case 'R': // MOVE RELATIVE COORDS
            svg_get_2_args(S.substr(cmd_pos + 1));
            svg_X = svg_X + (double)svg_arg[0];
            svg_Y = svg_Y + (double)svg_arg[1];
            svg_update_bounds();
            break;
        case 'S': // SET TEXT SIZE
            svg_get_arg(S.substr(cmd_pos + 1), 0);
            svg_set_text_size(svg_arg[0]);
            break;
        case 'X': // DRAW GRAPH AXIS
            svg_get_3_args(S.substr(cmd_pos + 1));
            svg_plot_axis();
            break;
        default:
            return;
        }
        // find either ':' to repeat the command
        // or '*' to start a new command
        size_t new_pos = S.find_first_of(":*", cmd_pos + 1);
        if (new_pos == std::string::npos)
            return;
        if (S[new_pos] == ':')
            S[new_pos] = S[cmd_pos]; // repeat command - just copy command over
        else if (S[new_pos] == '*')
            new_pos++; // new command so go to next char to get command
        cmd_pos = new_pos;
    } while (true);
}

bool svgPlotter::process_buffer(uint8_t n, uint8_t aux1, uint8_t aux2)
//void svg_add(int n) // prototype expected n to be length up to including EOL
// but here n==40
{
    uint8_t new_n = 0;
    while (buffer[new_n++] != ATASCII_EOL && new_n < n)
    {
#ifdef DEBUG
        Debug_printf("%c", buffer[new_n - 1]);
#endif
    }
    //new_n++;
#ifdef DEBUG
    Debug_printf(" : End of buffer, char %x at %d\n", buffer[new_n - 1], new_n - 1);
#endif

    // looks like escape codes take you out of GRAPHICS MODE
    if (buffer[0] == 27)
    {
        textMode = true;
        svg_X = 0.;
#ifdef DEBUG
        Debug_printf("Text Mode!\n");
#endif
    }
    if (!textMode)
        graphics_command(new_n);
    else
    {
        // loop through string
        for (int i = 0; i < new_n; i++)
        {
            uint8_t c = buffer[i];

            // clear out residual EOL from ESC sequence
            if (escResidual)
            {
                escResidual = false;
                if (c == ATASCII_EOL)
                    return true;
            }
            // the following creates a blank line of text
            if (BOLflag && c == ATASCII_EOL)
            { // svg_new_line();
                svg_X = 0.;
                svg_Y += lineHeight;
                svg_update_bounds();
                return true;
            }
            // check for EOL or if at end of line and need automatic CR
            if (!BOLflag && c == ATASCII_EOL)
            {
                svg_end_line();
                return true;
            }
            if (!BOLflag && (svg_X > (printWidth - charWidth)))
            {
                svg_end_line();
                svg_new_line();
            } // or do I just need to start a new line of text
            else if (BOLflag && !escMode)
            {
                if (c == 32)
                {
                    svg_X += charWidth;
                    continue;
                }
                if (c != 27)
                    svg_new_line();
            } // disposition the current byte
            svg_handle_char(c);
            if (!textMode)
                return true;
        }
    }
    return true;
}

void svgPlotter::post_new_file()
{
    shortname = "a1020";

    // pageWidth = 612.0;
    // //pageHeight = 792.0;
    // leftMargin = 18.0;
    // //bottomMargin = 0;
    // printWidth = 576.0; // 8 inches
    // lineHeight = 12.0;
    // charWidth = 7.2;
    // //fontNumber = 1;
    // fontSize = 12;

    svg_header();
    //intlFlag = false;
    //escMode = false;
}

void svgPlotter::pre_close_file()
{
    svg_footer();
    //printer_emu::pageEject();
}
