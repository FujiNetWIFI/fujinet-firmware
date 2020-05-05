#include <iostream>
#include <fstream>
#include <string>

using namespace std;

ifstream f;

void findFontList()
{
  bool found;
  string line;
  //int count = 0;
  do
  {
    line.clear();
    getline(f, line);
    cout << line << ": ";
    cout << line.find("/Font") << "\n";
    // count++;
  } while (line.find("/Font") == string::npos); // count < 40 && 
  line.clear();
  do
  {
    int f1;
    int f2;
    char buffer[10];
    getline(f, line);
    size_t offset = line.find("/F");
    // if offset not found then done
    size_t ll = line.copy(buffer,7,offset+2);
    sscanf(buffer,"%d %d",&f1,&f2);
    printf("%d %d\n",f1,f2);
    //cout << f1 << f2 << "\n";
  } while (true);
  // need to return the font and object numbers we found (vectors?)
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    cout << "no filename specified\n";
    return -1;
  }
  cout << "processing file: " << argv[1] << "\n";

  f.open(argv[1], ios::in | ios::binary);
  if (!f)
  {
    cout << "file not found.";
    return -1;
  }

  findFontList();

  return 0;
}