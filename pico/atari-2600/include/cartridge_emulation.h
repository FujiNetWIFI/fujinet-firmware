#ifndef CARTRIDGE_EMULATION_H
#define CARTRIDGE_EMULATION_H

#include <stdint.h>
#include <stdbool.h>
#include <WiFiEspAT.h>
#include "global.h"
#include "plusrom.h"
#include "hardware/sync.h"
#include "cartridge_detection.h"

// multiproc queue to sync start/stop cartridge and PlusROM communications
extern queue_t qprocs;

// multiproc queue to pass function arguments as memory pointer
extern queue_t qargs;

// multicore sync
extern const uint8_t emuexit, sendstart, recvdone;

void exit_cartridge(uint16_t, uint16_t);

/* 'Standard' Bankswitching */
//void emulate_standard_cartridge(bool, uint16_t, uint16_t, int);
void emulate_standard_cartridge(CART_TYPE *cart_type);
// multicore wrapper
void _emulate_standard_cartridge(void);

/* UA Bankswitching */
void emulate_UA_cartridge(void);
// multicore wrapper
void _emulate_UA_cartridge(void);

/* FA (CBS RAM plus) Bankswitching */
void emulate_FA_cartridge(CART_TYPE *cart_type);
// multicore wrapper
void _emulate_FA_cartridge(void);

/* FE Bankswitching */
void emulate_FE_cartridge();
// multicore wrapper
void _emulate_FE_cartridge();

/* 3F (Tigervision) Bankswitching */
void emulate_3F_cartridge();
// multicore wrapper
void _emulate_3F_cartridge();

/* 3E (3F + RAM) Bankswitching */
void emulate_3E_cartridge(CART_TYPE *cart_type);
// multicore wrapper
void _emulate_3E_cartridge(void);

/* 3E+ Bankswitching by Thomas Jentzsch */
void emulate_3EPlus_cartridge(CART_TYPE *cart_type);
// multicore wrapper
void _emulate_3EPlus_cartridge(void);

/* E0 Bankswitching */
void emulate_E0_cartridge();
// multicore wrapper
void _emulate_E0_cartridge();

void emulate_0840_cartridge();
// multicore wrapper
void _emulate_0840_cartridge();

/* CommaVid Cartridge*/
void emulate_CV_cartridge();
// multicore wrapper
void _emulate_CV_cartridge();

/* F0 Bankswitching */
void emulate_F0_cartridge();
// multicore wrapper
void _emulate_F0_cartridge();

/* E7 Bankswitching */
void emulate_E7_cartridge(CART_TYPE *cart_type);
// multicore wrapper
void _emulate_E7_cartridge(void);

/* DPC (Pitfall II) Bankswitching */
void emulate_DPC_cartridge(uint32_t);
// multicore wrapper
void _emulate_DPC_cartridge(void);

/* Pink Panther */
void emulate_pp_cartridge(uint8_t* ram);
// multicore wrapper
void _emulate_pp_cartridge(void);

#endif // CARTRIDGE_EMULATION_H
