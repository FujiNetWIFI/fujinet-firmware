#include "d71.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D71File::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D71IStream(containerIstream);
}
