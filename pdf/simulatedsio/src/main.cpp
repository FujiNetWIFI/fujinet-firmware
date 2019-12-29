#include <stdio.h>
#include <string.h>

#include <fstream>
//using namespace std;

#define INSTREAM prtin
#define NUMLINES 66

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
int leftMargin = 18;
int bottomMargin = 2;
int fontSize = 12;
const char *fontName = "Courier";

// todo:
// specify: page size, left/bottom margins, line spacing
// sio simulation: read input file, make 40-char buffers, replace CRLF with EOL, pad with spaces to 40 char
// using simulated sio buffer to create text stream payload for PDF lines
// use standard C and POSIX file output (don't care about input because that's SIO simulation)
// replace page(string) with incremental line string and accumulate lengths for xref
// use NUMLINES to parameterize the routine

std::ifstream INSTREAM; // input file
int offset;
int objLocations[NUMLINES + 5]; // reference table storage
int xref;
int objCtr = 0;

FILE *f; // standard C output file

void pdf_header()
{
  offset = fprintf(f, "%%PDF-1.4\n");
  // first object: catalog of pages
  objCtr++;
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

void pdf_add_line(int lineNum, const char *L)
{
  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + offset;
  offset = fprintf(f, "%d 0 obj <</Length %d>> stream\n", 6 + lineNum, 30 + strlen(L));
  int xcoord = pageHeight - lineHeight + bottomMargin - lineNum * lineHeight;
  //this string right here vvvvvv is 30 chars long plus the length of the payload
  offset += fprintf(f, "BT /F1 %2d Tf %2d %3d Td (%s)Tj ET\n", fontSize, leftMargin, xcoord, L);
  offset += fprintf(f, "endstream endobj\n");
}

int main()
{
  INSTREAM.open("in.txt");
  f = fopen("out2.pdf", "w");
  pdf_header();
  // body ******************************************************************************************
  for (int i = 0; i < NUMLINES; i++)
  {
    std::string payload;
    if (!INSTREAM.eof())
      getline(INSTREAM, payload);
    pdf_add_line(i, payload.c_str());
  }
  pdf_xref();
  fclose(f);
  return 0;
}
