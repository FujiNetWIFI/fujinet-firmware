#ifndef PRINTER_EMU_H
#define PRINTER_EMU_H
#include <Arduino.h>

#include "fnFsSD.h"
#include "fnFsSPIF.h"

// TODO: Combine html_printer.cpp/h and file_printer.cpp/h

// I think the way we're using this value is as a switch to tell the printer
// emulator what kind of output we expect or what kind of conversion to
// perform on the incoming data.  Maybe more clear if we called it
// output_type or output_conversion?
enum paper_t
{
    RAW,
    TRIM,
    ASCII,
    PDF,
    SVG,
    PNG,
    HTML,
    HTML_ATASCII
};

class printer_emu
{
protected:
    FileSystem *_FS = nullptr;
    FILE * _file = nullptr;
    byte buffer[40];
    paper_t paperType;

    // executed after a new printer output file is created
    virtual void post_new_file() {};
    // executed before a printer output file is closed and prepared for reading
    virtual void pre_page_eject() {};
    
    virtual bool process_buffer(byte linelen, byte aux1, byte aux2)=0;

    size_t copy_file_to_output(const char *filename);

public:
    printer_emu(FileSystem *fs, paper_t ty = RAW) : _FS(fs), paperType(ty) {};
    // Destructor must be virtual to allow for proper cleanup of derived classes
    virtual ~printer_emu() = 0;

    //void copyChar(byte c, byte n);
    virtual void initPrinter();
    virtual void pageEject() = 0;
    bool process(byte linelen, byte aux1, byte aux2);



    paper_t getPaperType() { return paperType; };

    byte *provideBuffer() { return buffer; };

    virtual const char *modelname() = 0;
    //File *getFilePtr() { return _file; }
    // virtual void flushOutput(); // do this in pageEject
    size_t getOutputSize();
    int readFromOutput();
    int readFromOutput(uint8_t *buf, size_t size);
    void resetOutput();
    //void resetPrinter() { initPrinter(_FS); };

};

// close flush output file
// void sioPrinter::flushOutput()
// {
//     _file.flush();
//     _file.seek(0);
// }

#endif