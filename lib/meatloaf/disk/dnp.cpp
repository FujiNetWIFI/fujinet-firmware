
#include "dnp.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* DNPFile::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new DNPIStream(containerIstream);
}
