#ifndef RS232ROMFORMATS_H
#define RS232ROMFORMATS_H

#include "mediatype.h"
#include "MediaFormats.h"

#include "rom/Atari2600ROM.h"
#include "rom/Atari5200ROM.h"
#include "rom/Atari7800ROM.h"
#include "rom/ChannelFROM.h"
#include "rom/ColecoROM.h"
#include "rom/IntellicartROM.h"
#include "rom/IntellivisionROM.h"
#include "rom/MSXROM.h"
#include "rom/NESROM.h"
#include "rom/SegaMasterROM.h"
#include "rom/StudioIIROM.h"

// The first format to accept a file wins, so those with better evidence than the
// extension go first.
using RS232ROMFormats = MediaFormats<MEDIATYPE_UNKNOWN,
                                     MediaFormat<IntellicartROM, MEDIATYPE_ROM>,
                                     MediaFormat<NESROM, MEDIATYPE_ROM>,
                                     MediaFormat<MSXROM, MEDIATYPE_ROM>,
                                     MediaFormat<IntellivisionROM, MEDIATYPE_ROM>,
                                     MediaFormat<Atari7800ROM, MEDIATYPE_ROM>,
                                     MediaFormat<StudioIIROM, MEDIATYPE_ROM>,
                                     MediaFormat<Atari5200ROM, MEDIATYPE_ROM>,
                                     MediaFormat<Atari2600ROM, MEDIATYPE_ROM>,
                                     MediaFormat<SegaMasterROM, MEDIATYPE_ROM>,
                                     MediaFormat<ChannelFROM, MEDIATYPE_ROM>,
                                     MediaFormat<ColecoROM, MEDIATYPE_ROM>>;

#endif /* RS232ROMFORMATS_H */
