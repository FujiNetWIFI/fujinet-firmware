#include <stdio.h>
#include <string.h>

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

std::ifstream prtin; // input file

FILE *f; // standard C output file

bool BOLflag = true;
float svg_X = 0.;
float svg_Y = 0.;
float pageWidth = 550.;
float printWidth = 480.;
double leftMargin = 0.0;
float charWidth = 12.;
float lineHeight = 20.8;
float fontSize = 20.8;

bool escMode;
bool escResidual;
bool textMode;

void svg_new_line()
{
  // http://scruss.com/blog/2016/04/23/fifteentwenty-commodore-1520-plotter-font/
  // <text x="0" y="15" fill="red">I love SVG!</text>
  // position new line and start text string array
  //fprintf(f,"<text x=\"0\" y=\"%g\" font-size=\"%g\" font-family=\"ATARI 1020 VECTOR FONT APPROXIM\" fill=\"black\">", svg_Y,fontSize);
  svg_Y += lineHeight;
  fprintf(f, "<text x=\"%g\" y=\"%g\" font-size=\"%g\" font-family=\"FifteenTwenty\" fill=\"black\">", leftMargin, svg_Y, fontSize);
  svg_X = 0; // reinforce?

  BOLflag = false;
}

void svg_end_line()
{
  // <text x="0" y="15" fill="red">I love SVG!</text>
  fprintf(f, "</text>\n"); // close the line
                           // line feed
  //svg_X = 0.;               // CR

  BOLflag = true;
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
INITIALIZE		I			            GRAPHICS
(Sets current X,Y as HOME or 0,0)
RELATIVE DRAW	JX,Y			        GRAPHICS
(Used with Init.)
RELATIVE MOVE	RX,Y			        GRAPHICS
(Used with mit.)
CHAR. SCALE		S(0-63)			      GRAPHICS
*/
void svg_handle_char(unsigned char c)
{
  if (textMode)
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
      if (c == 16)
      {
        charWidth = 24.;
        fontSize = 2. * 20.8;
        lineHeight = fontSize;
      }
      if (c == 14)
      {
        charWidth = 12.;
        fontSize = 20.8;
        lineHeight = fontSize;
      }
      if (c == 19)
      {
        charWidth = 6.;
        fontSize = 10.4;
        lineHeight = fontSize;
      }
      escMode = false;
      escResidual = true;
    }
    else if (c == 27)
      escMode = true;
    else
        // simple ASCII printer
        if (c > 31 && c < 127)
    {
      // if (c == BACKSLASH || c == LEFTPAREN || c == RIGHTPAREN)
      //   _file->write(BACKSLASH);
      fputc(c, f);        //_file->write(c);
      svg_X += charWidth; // update x position
      //std::cout << svg_X << " ";
    }
  }
  else
  {
    //graphics mode
  }
}

void svg_header()
{
  // <!DOCTYPE html>
  // <html>
  // <body>
  // <svg height="210" width="500">
  //fprintf(f,"<!DOCTYPE html>\n");
  //fprintf(f,"<html>\n");
  //fprintf(f,"<body>\n\n");
  fprintf(f, "<svg height=\"2000\" width=\"%g\" viewBox=\"0 -1000 480 2000\">\n", pageWidth);
}

void svg_footer()
{
  // </svg>
  // </body>
  // </html>
  if (!BOLflag)
    svg_end_line();
  fprintf(f, "</svg>\n\n");
  //fprintf(f,"</body>\n");
  //fprintf(f,"</html>\n");
}

void svg_add(std::string S)
{
  // loop through string
  for (int i = 0; i < S.length(); i++)
  {
    unsigned char c = (unsigned char)S[i];
    //std::cout << "c=" << c << " ";

    if (textMode)
    {
      if (escResidual)
      {
        escResidual = false;
        if (c == EOL)
          return;
      }
      // the following creates a blank line of text
      if (BOLflag && c == EOL)
      { // svg_new_line();
        svg_Y += lineHeight;
        return;
      }
      // check for EOL or if at end of line and need automatic CR
      if (!BOLflag && c == EOL)
      {
        svg_end_line();
        return;
      }
      else if (!BOLflag && (svg_X > (printWidth - charWidth)))
      {
        svg_end_line();
        svg_new_line();
      } // or do I just need to start a new line of text
      else if (BOLflag && c != 27 && !escMode)
        svg_new_line();
    }
    else
    {
      // graphics mode
    }

    // disposition the current byte
    svg_handle_char(c);
  }
}

int main()
{

  std::string payload;

  int j;

  prtin.open("in.txt");
  if (prtin.fail())
    return -1;
  f = fopen("out.svg", "w");

  svg_header();
  //SIMULATE SIO:
  //standard Atari P: handler sends 40 bytes at a time
  //break up line into 40-byte buffers
  do
  {
    payload.clear();
    char c = '\0';
    int i = 0;
    while (c != EOLS && i < 40 && !prtin.eof())
    {
      prtin.get(c);
      if (c == '\n')
        c = EOLS;
      payload.push_back(c);
      i++;
    }
    std::cout << payload << ", ";
    svg_add(payload);
    fflush(f);
  } while (!prtin.eof());
  svg_footer();
  fclose(f);
  return 0;
}
