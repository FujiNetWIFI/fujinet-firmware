#include "d90.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D90File::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D90IStream(containerIstream);
}