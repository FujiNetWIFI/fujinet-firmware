// #ifdef BUILD_IEC
// #include "iec_buffer.h"
// #include "../../../include/cbm_defines.h"

// oiecstream iecStream;

// /********************************************************
//  * oiecbuf
//  * 
//  * A buffer for writing IEC data, handles sending EOI
//  ********************************************************/


// /********************************************************
//  * SAVE ops, pipe mode = _S_out, uses get area
//  ********************************************************/
// size_t oiecstream::receiveBytesViaIEC() {
//     // we are in a SAVE operation here, so we are moving bytes from C64 to file, by reading from IEC
//     // underflow happened to our get buffer and this function was called, it has to read bytes from IEC
//     // put them in gbuff and setg to point to them

//     // TODO: implement
//     return 0;
// }

// /********************************************************
//  * LOAD ops, pipe mode = _S_in, uses put area
//  ********************************************************/
// void oiecstream::flushpbuff() {
//     // a seek was called on our pipe, meaning we want to change the location we are reading from
//     // since there might be some bytes in the buffer from previous read operations, 
//     // waiting to be sent, we need to flush them, as they won't be!

//     setp(data, data+IEC_BUFFER_SIZE); // reset the beginning and ending buffer pointers
// }

// size_t oiecstream::sendBytesViaIEC() {
//     size_t written = 0;

//     // we are in a LOAD operation here, so we are pusing bytes from file to C64, by writing to IEC

//     // printf("buff     :");
//     // for(auto i = pbase(); i<pptr(); i++) {
//     //     printf("%c",*i);
//     // }
//     // Debug_printv("\n");

//     // we're always writing without the last character in buffer just to be able to send this special delay
//     // if this is last character in the file

//     //Debug_printv("IEC sendBytesViaIEC will try to send %d bytes over IEC", pptr()-pbase());

//     //  pptr =  Returns the pointer to the current character (put pointer) in the put area.
//     //  pbase = Returns the pointer to the beginning ("base") of the put area.
//     //  epptr = Returns the pointer one past the end of the put area.
//     printf("loading file: buff->IEC:");
//     for(auto b = pbase(); b < pptr()-1; b++) {
//         //printf("%c",*b);
//         //printf("%c[%.2X]",*b, *b);
//         bool sendSuccess = m_iec->sendByte(*b);
//         //bool sendSuccess = true;
//         if(sendSuccess && !(IEC.flags bitand ATN_ASSERTED) ) written++;
//         else if(!sendSuccess) {
//             // JAIME: what should happen here? should the badbit be set when send returns false?
//             setstate(badbit);
//             setp(data+written, data+IEC_BUFFER_SIZE); // set pbase to point to next unwritten char
//             Debug_printv("IEC acknowledged %d bytes, then failed\n", written);
//             return written;
//         }
//         else {
//             // ATN was asserted
//             setp(data+written, data+IEC_BUFFER_SIZE); // set pbase to point to next unwritten char
//             Debug_printv("IEC acknowledged %d bytes, then ATN was asserted\n", written);
//             return written;
//         }
//     }

    
//     // probably more bytes to come, so
//     // here we wrote all buffer chars but the last one.
//     // we will take this last byte, put it at position 0 and set pptr to 1
//     char lastChar = *(pbase()+written);
//     setp(data, data+IEC_BUFFER_SIZE); // reset the beginning and ending buffer pointers
//     pbump(1); // and set pptr to 1 to tell there's 1 byte in our buffer
//     data[0] = lastChar; // let's put it at position 0
//     Debug_printv("---> LAST [%.2X]\n", data[0]);
//     Debug_printv("IEC acknowledged %d bytes\n", written);

//     return written;
// }

// int oiecstream::overflow(int ch) {
//     if (!is_open())
//     {
//         return EOF;
//     }

//     Debug_printv("overflow for iec called, size=%d", pptr()-pbase());
//     char* end = pptr();
//     if ( ch != EOF ) {
//         pbump(1);
//         *end ++ = ch;
//     }

//     size_t written = sendBytesViaIEC();

//     if ( written == 0 ) {
//         ch = EOF;
//     } else if ( ch == EOF ) {
//         ch = 0;
//     }
    
//     return ch;
// };

// int oiecstream::sync() { 
//     if(pptr()-pbase() <= 1) {
//         Debug_printv("sync for iec called - nothing more to write");
//         return 0;
//     }
//     else {
//         Debug_printv("sync for iec called - buffer contains %d bytes", pptr()-pbase());
//         auto result = sendBytesViaIEC(); 
//         return (result != 0) ? 0 : -1;  
//     }  
// };


// /********************************************************
//  * oiecstream
//  * 
//  * Standard C++ stream for writing to IEC
//  ********************************************************/

// void oiecstream::putUtf8(U8Char* codePoint) {
//     //printf("%c",codePoint->toPetscii());
//     //Debug_printv("oiecstream calling put");
//     auto c = codePoint->toPetscii();
//     put(c);
// }

//     // void oiecstream::writeLn(std::string line) {
//     //     // line is utf-8, convert to petscii

//     //     std::string converted;
//     //     std::stringstream ss(line);

//     //     while(!ss.eof()) {
//     //         U8Char codePoint(&ss);
//     //         converted+=codePoint.toPetscii();
//     //     }

//     //     Debug_printv("UTF8 converted to PETSCII:%s",converted.c_str());

//     //     (*this) << converted;
//     // }
// #endif /* BUILD_IEC */
