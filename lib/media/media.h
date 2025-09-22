#ifndef MEDIA_H
#define MEDIA_H

#ifdef BUILD_ATARI
# include "atari/diskType.h"
# include "atari/diskTypeAtr.h"
# include "atari/diskTypeAtx.h"
# include "atari/diskTypeXex.h"
#endif

#ifdef BUILD_RS232
# include "rs232/diskType.h"
# include "rs232/diskTypeImg.h"
#endif

#ifdef BUILD_IEC
# include "cbm/mediaType.h"
#endif

#ifdef BUILD_ADAM
# include "adam/mediaType.h"
# include "adam/mediaTypeDDP.h"
# include "adam/mediaTypeDSK.h"
# include "adam/mediaTypeROM.h"
#endif

#ifdef BUILD_LYNX
#include "lynx/mediaType.h"
#include "lynx/mediaTypeROM.h"
#endif

#ifdef BUILD_APPLE
# include "apple/mediaType.h"
# include "apple/mediaTypeDO.h"
# include "apple/mediaTypePO.h"
# include "apple/mediaTypeWOZ.h"
# include "apple/mediaTypeDSK.h"
#endif

#ifdef BUILD_MAC
#include "mac/mediaType.h"
#include "mac/mediaTypeMOOF.h"
#include "mac/mediaTypeDCD.h"
#endif

#ifdef BUILD_S100
# include "adam/mediaType.h"
# include "adam/mediaTypeDSK.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/mediaType.h"
# include "rc2014/mediaTypeIMG.h"
#endif

#ifdef BUILD_H89
# include "h89/mediaType.h"
# include "h89/mediaTypeIMG.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/mediaType.h"
# include "drivewire/mediaTypeDSK.h"
# include "drivewire/mediaTypeMRM.h"
# include "drivewire/mediaTypeVDK.h"
#endif

#ifdef NEW_TARGET
# include "new/mediaType.h"
# include "new/mediaTypeDDP.h"
# include "new/mediaTypeDSK.h"
# include "new/mediaTypeROM.h"
#endif

#ifdef BUILD_CX16
#include "cx16/mediaType.h"
#endif

#endif // MEDIA_H