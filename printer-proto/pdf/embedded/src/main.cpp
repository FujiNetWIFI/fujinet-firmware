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

string fontname;
size_t objPos[7];

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

int copyFont(int i)
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
  int objCtr = 0;

  sprintf(objstr, "%d 0 obj", fontObject[i]);

  do
  {
    line.clear();
    getline(f, line);
  } while (line.find(objstr) == string::npos);
  cout << line;
  cout << " object found\n";

  // start copy
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";

  getline(f, line); // <<
  g << line << "\n";
  getline(f, line); //  /Type /Font
  g << line << "\n";
  getline(f, line); // /Subtype /Type1
  g << line << "\n";

  getline(f, line); // /FontDescriptor 7 0 R
  offset = line.find_first_of("0123456789");
  g << line.substr(0, offset);
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";

  getline(f, line); //  /BaseFont /Atari-1025-Normal
  g << line << "\n";
  getline(f, line); //  /FirstChar 0
  g << line << "\n";
  getline(f, line); //  /LastChar 255
  g << line << "\n";

  getline(f, line); // /Widths 9 0 R
  offset = line.find_first_of("0123456789");
  g << line.substr(0, offset);
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";

  getline(f, line); //  /Encoding /WinAnsiEncoding
  g << line << "\n";
  getline(f, line); // >>
  g << line << "\n";
  getline(f, line); //endobj
  g << line << "\n";

  getline(f, line); // 7 0 obj
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";
  getline(f, line); // <<
  g << line << "\n";
  getline(f, line); // /Type /FontDescriptor

  g << line << "\n";
  getline(f, line);                     // /FontName /Atari-1025-Normal
  fontname = getarg(line, "/FontName"); // store away for filename

  g << line << "\n";
  getline(f, line); // /Ascent 1000
  g << line << "\n";
  getline(f, line); // /CapHeight 1000
  g << line << "\n";
  getline(f, line); // /Descent 0
  g << line << "\n";
  getline(f, line); // /Flags 33
  g << line << "\n";
  getline(f, line); // /FontBBox [0 1 499 699]
  g << line << "\n";
  getline(f, line); // /ItalicAngle 0
  g << line << "\n";
  getline(f, line); // /StemV 87
  g << line << "\n";
  getline(f, line); // /XHeight 500
  g << line << "\n";

  getline(f, line); // /FontFile3 8 0 R but could be FontFile or maybe FontFile2
  offset = line.find("/FontFile");
  offset += 9;
  if (line[offset] != ' ')
  {
    cout << "'" << line[offset] << "'";
    offset++;
  }

  //offset = line.find_first_of("0123456789");       // always font file 3 because of OTF
  g << line.substr(0, ++offset);
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n"; // always font file 3 because of OTF

  getline(f, line); // >>
  g << line << "\n";
  getline(f, line); // endobj
  g << line << "\n";

  getline(f, line); // 8 0 obj
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";
  getline(f, line); // <<
  g << line << "\n";
  getline(f, line); // /Length1 31137
  g << line << "\n";
  getline(f, line); // /Subtype /Type1C
  g << line << "\n";
  getline(f, line); // /Filter /FlateDecode
  g << line << "\n";

  getline(f, line); // /Length 5592
  // need to get the length number to copy the deflated fontfile
  offset = line.find_first_of("0123456789");
  cout << line.substr(offset) << "\n";
  size_t len;
  sscanf(line.substr(offset).c_str(), "%d", &len);
  len++; // grab the missing EOL character
  printf("%d\n", len);
  g << line << "\n";

  getline(f, line); // >>
  g << line << "\n";
  getline(f, line); // stream
  g << line << "\n";
  cout << line << "\n";

  for (int i = 0; i < len; i++)
    g.put(f.get());
  // copy deflated font file

  getline(f, line); // endstream
  g << line << "\n";
  cout << line << "\n";
  getline(f, line); // endobj
  g << line << "\n";
  cout << line << "\n";
  getline(f, line); // 9 0 obj
  objPos[objCtr++] = g.tellp();
  g << "%d 0 obj\n";
  cout << line << "\n";
  getline(f, line); // [ 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 600 ]
  g << line << "\n";
  cout << line << "\n";
  getline(f, line); // endobj
  g << line << "\n";
  cout << line << "\n";
  cout << "done with font\n\n";
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    cout << "no filename specified\n";
    return -1;
  }
  cout << "processing file: " << argv[1] << "\n"; //<< " for printer " << argv[2] << "\n";

  f.open(argv[1], ios::in | ios::binary);
  if (!f)
  {
    cout << "file not found.";
    return -1;
  }

  // create file names
  h.open("fontpos.h", ios::out | ios::binary);

  int numFonts = findFontList();

  h << "const unsigned int fontObjPos[" << numFonts << "][6] = {\n";

  for (int i = 0; i < numFonts; i++)
  {
    char gname[10];
    sprintf(gname,"F%d\0",i);
    g.open(gname, ios::out | ios::binary);
    copyFont(i);
    g.close();

    h << "  {\n  // " << fontname << " \n";
    h << "    " << (objPos[1] - objPos[0]) << ", // FontDescriptor Reference \n";
    h << "    " << (objPos[2] - objPos[0]) << ", // Widths Reference \n";
    h << "    " << (objPos[3] - objPos[0]) << ", // FontDescriptor Object \n";
    h << "    " << (objPos[4] - objPos[0]) << ", // FontFile Reference \n";
    h << "    " << (objPos[5] - objPos[0]) << ", // FontFile Object \n";
    h << "    " << (objPos[6] - objPos[0]) << "  // Widths Object \n";
    h << "  }";
    if (i != (numFonts - 1))
      h << ",";
    h << "\n";
  }
  h << "};\n";

  return 0;
}