#ifdef BUILD_CBM

#include "d71.h"

/********************************************************
 * File implementations
 ********************************************************/

MIStream* D71File::createIStream(std::shared_ptr<MIStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D71IStream(containerIstream);
}

#endif /* BUILD_CBM */