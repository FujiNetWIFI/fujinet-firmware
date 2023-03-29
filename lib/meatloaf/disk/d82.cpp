#include "d82.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D82File::createIStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D82IStream(containerIstream);
}