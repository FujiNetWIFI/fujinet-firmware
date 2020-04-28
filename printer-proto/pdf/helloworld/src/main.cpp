#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
using namespace std;

#define OUTSTREAM pdfout
#define INSTREAM prtin
#define NUMLINES 66

//based on ....
//HELLO WORLD PDF example from https://blog.idrsolutions.com/2010/10/make-your-own-pdf-file-part-4-hello-world-pdf/
//
//but with two lines of text on a letter size page
//standard letter 8.5 x 11 inch pages is 612 x 792 pts (aka pdf units = 72 points/inch)
//80 columns of 12 pt text is 8 inches or 72 x 8 = 576 unts
//11 inches of page is 792 units
//top left char is at 18 781
//bottom left char is at 18 001
/*
%PDF-1.4
1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj
2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj
3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 612 792] /Contents [ 6 0 R 8 0 R ]>> endobj
4 0 obj <</Font <</F1 5 0 R>>>> endobj
5 0 obj <</Type /Font /Subtype /Type1 /BaseFont /Courier>> endobj
6 0 obj <</Length 112>> stream
BT /F1 12 Tf 018 781 Td (00000000001111111111222222222233333333334444444444555555555566666666667777777777)Tj ET
endstream endobj
8 0 obj <</Length 112>> stream
BT /F1 12 Tf 018 769 Td (01234567890123456789012345678901234567890123456789012345678901234567890123456789)Tj ET
endstream endobj
xref
0 7
0000000000 65535 f
0000000009 00000 n
0000000056 00000 n
0000000111 00000 n
0000000223 00000 n
0000000262 00000 n
0000000328 00000 n
0000000359 00000 n
trailer <</Size 7/Root 1 0 R>>
startxref
648
%%EOF
*/

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

  int oCtr = 1;
  page = "%PDF-1.4\n";
  loc[oCtr++] = page.length();
  page += "1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj\n";
  loc[oCtr++] = page.length();
  page += "2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj\n";
  loc[oCtr++] = page.length();
  page += "3 0 obj <</Type /Page /Parent 2 0 R /Resources 4 0 R /MediaBox [0 0 612 792] /Contents [ ";
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
  for (int i = 0; i < NUMLINES; i++)
  {
    string s;
    if (!INSTREAM.eof())
      getline(INSTREAM, s);
    loc[oCtr++] = page.length();
    stringstream str2;
    str2 << (6 + i) << " 0 obj <</Length " << (31 + s.length()) << ">> stream\n";
    str2 << "BT /F1 12 Tf 018 " << (782 - i * 12) << " Td (" << s << ")Tj ET\n";
    page += str2.str();
    page += "endstream endobj\n";
  }
  xref = page.length();
  page += "xref\n";
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
