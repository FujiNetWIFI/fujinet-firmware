#include "dfi.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* DFIFile::createIStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new DFIIStream(containerIstream);
}
