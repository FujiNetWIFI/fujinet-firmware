#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <iomanip>
#include <iostream>
#include <fstream>
//using namespace std;

#define EOL 155
#define EOLS -101

// todo:
// specify: page size, left/bottom margins, line spacing
// sio simulation: read input file, make 40-char buffers, replace CRLF with EOL, pad with spaces to 40 char
// using simulated sio buffer to create text stream payload for PDF lines
// use standard C and POSIX file output (don't care about input because that's SIO simulation)
// replace page(string) with incremental line string and accumulate lengths for pdf_xref
// use NUMLINES to parameterize the routine

//std::ifstream prtin; // input file
unsigned char buffer[40];

FILE *f; // standard C output file

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

void svg_update_bounds()
{
  if (svg_Y < svg_Y_min)
    svg_Y_min = svg_Y;
  if (svg_Y > svg_Y_max)
    svg_Y_max = svg_Y;
}

int svg_compute_weight(float fsize)
{
  if (fsize < 20)
    return 700;
  if (fsize < 30)
    return 500;
  if (fsize < 90)
    return 400;
  return 300;
}

void svg_new_line()
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
  fprintf(f, "<text x=\"%g\" y=\"%g\" ", svg_X, svg_Y + svg_text_y_offset);
  fprintf(f, "font-size=\"%g\" font-family=\"FifteenTwenty\" ", fontSize);
  fprintf(f, "font-weight=\"%d\" ", fontWeight);
  fprintf(f, "fill=\"%s\">", svg_colors[svg_color_idx].c_str());

  BOLflag = false;
}

void svg_end_line()
{
  // <text x="0" y="15" fill="red">I love SVG!</text>
  fprintf(f, "</text>\n"); // close the line
  svg_X = 0;               // CR
  BOLflag = true;
}

void svg_plot_line(float x1, float x2, float y1, float y2)
{
  float dash = (float)svg_line_type;
  fprintf(f, "<line ");
  fprintf(f, "stroke=\"%s\" ", svg_colors[svg_color_idx].c_str());
  fprintf(f, "stroke-width=\"1.5\" stroke-linecap=\"round\" ");
  fprintf(f, "stroke-dasharray=\"%g,%g\" ", dash, dash);
  fprintf(f, "x1=\"%g\" x2=\"%g\" y1=\"%g\" y2=\"%g\" ", x1, x2, y1, y2);
  fprintf(f, "/>\n");
}

void svg_abs_plot_line()
{
  //<line x1="0" x2="100" y1="0" y2="100" style="stroke:rgb(0,0,0);stroke-width:2 />
  float x1 = svg_X;
  float y1 = svg_Y;
  float x2 = svg_X_home + svg_arg[0];
  float y2 = svg_Y_home + svg_arg[1] % 1000;
  svg_X = x2;
  svg_Y = y2;
  svg_update_bounds();
  svg_plot_line(x1, x2, y1, y2);
}

void svg_rel_plot_line()
{
  //<line x1="0" x2="100" y1="0" y2="100" style="stroke:rgb(0,0,0);stroke-width:2 />
  float x1 = svg_X;
  float y1 = svg_Y;
  float x2 = x1 + svg_arg[0];
  float y2 = y1 + svg_arg[1];
  svg_X = x2;
  svg_Y = y2;
  svg_update_bounds();
  svg_plot_line(x1, x2, y1, y2);
}

void svg_set_text_size(int s)
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
  float scale = 6. * (float)(s & 63);
  charWidth = 6. + scale;
  fontSize = 10.4 * charWidth / 6.0;
  lineHeight = fontSize;
}

void svg_handle_char(unsigned char c)
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
      std::cout << "\nentering GRAPHICS mode!\n";
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
      fprintf(f, "&#%2d;", c);
   else if (c == ' ')
      fprintf(f, "&nbsp;");
    else
      fputc(c, f);      //_file->write(c);
    switch (svg_rotate) // update x position - PUT Q ROTATION HERE
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

void svg_put_text(std::string S)
{
  int fontWeight = svg_compute_weight(fontSize);
  fprintf(f, "<text x=\"%g\" y=\"%g\" ", svg_X, svg_Y);
  fprintf(f, "font-size=\"%g\" font-family=\"FifteenTwenty\" font-weight=\"%d\" fill=\"%s\" ", fontSize, fontWeight, svg_colors[svg_color_idx].c_str());
  fprintf(f, "transform=\"rotate(%d %g,%g)\">", svg_rotate, svg_X, svg_Y);
  for (int i = 0; i < S.length(); i++)
  {
    svg_handle_char((unsigned char)S[i]);
  }
  fprintf(f, "</text>\n"); // close the line
}

void svg_get_arg(std::string S, int n)
{
  svg_arg[n] = atoi(S.c_str());
  std::cout << " (arg " << n << ": " << svg_arg[n] << ") ";
}

void svg_get_2_args(std::string S)
{
  size_t n = S.find_first_of(',');
  svg_get_arg(S.substr(0, n), 0);
  svg_get_arg(S.substr(n + 1), 1);
  svg_arg[1] *= -1; // y-axis if flipped
}

void svg_get_3_args(std::string S)
{
  size_t n1 = S.find_first_of(',');
  size_t n2 = S.find_first_of(',', n1 + 1);
  svg_get_arg(S.substr(0, n1 - 1), 0);
  svg_get_arg(S.substr(n1 + 1, n2 - n1), 1);
  svg_get_arg(S.substr(n2 + 1), 2);
}

void svg_graphics_command(int n)
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
      svg_X = 0; //CR
      return;    // get outta here!
    case 'C':    // SELECT COLOR
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
      svg_get_2_args(S.substr(cmd_pos + 1));
      if (svg_arg[0] != 1000)
        svg_X = svg_X_home + svg_arg[0];
      if (svg_arg[1] == -1000)
        svg_Y = svg_Y_home;
      else
        svg_Y = svg_Y_home + svg_arg[1];
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
      svg_X = svg_X + svg_arg[0];
      svg_Y = svg_Y + svg_arg[1];
      svg_update_bounds();
      break;
    case 'S': // SET TEXT SIZE
      svg_get_arg(S.substr(cmd_pos + 1), 0);
      svg_set_text_size(svg_arg[0]);
      break;
    case 'X': // DRAW GRAPH AXIS
      svg_get_3_args(S.substr(cmd_pos + 1));
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

void svg_header()
{
  // <!DOCTYPE html>
  // <html>
  // <body>
  // <svg height="210" width="500">

  //<svg version="1.1"
  //  baseProfile="full"
  //  width="300" height="200"
  //  xmlns="http://www.w3.org/2000/svg">

  //fprintf(f,"<!DOCTYPE html>\n");
  //fprintf(f,"<html>\n");
  //fprintf(f,"<body>\n\n");
  fprintf(f, "<svg version=\"1.1\" height=\"");
  svg_filepos[0] = ftell(f);
  fprintf(f, " 400.0mm\" width=\"114mm\" style=\"background-color:white\" ");
  fprintf(f, "viewBox=\"0 ");
  svg_filepos[1] = ftell(f);
  fprintf(f, " -1000 480 ");
  svg_filepos[2] = ftell(f);
  fprintf(f, "  2000\" xmlns=\"http://www.w3.org/2000/svg\">\n");
  svg_home_flag = true;
}

void svg_footer()
{
  size_t here = ftell(f);
  // go back and rewrite the Y extent
  fseek(f, svg_filepos[0], 0);
  fprintf(f, "%6.1f", 20. + (svg_Y_max - svg_Y_min) * 96. / 480.);
  fseek(f, svg_filepos[1], 0);
  fprintf(f, "%6d", (int)(svg_Y_min)-50);
  fseek(f, svg_filepos[2], 0);
  fprintf(f, "%6d", 100 + (int)(svg_Y_max - svg_Y_min));

  // close out svg file
  fseek(f, here, 0);
  // </svg>
  // </body>
  // </html>
  if (!BOLflag)
    svg_end_line();
  fprintf(f, "</svg>\n\n");
  //fprintf(f,"</body>\n");
  //fprintf(f,"</html>\n");
}

void svg_add(int n)
{
  // looks like escape codes take you out of GRAPHICS MODE
  if (buffer[0] == 27)
  {
    textMode = true;
    svg_X = 0;
  }
  if (!textMode)
    svg_graphics_command(n);
  else
  {
    // loop through string
    for (int i = 0; i < n; i++)
    {
      unsigned char c = buffer[i];
      //std::cout << "c=" << c << " ";

      // clear out residual EOL from ESC sequence
      if (escResidual)
      {
        escResidual = false;
        if (c == EOL)
          return;
      }
      // the following creates a blank line of text
      if (BOLflag && c == EOL)
      { // svg_new_line();
        svg_X = 0;
        svg_Y += lineHeight;
        svg_update_bounds();
        return;
      }
      // check for EOL or if at end of line and need automatic CR
      if (!BOLflag && c == EOL)
      {
        svg_end_line();
        return;
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
        return;
    }
  }
}

int main()
{
  FILE *fin = fopen("in.txt", "rb");
  if (!fin)
    return -1;
  f = fopen("out.svg", "w");

  svg_header();
  //SIMULATE SIO:
  //standard Atari P: handler sends 40 bytes at a time
  //break up line into 40-byte buffers
  do
  {
    unsigned char c = '\0';
    int i = 0;
    while (c != EOL && i < 40)
    {
      int t = getc(fin);
      if (t == EOF)
        break;
      c = (unsigned char)(t);
      std::cout << i;
      if (c == '\n')
        c = EOL;
      buffer[i++] = c;
    }
    std::cout << ", ";
    svg_add(i);
    fflush(f);
  } while (!feof(fin));
  svg_footer();
  fclose(f);
  fclose(fin);
  return 0;
}
