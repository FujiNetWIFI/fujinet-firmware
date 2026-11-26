#ifndef COCOROMFORMATS_H
#define COCOROMFORMATS_H

#include "mediatype.h"
#include "MediaFormats.h"

#include "rom/CoCoCartROM.h"

using CoCoROMFormats = MediaFormats<MEDIATYPE_UNKNOWN,
                                    MediaFormat<CoCoCartROM, MEDIATYPE_ROM>>;

#endif /* COCOROMFORMATS_H */
