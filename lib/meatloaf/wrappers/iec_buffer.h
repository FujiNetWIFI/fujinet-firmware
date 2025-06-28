// #ifdef BUILD_IEC
// #ifndef MEATLOAF_WRAPPER_IEC_BUFFER
// #define MEATLOAF_WRAPPER_IEC_BUFFER

// #include "../../../include/debug.h"

// #if HOST_OS==win32
// #include "../../bus/bus.h"
// #else
// #include "bus.h"
// #endif

// #include <string>
// #include <iostream>
// #include <fstream>
// #include <sstream>
// #include <memory>
// #include "U8Char.h"

// #define IEC_BUFFER_SIZE 255 // 1024

// /********************************************************
//  * oiecstream
//  * 
//  * Standard C++ stream for writing to IEC
//  * 
//  * This is of course a bit counter-intuitive:
//  * 
//  * Writing to iecstream == LOAD on C64 (pipe mode: _S_in), we're doing file -> IEC -> C64
//  * Reading from iestream == SAVE on C64 (pipe mode: _S_out), we're doing C64 -> IEC -> file
//  ********************************************************/

// class oiecstream : private std::filebuf, public std::ostream {
//     char* data = nullptr;
//     systemBus* m_iec = nullptr;
//     bool _is_open = false;

//     size_t sendBytesViaIEC();
//     size_t receiveBytesViaIEC();

// public:
//     oiecstream(const oiecstream &copied) : std::ios(0), std::filebuf(),  std::ostream( this ) {
//         Debug_printv("oiecstream COPY constructor");
//     }

//     oiecstream() : std::ostream( this ) {
//         Debug_printv("oiecstream constructor");

//         data = new char[IEC_BUFFER_SIZE+1];
//         setp(data, data+IEC_BUFFER_SIZE);
//     };

//     ~oiecstream() {
//         Debug_printv("oiecstream destructor");

//         close();
        
//         if(data != nullptr)
//             delete[] data;
//     }


//     virtual void open(systemBus* iec) {
//         m_iec = iec;
//         setp(data, data+IEC_BUFFER_SIZE);
//         if(iec != nullptr)
//         {
//             _is_open = true;
//             clear();
//         }
//     }

//     virtual void close() {
//         if(_is_open) {
//             sync();

//             if(pptr()-pbase() == 1) {
//                 char last = data[0];
//                 Debug_printv("closing, sending EOI with [%.2X] %c", last, last);
//                 m_iec->transmitIECByte(last);
//                 setp(data, data+IEC_BUFFER_SIZE);
//             }

//             _is_open = false;
//         }
//     }

//     bool is_open() const {
//         return _is_open;
//     }    

//     int overflow(int ch  = std::filebuf::traits_type::eof()) override;

//     int sync() override;

//     void putUtf8(U8Char* codePoint);

//     void flushpbuff();
// };

// extern oiecstream iecStream;

// #endif /* MEATLOAF_WRAPPER_IEC_BUFFER */
// #endif /* BUILD_IEC */