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
# include "cbm/d64.h"
# include "cbm/d71.h"
# include "cbm/d80.h"
# include "cbm/d81.h"
# include "cbm/d82.h"
# include "cbm/d8b.h"
# include "cbm/t64.h"
# include "cbm/tcrt.h"
#endif

#ifdef BUILD_ADAM
# include "adam/mediaType.h"
# include "adam/mediaTypeDDP.h"
# include "adam/mediaTypeDSK.h"
# include "adam/mediaTypeROM.h"
#endif

#ifdef NEW_TARGET
# include "new/mediaType.h"
# include "new/mediaTypeDDP.h"
# include "new/mediaTypeDSK.h"
# include "new/mediaTypeROM.h"
#endif

#endif // MEDIA_H