#ifndef CARTRIDGE_DETECTION_H
#define CARTRIDGE_DETECTION_H

#include <stdint.h>

enum cart_base_type {
   base_type_Load_Failed = -1,
   base_type_None,
   base_type_2K,
   base_type_4K,
   base_type_4KSC,
   base_type_F8,
   base_type_F6,
   base_type_F4,
   base_type_FE,
   base_type_3F,
   base_type_3E,
   base_type_E0,
   base_type_0840,
   base_type_CV,
   base_type_EF,
   base_type_F0,
   base_type_FA,
   base_type_E7,
   base_type_DPC,
   base_type_AR,
   base_type_PP,
   base_type_DF,
   base_type_DFSC,
   base_type_BF,
   base_type_BFSC,
   base_type_3EPlus,
   base_type_DPCplus,
   base_type_SB,
   base_type_UA,
   base_type_ACE,
   base_type_ELF
};

typedef struct {
   enum cart_base_type base_type;
   bool withSuperChip;
   bool withPlusFunctions;
   bool uses_ccmram;
   bool uses_systick;
   uint32_t flash_part_address;
} CART_TYPE;

typedef struct {
   const char *ext;
   CART_TYPE cart_type;
} EXT_TO_CART_TYPE_MAP;

extern const EXT_TO_CART_TYPE_MAP ext_to_cart_type_map[];

int isProbablyPLS(unsigned int, unsigned char *);
int isPotentialF8(unsigned int, unsigned char *);

/* The following detection routines are modified from the Atari 2600 Emulator Stella
  (https://github.com/stella-emu) */
int isProbablySC(unsigned int, unsigned char *);
int isProbablyUA(unsigned int, unsigned char *);
int isProbablyFE(unsigned int, unsigned char *);
int isProbably3F(unsigned int, unsigned char *);
int isProbably3E(unsigned int, unsigned char *);
int isProbably3EPlus(unsigned int, unsigned char *);
int isProbablyE0(unsigned int, unsigned char *);
int isProbably0840(unsigned int, unsigned char *);
int isProbablyCV(unsigned int, unsigned char *);
int isProbablyEF(unsigned int, unsigned char *);
int isProbablyE7(unsigned int, unsigned char *);
int isProbablyE78K(unsigned int, unsigned char *);
int isProbablyBF(unsigned char *);
int isProbablyBFSC(unsigned char *);
int isProbablyDF(unsigned char *);
int isProbablyDFSC(unsigned char *);
int isProbablyDPCplus(unsigned int, unsigned char *);
int isProbablySB(unsigned int, unsigned char *);

#endif // CARTRIDGE_DETECTION_H
