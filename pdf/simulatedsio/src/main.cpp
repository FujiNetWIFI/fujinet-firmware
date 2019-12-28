//#include <stdio.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
using namespace std;

#define OUTSTREAM pdfout
#define INSTREAM prtin
#define NUMLINES 66

// todo:
// specify: page size, left/bottom margins, line spacing
// sio simulation: read input file, make 40-char buffers, replace CRLF with EOL, pad with spaces to 40 char
// using simulated sio buffer to create text stream payload for PDF lines
// use standard C and POSIX file output (don't care about input because that's SIO simulation)
// replace page(string) with incremental line string and accumulate lengths for xref
// use NUMLINES to parameterize the routine

int main()
{
  ifstream INSTREAM; // input file
  ofstream OUTSTREAM;
  string page;           // buffer for whole page
  stringstream str1;     // temporary stream buffer for numeric to string conversion
  string s;              // temporary string for line reading from cin stream
  int loc[NUMLINES + 5]; // reference table storage
  int xref;

  INSTREAM.open("in.txt");
  OUTSTREAM.open("out.pdf");

  // header ****************************************************************************************

  int oCtr = 1;
  page = "%PDF-1.4\n";
  loc[oCtr++] = page.length();
  page += "1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj\n";
  loc[oCtr++] = page.length();
  page += "2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj\n";
  loc[oCtr++] = page.length();
  page += "3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 320 792] /Contents [ ";
  for (int i = 0; i < NUMLINES; i++)
  {
    str1 << (i + 6) << " 0 R ";
  }
  str1 << "]>> endobj\n";
  page += str1.str();
  loc[oCtr++] = page.length();
  page += "4 0 obj <</Font <</F1 5 0 R>>>> endobj\n";
  loc[oCtr++] = page.length();
  page += "5 0 obj <</Type /Font /Subtype /Type1 /BaseFont /Courier>> endobj\n";
  OUTSTREAM << page;

  // body ******************************************************************************************

  int bodyorig = page.length();
  page = "";

  for (int i = 0; i < NUMLINES; i++)
  {
    string s;
    if (!INSTREAM.eof())
      getline(INSTREAM, s);
    loc[oCtr++] = bodyorig + page.length();
    stringstream str2;
    str2 << (6 + i) << " 0 obj <</Length " << (31 + s.length()) << ">> stream\n";
    str2 << "BT /F1 12 Tf 016 " << (782 - i * 12) << " Td (" << s << ")Tj ET\n";
    page += str2.str();
    page += "endstream endobj\n";
  }
  OUTSTREAM << page;

  // xref and output *****************************************************************************

  xref = bodyorig + page.length();
  page = "xref\n";
  page += "0 72\n";
  OUTSTREAM << page;
  OUTSTREAM << "0000000000 65535 f\n";
  for (int i = 1; i < (NUMLINES + 5); i++)
  {
    OUTSTREAM << setfill('0') << setw(10) << loc[i] << " 00000 n\n";
  }
  OUTSTREAM << "trailer <</Size 72/Root 1 0 R>>\n";
  OUTSTREAM << "startxref\n";
  OUTSTREAM << xref << "\n"
            << "%%EOF\n";
  OUTSTREAM.close();

  return 0;
}
