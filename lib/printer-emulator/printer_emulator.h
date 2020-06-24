#ifndef PRINTER_EMU_H
#define PRINTER_EMU_H

#include "../../include/atascii.h"

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
private:
    bool _output_started = false;

protected:
    FileSystem *_FS = nullptr;
    FILE * _file = nullptr;
    paper_t _paper_type = RAW;

    uint8_t buffer[40];

    // Called after a new printer output file is created (allows for providing header data)
    virtual void post_new_file()=0;

    // Called before a printer output file is closed to send to the user (allows for providing footer data)
    virtual void pre_close_file()=0;
    
    // Called to actually process the printer output from the Atari as uint8_ts
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2)=0;

    size_t copy_file_to_output(const char *filename);
    void restart_output();
    
public:
    // Destructor must be virtual to allow for proper cleanup of derived classes
    virtual ~printer_emu();

    void initPrinter(FileSystem *fs);

    void closeOutput();
    FILE * closeOutputAndProvideReadHandle();

    bool process(uint8_t linelen, uint8_t aux1, uint8_t aux2);

    paper_t getPaperType() { return _paper_type; };

    uint8_t *provideBuffer() { return buffer; };

    void setPaper(paper_t ptype) { _paper_type = ptype; };

    virtual const char *modelname() = 0;
    size_t getOutputSize();

};

#endif // PRINTER_EMU_H
