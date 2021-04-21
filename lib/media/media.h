#ifndef MEDIA_H
#define MEDIA_H

#include "fnSystem.h"

#if defined( BUILD_ATARI )
#   include "atari/diskType.h"
#   include "atari/diskTypeAtr.h"
#   include "atari/diskTypeAtx.h"
#   include "atari/diskTypeXex.h"
#elif defined( BUILD_CBM )
#   include "cbm/diskType.h"
#endif

#endif // MEDIA_H