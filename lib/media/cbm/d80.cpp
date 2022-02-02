#include "d80.h"

/********************************************************
 * File implementations
 ********************************************************/

MIStream* D80File::createIStream(std::shared_ptr<MIStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D80IStream(containerIstream);
}