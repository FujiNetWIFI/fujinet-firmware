#ifndef BUS_H
#define BUS_H

// #include "fnSystem.h"

// union cmdFrame_t
// {
//     struct
//     {
//         uint8_t device;
//         uint8_t comnd;
//         uint8_t aux1;
//         uint8_t aux2;
//         uint8_t cksum;
//     };
//     struct
//     {
//         uint32_t commanddata;
//         uint8_t checksum;
//     } __attribute__((packed));
// };

// enum bus_message : uint16_t
// {
//     BUSMSG_DISKSWAP,  // Rotate disk
//     BUSMSG_DEBUG_TAPE // Tape debug msg
// };

// struct bus_message_t
// {
//     bus_message message_id;
//     uint16_t message_arg;
// };

// typedef bus_message_t bus_message_t;

#ifdef BUILD_ATARI
# include "sio/sio.h"
# define BUS_CLASS SIO

#elif BUILD_CBM
# include "iec/iec.h"
# define BUS_CLASS IEC

#elif BUILD_ADAM
# include "adamnet/adamnet.h"
# define BUS_CLASS AdamNet

#elif NEW_TARGET
# include ".new/adamnet.h"
# define BUS_CLASS AdamNet
#endif

#endif // BUS_H