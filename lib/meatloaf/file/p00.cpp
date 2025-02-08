#include "p00.h"

//#include "meat_broker.h"

/********************************************************
 * Streams
 ********************************************************/

uint32_t P00MStream::readFile(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    bytesRead += containerStream->read(buf, size);
    _position += bytesRead;

    return bytesRead;
}


