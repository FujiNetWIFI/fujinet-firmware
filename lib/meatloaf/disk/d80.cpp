#include "d80.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D80File::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D80IStream(containerIstream);
}