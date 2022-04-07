#ifdef BUILD_CBM

#include "d82.h"

/********************************************************
 * File implementations
 ********************************************************/

MIStream* D82File::createIStream(std::shared_ptr<MIStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new D82IStream(containerIstream);
}

#endif /* BUILD_CBM */