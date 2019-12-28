#include <stdio.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
using namespace std;

#define OUTSTREAM pdfout
#define INSTREAM prtin
#define NUMLINES 66

// printers
// standard: 80 column, 12 point, 6 LPI, letter size: 612 x 792, 18 left, 2 bottom + 780-12*linenum
// 822 thermal: 40 column, 12 point, 6 LPI, 4 7/16" width: 320 x roll, 16 left
// 820 dot matrix: 40 column, TBD point, LPI, 3 7/8" width:  279 x roll,
// 1027 impact:

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
  FILE *f = fopen("out2.pdf", "w"); // standard C output file
  string page;                      // buffer for page sections
  string line;
  stringstream str1;              // temporary stream buffer for numeric to string conversion
  string s;                       // temporary string for line reading from cin stream
  int objLocations[NUMLINES + 5]; // reference table storage
  int xref;

  INSTREAM.open("in.txt");
  OUTSTREAM.open("out.pdf");

  // header ****************************************************************************************

  int objCtr = 1;

  objLocations[objCtr] = fprintf(f, "%%PDF-1.4\n");

  line = "1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj\n";
  fprintf(f, line.c_str());

  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + line.length();
  line = "2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj\n";
  fprintf(f, line.c_str());

  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + line.length();
  line = "3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 612 792] /Contents [ ";

  int templength = line.length();
  fprintf(f, line.c_str());
  for (int i = 0; i < NUMLINES; i++)
  {
    templength += fprintf(f, "%d 0 R ", i + 6);
  }
  templength += fprintf(f, "]>> endobj\n");

  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + templength;
  line = "4 0 obj <</Font <</F1 5 0 R>>>> endobj\n";
  fprintf(f, line.c_str());

  objCtr++;
  objLocations[objCtr] = objLocations[objCtr - 1] + line.length();
  line = "5 0 obj <</Type /Font /Subtype /Type1 /BaseFont /Courier>> endobj\n";
  fprintf(f, line.c_str());

  // body ******************************************************************************************
  templength = line.length();
  for (int i = 0; i < NUMLINES; i++)
  {
    string s;
    if (!INSTREAM.eof())
      getline(INSTREAM, s);
    objCtr++;
    objLocations[objCtr] = objLocations[objCtr - 1] + templength;
    //stringstream str2;
    templength = fprintf(f, "%d 0 obj <</Length %d>> stream\n", 6 + i, 31 + s.length());
    //str2 << (6 + i) << " 0 obj <</Length " << (31 + s.length()) << ">> stream\n";
    templength += fprintf(f, "BT /F1 12 Tf 018 %d Td (%s)Tj ET\n", 782 - i * 12, s.c_str());
    //str2 << "BT /F1 12 Tf 018 " << (782 - i * 12) << " Td (" << s << ")Tj ET\n";
    //page += str2.str();
    templength += fprintf(f, "endstream endobj\n");
  }
  //OUTSTREAM << page;
  //fprintf(f, page.c_str());

  // xref and output *****************************************************************************

  xref = objLocations[objCtr] + templength;
  line = "xref\n";
  line += "0 72\n";
  //OUTSTREAM << page;
  fprintf(f, line.c_str());

  //OUTSTREAM << "0000000000 65535 f\n";
  fprintf(f, "0000000000 65535 f\n");

  for (int i = 1; i < (NUMLINES + 5); i++)
  {
    //OUTSTREAM << setfill('0') << setw(10) << objLocations[i] << " 00000 n\n";
    fprintf(f, "%010d 00000 n\n", objLocations[i]);
  }
  //OUTSTREAM << "trailer <</Size 72/Root 1 0 R>>\n";
  fprintf(f, "trailer <</Size 72/Root 1 0 R>>\n");
  //OUTSTREAM << "startxref\n";
  fprintf(f, "startxref\n");
  //OUTSTREAM << xref << "\n"
  //          << "%%EOF\n";
  fprintf(f, "%d\n", xref);
  fprintf(f, "%%%%EOF\n");
  //OUTSTREAM.close();
  fclose(f);
  return 0;
}
