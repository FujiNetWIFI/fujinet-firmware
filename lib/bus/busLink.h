#ifndef BUSLINK_H
#define BUSLINK_H

/*
 *  Bus Link
 *
 *  The interface to communicate with System Bus:
 *    SIO Serial Port, SIO over UDP, plain UART and GPIO PINs, ...
 */

#ifdef BUILD_ATARI
#include "sio/link/fnSioLink.h"
#define SYSTEM_BUS_LINK fnSioLink
#endif

#ifdef BUILD_CBM
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef BUILD_ADAM
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef BUILD_LYNX
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef NEW_TARGET
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef BUILD_APPLE
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef BUILD_S100
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#ifdef BUILD_RS232
#include "fnUART.h"
#define SYSTEM_BUS_LINK fnUartSIO
#endif

#endif // BUSLINK_H