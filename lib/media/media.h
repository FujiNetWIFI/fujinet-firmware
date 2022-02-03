#ifndef MEDIA_H
#define MEDIA_H

#ifdef BUILD_ATARI
# include "atari/diskType.h"
# include "atari/diskTypeAtr.h"
# include "atari/diskTypeAtx.h"
# include "atari/diskTypeXex.h"
#endif

#ifdef BUILD_CBM
# include "cbm/mediaType.h"
#endif

#ifdef BUILD_ADAM
# include "adam/mediaType.h"
# include "adam/mediaTypeDDP.h"
# include "adam/mediaTypeDSK.h"
# include "adam/mediaTypeROM.h"
#endif

#ifdef BUILD_APPLE
# include "apple/mediaType.h"
#endif 

#ifdef NEW_TARGET
# include "new/mediaType.h"
# include "new/mediaTypeDDP.h"
# include "new/mediaTypeDSK.h"
# include "new/mediaTypeROM.h"
#endif

#endif // MEDIA_H