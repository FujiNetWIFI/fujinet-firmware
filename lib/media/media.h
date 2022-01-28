#ifndef MEDIA_H
#define MEDIA_H

#if BUILD_ATARI
# include "atari/diskType.h"
# include "atari/diskTypeAtr.h"
# include "atari/diskTypeAtx.h"
# include "atari/diskTypeXex.h"

#elif BUILD_CBM
# include "cbm/mediaType.h"

#elif BUILD_ADAM
# include "adam/mediaType.h"
# include "adam/mediaTypeDDP.h"
# include "adam/mediaTypeDSK.h"
# include "adam/mediaTypeROM.h"

#elif NEW_TARGET
# include ".new/mediaType.h"
# include ".new/mediaTypeDDP.h"
# include ".new/mediaTypeDSK.h"
# include ".new/mediaTypeROM.h"
#endif

#endif // MEDIA_H