#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#define UNOCART      1
#define PLUSCART     2
#define PICOCART     3

#define MENU_TYPE    PLUSCART

#define VERSION                   "2.3.17"
#define PLUSSTORE_API_HOST        "pluscart.firmaplus.de"

// multicore flags
#define EMU_EXITED            0x0F
#define EMU_PLUSROM_CONFIG    0x0C
#define EMU_PLUSROM_SENDSTART 0x01
#define EMU_PLUSROM_RECVSTART 0x02
#define EMU_PLUSROM_RECVDONE  0x03

// buffers
#define BUFFER_SIZE     96
#define BUFFER_SIZE_KB  96
#define ERAM_SIZE_KB    32    // extra RAM

extern char http_request_header[];
extern uint8_t buffer[];
extern unsigned int cart_size_bytes;

#endif // GLOBAL_H
