#include "d8b.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D8BFile::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D8BIStream(containerIstream);
}
