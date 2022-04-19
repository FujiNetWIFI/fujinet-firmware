#ifdef DONT_BUILD_CBM

#include "cbm_image.h"

// Utility Functions

std::string CBMImageStream::decodeType(uint8_t file_type, bool show_hidden) 
{
    //bool hide = false;
    std::string type = file_type_label[ file_type & 0b00000111 ];
    //if ( file_type == 0 )
    //    hide = true;

    switch ( file_type & 0b11000000 )
    {
        case 0xC0:			// Bit 6: Locked flag (Set produces "<" locked files)
            type += "<";
            //hide = false;
            break;
            
        case 0x00:
            type += "*";	// Bit 7: Closed flag  (Not  set  produces  "*", or "splat" files)
            //hide = true;
            break;					
    }

    return type;
}

/********************************************************
 * Istream impls
 ********************************************************/

// std::string CBMImageStream::seekNextEntry() {
//     // Implement this to skip a queue of file streams to start of next file and return its name
//     // this will cause the next read to return bytes of "next" file in D64 image
//     // might not have sense in this case, as D64 is kinda random access, not a stream.
//     return "";
// };

bool CBMImageStream::open() {
    // return true if we were able to read the image and confirmed it is valid.
    // it's up to you in what state the stream will be after open. Could be either:
    // 1. EOF-like state (0 available) and the state will be cleared only after succesful seekNextEntry or seekPath
    // 2. non-EOF-like state, and ready to send bytes of first file, because you did immediate seekNextEntry here
    Debug_printf("here");
    return false;
};

void CBMImageStream::close() {

};

// bool CBMImageStream::seek(uint32_t pos) {
//     // seek only within current "active" ("seeked") file within the image (see above)
//     if(pos==m_position)
//         return true;

//     return false;
// };

size_t CBMImageStream::position() {
    return m_position; // return position within "seeked" file, not the D64 image!
};


size_t CBMImageStream::available() {
    // return bytes available in currently "seeked" file
    return m_bytesAvailable;
};

size_t CBMImageStream::size() {
    // size of the "seeked" file, not the image.
    return m_length;
};

size_t CBMImageStream::read(uint8_t* buf, size_t size) {
    size_t bytesRead = 0;

    if(seekCalled) {
        // if we have the stream set to a specific file already, either via seekNextEntry or seekPath, return bytes of the file here
        // or set the stream to EOF-like state, if whle file is completely read.
        bytesRead = readFile(buf, size);

    }
    else {
        // seekXXX not called - just pipe image bytes, so it can be i.e. copied verbatim
        bytesRead = containerStream->read(buf, size);
    }

    m_position += bytesRead;
    return bytesRead;
};

bool CBMImageStream::isOpen() {
    Debug_printf("here");
    return m_isOpen;
};

std::unordered_map<std::string, CBMImageStream*> ImageBroker::repo;

#endif /* BUILD_CBM */