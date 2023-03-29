#include "d81.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* D81File::createIStream(std::shared_ptr<MStream> containerIstream) {
    //Debug_printv("[%s]", url.c_str());

    return new D81IStream(containerIstream);
}
