#include <stdio.h>
#include <string.h>

#include <iomanip>
#include <iostream>
#include <fstream>
//using namespace std;

#define NUMLINES 66
#define EOL -100

// printers
// standard: 80 column, 12 point, 6 LPI, letter size: 612 x 792, 18 left, 2 bottom
// 822 thermal: 40 column, 12 point, 6 LPI, 4 7/16" width: 320 x roll, 16 left
// 820 dot matrix: 40 column, TBD point, LPI, 3 7/8" width:  279 x roll,
// 1027 impact: 80 columns, TBD point, LPI, letter size: 612 x 792, TBD left, TBD bottom
#define STANDARD_80
//#define ATARI_820
//#define ATARI_820_2_COL
//#define ATARI_822
//#define ATARI_1027
int lineHeight = 12;
int pageWidth = 612;
int pageHeight = 792;
int leftMargin = 42;
int bottomMargin = 2;
int fontSize = 11;
const char *fontName = "Courier";
int lineCounter = 0;

// todo:
// specify: page size, left/bottom margins, line spacing
// sio simulation: read input file, make 40-char buffers, replace CRLF with EOL, pad with spaces to 40 char
// using simulated sio buffer to create text stream payload for PDF lines
// use standard C and POSIX file output (don't care about input because that's SIO simulation)
// replace page(string) with incremental line string and accumulate lengths for xref
// use NUMLINES to parameterize the routine

std::ifstream prtin; // input file

int offset;                     // used to store location offset to next object
int objLocations[NUMLINES + 5]; // reference table storage
int xref;                       // store the xref tabel location
int objCtr = 0;                 // count the objects

FILE *f; // standard C output file

void pdf_header()
{
  offset = fprintf(f, "%%PDF-1.4\n");
  // first object: catalog of pages
  objCtr = 1;
  objLocations[objCtr] = offset;
  offset = fprintf(f, "1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj\n");
  // second object: one page
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  offset = fprintf(f, "2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj\n");
  // third object: page contents
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  offset = fprintf(f, "3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 %d %d] /Contents [ ", pageWidth, pageHeight);
  for (int i = 0; i < NUMLINES; i++)
  {
    offset += fprintf(f, "%d 0 R ", i + 6);
  }
  offset += fprintf(f, "]>> endobj\n");
  // fourth object: font catalog
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  //line = ;
  offset = fprintf(f, "4 0 obj <</Font <</F1 5 0 R>>>> endobj\n");
  // fifth object: font 1
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  offset = fprintf(f, "5 0 obj <</Type /Font /Subtype /Type1 /BaseFont /%s>> endobj\n", fontName);
}

void pdf_xref()
{
  xref = objLocations[objCtr] + offset;
  objCtr++;
  fprintf(f, "xref\n");
  fprintf(f, "0 %d\n", objCtr);
  fprintf(f, "0000000000 65535 f\n");
  for (int i = 1; i < (NUMLINES + 5); i++)
  {
    fprintf(f, "%010d 00000 n\n", objLocations[i]);
  }
  fprintf(f, "trailer <</Size %d/Root 1 0 R>>\n", objCtr);
  fprintf(f, "startxref\n");
  fprintf(f, "%d\n", xref);
  fprintf(f, "%%%%EOF\n");
}

void pdf_add_line(const char *L)
{
  // to do: handle odd characters for fprintf, e.g., %,'," etc.
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  offset = fprintf(f, "%d 0 obj <</Length %d>> stream\n", objCtr, 30 + strlen(L));
  int xcoord = pageHeight - lineHeight + bottomMargin - lineCounter * lineHeight;
  //this string right here vvvvvv is 30 chars long plus the length of the payload
  offset += fprintf(f, "BT /F1 %2d Tf %2d %3d Td (%s)Tj ET\n", fontSize, leftMargin, xcoord, L);
  offset += fprintf(f, "endstream endobj\n");
}

void atari_to_c_str(char *S)
{
  int i = 0;
  S[40] = '\0';
  while (i < 40)
  {
    if (S[i] == EOL)
    {
      S[i] = '\0';
      return;
    }
    i++;
  }
}

int main()
{
  char buffer[40];
  std::string payload;
  std::string output;
  int j;

  prtin.open("in.txt");
  f = fopen("out2.pdf", "w");

  pdf_header();
  // body ******************************************************************************************
  //for (int inputCounter = 0; inputCounter < NUMLINES; inputCounter++)
  while (lineCounter < NUMLINES)
  {
    lineCounter++;
    output.clear();
    if (!prtin.eof())
    {
      getline(prtin, payload);
      std::cout << "line " << std::setw(3) << lineCounter << ":  " << payload << "\n";
      //SIMULATE SIO:
      //standard Atari P: handler sends 40 bytes at a time
      //break up line into two 40-byte buffers, add an EOL, pad with spaces
      //memset(buffer, '\0', 41);
      j = payload.copy(buffer, 40);
      if (j < 40)
      {
        buffer[j++] = EOL;
        while (j < 40)
        {
          buffer[j++] = ' ';
        }
      }
      buffer[40] = '\0';
      // now buffer contains an SIO-like buffer array from the OS P: handler
      std::cout << "buffer 1: [" << buffer << "]\n";
      atari_to_c_str(buffer);
      output.append(buffer);
      // make a new SIO-like buffer
      if (payload.length() > 40)
      {
        //memset(buffer, '\0', 41);
        j = payload.copy(buffer, 40, 40);
        if (j < 40)
        {
          buffer[j++] = EOL;
          while (j < 40)
          {
            buffer[j++] = ' ';
          }
        }
        buffer[40] = '\0';
        // now buffer contains an SIO-like buffer array from the OS P: handler
        std::cout << "buffer 2: [" << buffer << "]\n";
        atari_to_c_str(buffer);
        output.append(buffer);
      }
    }
    std::cout << "output:   >" << output << '\n';
    pdf_add_line(output.c_str());
    fflush(f);
  }
  pdf_xref();
  fclose(f);
  return 0;
}
