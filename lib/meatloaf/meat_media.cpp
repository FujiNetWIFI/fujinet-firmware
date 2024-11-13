#include "meat_media.h"

std::unordered_map<std::string, MMediaStream*> ImageBroker::repo;

// Utility Functions

std::string MMediaStream::decodeType(uint8_t file_type, bool show_hidden)
{
    //bool hidden = false;
    std::string type = "";

    // Check if splat file
    // Bit 7: Closed flag  (Not  set  produces  "*", or "splat" files)
    if (!(file_type >> 7 & 1)) {
        type += "*";
        //hidden = true;
    } else {
        type += " ";
    }

    type += file_type_label[ file_type & 0b00000111 ];
    //if ( file_type == 0 )
    //    hidden = true;

    // Bit 6: Locked flag (Set produces ">" locked files)
    if ((file_type >> 6 & 1)) {
        type += "<";
        //hidden = false;
    } else {
        type += " ";
    }

    return type;
}

std::string MMediaStream::decodeType(std::string file_type)
{
    std::string type = " PRG";

    if (file_type == "P")
        type = " PRG";
    else if (file_type == "S")
        type = " SEQ";
    else if (file_type == "U")
        type = " USR";
    else if (file_type == "R")
        type = " REL";

    return type;
}

/********************************************************
 * Istream impls
 ********************************************************/

// std::string MMediaStream::seekNextEntry() {
//     // Implement this to skip a queue of file streams to start of next file and return its name
//     // this will cause the next read to return bytes of "next" file in D64 image
//     // might not have sense in this case, as D64 is kinda random access, not a stream.
//     return "";
// };

bool MMediaStream::open() 
{
    // return true if we were able to read the image and confirmed it is valid.
    // it's up to you in what state the stream will be after open. Could be either:
    // 1. EOF-like state (0 available) and the state will be cleared only after succesful seekNextEntry or seekPath
    // 2. non-EOF-like state, and ready to send bytes of first file, because you did immediate seekNextEntry here

    return false;
};

void MMediaStream::close()
{
    Debug_printv("url[%s]", url.c_str());
};

uint32_t MMediaStream::seekFileSize( uint8_t start_track, uint8_t start_sector )
{
    // Calculate file size
    seekSector(start_track, start_sector);

    size_t blocks = 0; 
    do
    {
        //Debug_printv("t[%d] s[%d]", t, s);
        readContainer(&start_track, 1);
        readContainer(&start_sector, 1);
        blocks++;
        if ( start_track > 0 )
            if ( !seekSector( start_track, start_sector ) )
                break;
    } while ( start_track > 0 );
    blocks--;
    return (blocks * (block_size - 2)) + start_sector - 1;
};


uint32_t MMediaStream::readContainer(uint8_t *buf, uint32_t size)
{
    return containerStream->read(buf, size);
}


uint32_t MMediaStream::write(const uint8_t *buf, uint32_t size) {
    return -1;
}

uint32_t MMediaStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    //Debug_printv("seekCalled[%d]", seekCalled);
    if ( _position >= _size )
        return 0;

    if(seekCalled) {
        // if we have the stream set to a specific file already, either via seekNextEntry or seekPath, return bytes of the file here
        // or set the stream to EOF-like state, if whle file is completely read.
        bytesRead = readFile(buf, size);

    }
    else {
        // seekXXX not called - just pipe image bytes, so it can be i.e. copied verbatim
        bytesRead = readContainer(buf, size);
    }

    _position += bytesRead;

    return bytesRead;
};

bool MMediaStream::isOpen() {

    return _is_open;
};
