#include <iostream>
#include <fstream>
#include <string>
#include <string.h>

using namespace std;

#define MAXFONTS 100

ifstream f;
ofstream g;
ofstream h;

int fontNumber[MAXFONTS];
int fontObject[MAXFONTS];

int findFontList()
{
  bool found = false;
  string line;
  int count = 0;

  do
  {
    line.clear();
    getline(f, line);
    cout << line << ": ";
    cout << line.find("/Font") << "\n";
  } while (line.find("/Font") == string::npos); // count < 40 &&
  line.clear();
  do
  {
    int f1;
    int f2;
    char buffer[10];
    getline(f, line);
    size_t offset = line.find("/F");
    printf("%u %u\n", offset, string::npos);
    // if offset not found then done
    if (offset < 10)
    {
      size_t ll = line.copy(buffer, 7, offset + 2);
      sscanf(buffer, "%d %d", &f1, &f2);
      printf("%d %d\n", f1, f2);
      fontNumber[count] = f1;
      fontObject[count] = f2;
      count++;
    }
    else
    {
      found = true;
    }

  } while (!found);
  // need to return the font and object numbers we found (vectors?)
  printf("found %d fonts\n", count);
  return count;
}

string getarg(string line, string param)
{
  cout << "   looking for " << param << " inside of " << line;
  size_t offset = line.find(param);
  if (offset == string::npos)
    return NULL;
  string found = line.substr(offset + param.length() + 1);
  cout << " and found '" << found << "'\n";
  return found;
}

int getFont(int i)
{
  // continue searching in file
  // first find object
  bool found = false;
  string line;
  string argstr;
  string param;
  char objstr[10];
  char buffer[40];
  size_t offset;
  size_t st;
  size_t sp;
  int tracking = 0;

  sprintf(objstr, "%d 0 obj", fontObject[i]);
  do
  {
    line.clear();
    getline(f, line);
    //cout << line << ": ";
    //cout << line.find(objstr) << "\n";
    // count++;
  } while (line.find(objstr) == string::npos); // count < 40 &&
  cout << line;
  cout << " object found\n";

  // now extract info
  /*
  std::string subtype;
  std::string basefont;
  uint16_t width[256]; // uniform spacing for now, todo: proportional
  uint16_t numwidth;
  float ascent;
  float capheight;
  float descent;
  byte flags;
  float bbox[4];
  float stemv;
  float xheight;
  byte ffnum;
  std::string ffname;
*/

  /*  const pdfFont_t F1 = {
      /Type /Font
    /Subtype /Type1
    /FontDescriptor 8 0 R
    /BaseFont /Atari-820-Normal
    /FirstChar 0
    /LastChar 255
    /Widths 10 0 R
    /Encoding /WinAnsiEncoding
    /Type /FontDescriptor
    /FontName /Atari-820-Normal
    /Ascent 1000
    /CapHeight 1000
    /Descent 0
    /Flags 33
    /FontBBox [0 0 433 700]
    /ItalicAngle 0
    /StemV 87
    /XHeight 714
    /FontFile3 9 0 R
  
        "Type1",            //F1->subtype =
        "Atari-820-Normal", //F1->basefont =
        {500},              //  F1->width[0] =
        1,                  // F1->numwidth
        1000,               // F1->ascent =
        1000,               // F1->capheight =
        0,                  // F1->descent =
        33,                 // F1->flags =
        {0, 0, 433, 700},
        87,         // F1->stemv =
        714,        // F1->xheight =
        3,          // F1->ffnum =
        "/a820norm" // F1->ffname =
    };
    */
  h << "const pdfFont_t F" << i << " = {" << endl;
  getline(f, line);
  getline(f, line);

  line.clear();
  getline(f, line);
  param = "/Subtype";
  argstr = getarg(line, param);
  h << "    \"" << argstr << "\","
    << "    // " << param << "\n";

  // need to grap the font descriptor object
  getline(f, line);

  line.clear();
  getline(f, line);
  param = "/BaseFont";
  argstr = getarg(line, param);
  h << "    \"" << argstr << "\","
    << "    // " << param << "\n";
}

int main(int argc, char **argv)
{
  if (argc != 3)
  {
    cout << "no filename or printer specified\n";
    return -1;
  }
  cout << "processing file: " << argv[1] << " for printer " << argv[2] << "\n";

  f.open(argv[1], ios::in | ios::binary);
  if (!f)
  {
    cout << "file not found.";
    return -1;
  }

  // create file names
  char gname[20] = "f_";
  char hname[20] = "h_";
  strcpy(&gname[2], argv[2]);
  strcpy(&hname[2], argv[2]);
  g.open(gname, ios::out | ios::binary);
  h.open(hname, ios::out);

  int numFonts = findFontList();
  for (int i = 0; i < numFonts; i++)
  {
    getFont(i);
  }

  return 0;
}