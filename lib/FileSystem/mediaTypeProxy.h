#ifndef MEDIA_TYPE_PROXY_H
#define MEDIA_TYPE_PROXY_H

#ifdef BUILD_ATARI
#include "../media/atari/diskType.h"
#elif BUILD_ADAM
#include "../media/adam/mediaType.h"
#elif BUILD_APPLE
#include "../media/apple/mediaType.h"
#elif BUILD_MAC
#include "../media/apple/mediaType.h"
#elif BUILD_IEC
#include "../media/cbm/mediaType.h"
#elif BUILD_LYNX
#include "../media/lynx/mediaType.h"
#elif BUILD_S100
#include "../media/s100spi/mediaType.h"
#elif BUILD_RS232
#include "../media/rs232/diskType.h"
#elif BUILD_CX16
#include "../media/cx16/mediaType.h"
#elif BUILD_RC2014
#include "../media/rc2014/mediaType.h"
#elif BUILD_H89
#include "../media/h89/mediaType.h"
#elif BUILD_COCO
#include "../media/drivewire/mediaType.h"
#endif

#endif