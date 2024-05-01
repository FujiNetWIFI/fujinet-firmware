#ifndef MEATLOAF_WRAPPER_DIRECTORY_STREAM
#define MEATLOAF_WRAPPER_DIRECTORY_STREAM

#include <memory>
#include <fstream>
#if HOST_OS==win32
#include "../meatloaf.h"
#else
#include "meatloaf.h"
#endif

class idirbuf : public std::filebuf {
    const size_t BUFFER_SIZE = 256;
    std::shared_ptr<MFile> container;
    char* buffer;
    uint8_t phase = 0; // 0 = headers, 1 = files, 2 = footer

public:
    idirbuf() {
        buffer = new char[BUFFER_SIZE];
    };

    ~idirbuf() {
        if(buffer != nullptr)
            delete[] buffer;
    }

    void open(std::shared_ptr<MFile> c) {
        container = c;
        container->rewindDirectory(); // in case we're in the middle of reading here!
        phase = 0;
    }

    // something will be READING from this stream, that's why we need to implement underflow
    int underflow() override {
        // it is up to us how we will fill this buffer. We can write it single line at
        // a time, or (if made big enough) with whole directory contents, it really
        // doesn't matter, because when everything from the buffer is read, underflow
        // will get called again
        //
        // I divided it into 3 phases, for header, files and footer, but again - any way is OK
        if(phase == 0) {
            // OK, so you are in headers. 
            // First you have to fill this buffer line by line or as whole. Doesn't matter.
            // i.e. like this:
            size_t written = lineToBuffer(69,"some header line");
            // When you're done, call this:
            this->setg(buffer, buffer, buffer + written);      
    
            // of course filling it at once would be easiest, as you just fill it here and set this:
            //phase = 1;
            // if you fill line by line incremet some count and set phase to 1 when all lines written...
        }
        else if(phase == 1) {
            // So now we're in files list phase, let's try and get a file:
            std::unique_ptr<MFile> entry(container->getNextFileInDir());

            if (entry != nullptr) {
                // There's still a file in this dir, so... Same principle:
                // we'll put BASIV V2 into buffer variable using this function:
                auto readCount = fileToBasicV2(entry.get());
                // and set required pointers:
                this->setg(buffer, buffer, buffer + readCount);
                // we're putting just this oone file in our buffer and exiting
                // underflow will be called to fill it with next file
            }
            else
                phase = 2; // nope, no more files here. Let's change phase, so we can write footer
                            // in the same pass. Otherwise we'll get an EOF!
        }
        
        if(phase == 2) {
            // ok, so now the footer, again - write it into the buffer and call:
            size_t written = lineToBuffer(69,"bytes free");
            this->setg(buffer, buffer, buffer + written);     
            phase = 3; 
            // next time underflow is called, we'll just return eof
        }
        else if(phase == 3) {
            // we've past the footer, let's send eof
            return std::char_traits<char>::eof();
        }

        return this->gptr() == this->egptr()
            ? std::char_traits<char>::eof()
            : std::char_traits<char>::to_int_type(*this->gptr());
    };

    size_t fileToBasicV2(MFile* file, long flags = 0L) {
        // convert MFile to ASCII line
        // we can use some additional FLAGS, i.e. for various CBM-style long directory format (they contain creation date and other info!)
        return lineToBuffer(69,"directory entry line in ASCII"); // return length of BASIC line written to the buffer
    }

    size_t lineToBuffer(uint16_t lineNumber, const std::string &lineContents) {
        // 1. convert lineContents to PETSCII or whatever is required to send to the C64
        // 2. fill our buffer with next line pointer, line number and converted lineContents
        // for(i...) buffer[i]=XXXX...
        // 3. return how may bytes you put in the buffer:
        return 69;
    }
};

// void exampleStreamFn(std::shared_ptr<MFile> container) {
//     idirbuf dirbuffer;  
//     dirbuffer.open(container);
//     std::istream dirStream(&dirbuffer);

//     // now you can just read BASIC V2 characters from dirStream
// }


#endif /* MEATLOAF_WRAPPER_DIRECTORY_STREAM */
