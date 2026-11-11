#ifndef BUSCHARSET_H
#define BUSCHARSET_H

#include "bus.h"
#include "fnpretty.h"

/**
 * The PrettyCharset matching the bus's native character encoding: what PRETTY's
 * "!gfx=1" selects.
 */
inline PrettyCharset busNativeCharset()
{
    switch (SYSTEM_BUS.nativeEncoding())
    {
    case SystemBusBase::NativeEncoding::ATASCII:
        return PrettyCharset::Atascii;
    case SystemBusBase::NativeEncoding::APPLE2:
        // Only an enhanced //e or later has MouseText; the client knows
        // which it is and asks for native graphics only there.
        return PrettyCharset::MouseText;
    default:
        return PrettyCharset::Ascii;
    }
}

#endif /* BUSCHARSET_H */
