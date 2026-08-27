#ifndef FNUSERAGENT_H
#define FNUSERAGENT_H

#include "version.h"

/**
 * The User-Agent FujiNet presents to origin servers.
 *
 * Sending no User-Agent at all gets us rejected outright by some sites:
 * Wikipedia answers such a request with 403 under its User-Agent policy
 * (https://foundation.wikimedia.org/wiki/Policy:User-Agent_policy), which
 * asks clients to identify the software and offer a way to make contact.
 * That policy also discourages generic strings, so name the product, its
 * version, the vintage platform we are bridging for, and the project URL
 * rather than something anonymous like "HTTP Client/1.0".
 *
 * The platform token is genuinely useful to servers beyond politeness -
 * it is the only hint they get that the other end of this request is an
 * 8-bit machine rendering to a 40-column screen.
 */

#if defined(BUILD_ATARI)
#  define FN_UA_PLATFORM "Atari"
#elif defined(BUILD_APPLE)
#  define FN_UA_PLATFORM "Apple II"
#elif defined(BUILD_COCO)
#  define FN_UA_PLATFORM "CoCo"
#elif defined(BUILD_ADAM)
#  define FN_UA_PLATFORM "ADAM"
#elif defined(BUILD_LYNX)
#  define FN_UA_PLATFORM "Lynx"
#elif defined(BUILD_RS232)
#  define FN_UA_PLATFORM "RS232"
#else
#  define FN_UA_PLATFORM "Unknown"
#endif

#define FN_USER_AGENT "FujiNet/" FN_VERSION_FULL " (" FN_UA_PLATFORM "; +https://fujinet.online/)"

#endif /* FNUSERAGENT_H */
