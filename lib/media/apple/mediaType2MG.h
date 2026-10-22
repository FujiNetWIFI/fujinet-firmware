#ifndef _MEDIATYPE_2MG_
#define _MEDIATYPE_2MG_

#include "mediaTypePO.h"

// A ProDOS-order image behind a 64-byte 2MG header
class MediaType2MG : public MediaTypePO
{
public:
    mediatype_t mount(fnFile *f, uint32_t disksize) override;
};

#endif // _MEDIATYPE_2MG_
