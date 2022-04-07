#ifdef BUILD_CBM

#include "d81.h"

/********************************************************
 * File implementations
 ********************************************************/

MIStream* D81File::createIStream(std::shared_ptr<MIStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D81IStream(containerIstream);
}

#endif /* BUILD_CBM */