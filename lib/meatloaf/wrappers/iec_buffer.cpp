#ifdef BUILD_IEC
#include "iec_buffer.h"

oiecstream iecStream;

/********************************************************
 * oiecbuf
 * 
 * A buffer for writing IEC data, handles sending EOI
 ********************************************************/
size_t oiecstream::easyWrite() {
    size_t written = 0;

    // Serial.printf("buff     :");
    // for(auto i = pbase(); i<pptr(); i++) {
    //     Serial.printf("%c",*i);
    // }
    // Debug_printv("\n");

    // we're always writing without the last character in buffer just to be able to send this special delay
    // if this is last character in the file

    //Debug_printv("IEC easyWrite will try to send %d bytes over IEC", pptr()-pbase());

    //  pptr =  Returns the pointer to the current character (put pointer) in the put area.
    //  pbase = Returns the pointer to the beginning ("base") of the put area.
    //  epptr = Returns the pointer one past the end of the put area.
    Serial.printf("buff->IEC:");
    for(auto b = pbase(); b < pptr()-1; b++) {
        //Serial.printf("%c",*b);
        //Serial.printf("%c[%.2X]",*b, *b);
        bool sendSuccess = m_iec->sendByte(*b);
        //bool sendSuccess = true;
        if(sendSuccess && !(IEC.flags bitand ATN_PULLED) ) written++;
        else if(!sendSuccess) {
            // what should happen here?
            // should the badbit be set when send returns false?
            setstate(badbit);
            setp(data+written, data+IEC_BUFFER_SIZE); // set pbase to point to next unwritten char
            Debug_printv("IEC acknowledged %d bytes, then failed\n", written);
            return written;
        }
        else {
            // ATN was pulled
            setp(data+written, data+IEC_BUFFER_SIZE); // set pbase to point to next unwritten char
            Debug_printv("IEC acknowledged %d bytes, then ATN was pulled\n", written);
            return written;
        }
    }

    
    // probably more bytes to come, so
    // here we wrote all buffer chars but the last one.
    // we will take this last byte, put it at position 0 and set pptr to 1
    char lastChar = *(pbase()+written);
    setp(data, data+IEC_BUFFER_SIZE); // reset the beginning and ending buffer pointers
    pbump(1); // and set pptr to 1 to tell there's 1 byte in our buffer
    data[0] = lastChar; // let's put it at position 0
    Debug_printv("---> LAST [%.2X]\n", data[0]);
    Debug_printv("IEC acknowledged %d bytes\n", written);

    return written;
}

int oiecstream::overflow(int ch) {
    if (!is_open())
    {
        return EOF;
    }

    Debug_printv("overflow for iec called, size=%d", pptr()-pbase());
    char* end = pptr();
    if ( ch != EOF ) {
        pbump(1);
        *end ++ = ch;
    }

    size_t written = easyWrite();

    if ( written == 0 ) {
        ch = EOF;
    } else if ( ch == EOF ) {
        ch = 0;
    }
    
    return ch;
};

int oiecstream::sync() { 
    if(pptr()-pbase() <= 1) {
        Debug_printv("sync for iec called - nothing more to write");
        return 0;
    }
    else {
        Debug_printv("sync for iec called - buffer contains %d bytes", pptr()-pbase());
        auto result = easyWrite(); 
        return (result != 0) ? 0 : -1;  
    }  
};


/********************************************************
 * oiecstream
 * 
 * Standard C++ stream for writing to IEC
 ********************************************************/

void oiecstream::putUtf8(U8Char* codePoint) {
    //Serial.printf("%c",codePoint->toPetscii());
    //Debug_printv("oiecstream calling put");
    auto c = codePoint->toPetscii();
    put(codePoint->toPetscii());        
}

    // void oiecstream::writeLn(std::string line) {
    //     // line is utf-8, convert to petscii

    //     std::string converted;
    //     std::stringstream ss(line);

    //     while(!ss.eof()) {
    //         U8Char codePoint(&ss);
    //         converted+=codePoint.toPetscii();
    //     }

    //     Debug_printv("UTF8 converted to PETSCII:%s",converted.c_str());

    //     (*this) << converted;
    // }
#endif /* BUILD_IEC */