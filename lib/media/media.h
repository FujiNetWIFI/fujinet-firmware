#ifndef MEDIA_H
#define MEDIA_H

#if defined( BUILD_ATARI )
#   include "atari/diskType.h"
#   include "atari/diskTypeAtr.h"
#   include "atari/diskTypeAtx.h"
#   include "atari/diskTypeXex.h"
#elif defined( BUILD_CBM )
#   include "cbm/mediaType.h"
#elif defined( BUILD_ADAM )
#   include "adam/mediaType.h"
#   include "adam/mediaTypeDDP.h"
#   include "adam/mediaTypeDSK.h"
#   include "adam/mediaTypeROM.h"
#elif defined ( BUILD_APPLE )
#   include "apple/mediaType.h"
#endif

#endif // MEDIA_H