/*
 * cartridge_detection.c
 *
 *  Created on: 31.10.2019
 *      Author: Wolfgang Stubig <w.stubig@firmaplus.de>
 */

#include <string.h>
#include <stdint.h>
#include "cartridge_detection.h"
#include "global.h"

const __in_flash("ext_to_cart_type_map") EXT_TO_CART_TYPE_MAP ext_to_cart_type_map[] = {
   {"ROM",  { base_type_None,    false, false, false, false }},
   {"BIN",  { base_type_None,    false, false, false, false }},
   {"A26",  { base_type_None,    false, false, false, false }},
   {"2K",   { base_type_2K,      false, false, false, false }},
   {"4K",   { base_type_4K,      false, false, false, false }},
   {"4KS",  { base_type_4K,      true,  false, false, false }},
   {"F8",   { base_type_F8,      false, false, false, false }},
   {"F6",   { base_type_F6,      false, false, false, false }},
   {"F4",   { base_type_F4,      false, false, false, false }},
   {"F8S",  { base_type_F8,      true,  false, false, false }},
   {"F6S",  { base_type_F6,      true,  false, false, false }},
   {"F4S",  { base_type_F4,      true,  false, false, false }},
   {"FE",   { base_type_FE,      false, false, false, false }},
   {"3F",   { base_type_3F,      false, false, false, false }},
   {"3E",   { base_type_3E,      false, false, false, false }},
   {"E0",   { base_type_E0,      false, false, false, false }},
   {"084",  { base_type_0840,    false, false, false, false }},
   {"CV",   { base_type_CV,      false, false, false, false }},
   {"EF",   { base_type_EF,      false, false, false, false }},
   {"EFS",  { base_type_EF,      true,  false, false, false }},
   {"F0",   { base_type_F0,      false, false, false, false }},
   {"FA",   { base_type_FA,      false, false, false, false }},
   {"E7",   { base_type_E7,      false, false, false, false }},
   {"DPC",  { base_type_DPC,     false, false, true,  true  }},
   {"AR",   { base_type_AR,      false, false, false, false }},
   {"BF",   { base_type_BF,      false, false, true,  false }},
   {"BFS",  { base_type_BFSC,    false, false, true,  false }},
   {"ACE",  { base_type_ACE,     false, false, false, false }},
   {"WD",   { base_type_PP,      false, false, false, false }},
   {"DF",   { base_type_DF,      false, false, true,  false }},
   {"DFS",  { base_type_DFSC,    false, false, true,  false }},
   {"3EP",  { base_type_3EPlus,  false, false, false, false }},
   {"DPCP", { base_type_DPCplus, false, false, false, true  }},
   {"SB",   { base_type_SB,      false, false, true,  false }},
   {"UA",   { base_type_UA,      false, false, false, false }},
   {"ELF",  { base_type_ELF,     false, false, false, false }},

   {0, {base_type_None, 0, 0}}
};

/*************************************************************************
 * Cartridge Type Detection
 *************************************************************************/
int isValidHostChar(char c) {
   return (c == 45 || c == 46 || (c > 47 && c < 58) || (c > 64 && c < 91) || (c > 96 && c < 123));
}

/*
 * basicly these Chars are allowed in path of URI:
 * pchar       = unreserved / pct-encoded / sub-delims / ":" / "@"
 * pct-encoded = "%" HEXDIG HEXDIG
 * unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
 * sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
 *             / "*" / "+" / "," / ";" / "="
 *
 * but we don't allow Search-String chars too
 */
int isValidPathChar(char c) {
   return ((c > 44 && c < 58) || (c > 64 && c < 91) || (c > 96 && c < 123));
}

int searchForBytes(unsigned char *bytes, unsigned int size, unsigned char *signature, unsigned int sigsize, int minhits) {
   int count = 0;

   for(unsigned int i = 0; i < size - sigsize; ++i) {
      int matches = 0;

      for(unsigned int j = 0; j < sigsize; ++j) {
         if(bytes[i+j] == signature[j])
            ++matches;
         else
            break;
      }

      if(matches == sigsize) {
         ++count;
         i += sigsize;  // skip past this signature 'window' entirely
      }

      if(count >= minhits)
         break;
   }

   return (count >= minhits);
}

int isProbablyPLS(unsigned int size, unsigned char *bytes) {
   uint16_t nmi_p = (bytes[size-5] << 8) + bytes[size-6];
   int i = nmi_p - 0x1000, hostHasNoDot = 1;

   if(i < 0)
      return 0;

   while(i < size && isValidPathChar(bytes[i]))
      i++;

   if(i >= size || bytes[i] != 0)
      return 0;

   i++;

   while(i < size && isValidHostChar(bytes[i])) {
      if(bytes[i] == 46)
         hostHasNoDot = 0;

      i++;
   }

   // we do not allow dotless hostnames or IP address strings. API on TLD not possible
   if(i >= size || bytes[i] != 0 || i < 6 || hostHasNoDot) {
      return 0;
   }

   unsigned char signature[] = { 0x8D, 0xF1, 0x1F };  // STA $1FF1 // Send write buffer signature

   return searchForBytes(bytes, size, signature, 3, 1);
}

int isPotentialF8(unsigned int size, unsigned char *bytes) {
   unsigned char  signature[] = { 0x8D, 0xF9, 0x1F };  // STA $1FF9
   return searchForBytes(bytes, size, signature, 3, 2);
}

/* The following detection routines are modified from the Atari 2600 Emulator Stella
  (https://github.com/stella-emu) */
int isProbablyUA(unsigned int size, unsigned char *bytes) {
   // UA cart bankswitching switches to bank 1 by accessing address 0x240
   // using 'STA $240' or 'LDA $240'
   // Similar Brazilian (Digivison) cart bankswitching switches to bank 1 by accessing address 0x2C0
   // using 'BIT $2C0', 'STA $2C0' or 'LDA $2C0'
   // Other Brazilian (Atari Mania) ROM's bankswitching switches to bank 1 by accessing address 0xFC0
   // using 'BIT $FA0', 'BIT $FC0' or 'STA $FA0'
   unsigned char signature[7][3] = {
      { 0x8D, 0x40, 0x02 },  // STA $240 (Funky Fish, Pleiades)
      { 0xAD, 0x40, 0x02 },  // LDA $240 (???)
      { 0xBD, 0x1F, 0x02 },  // LDA $21F,X (Gingerbread Man)
      { 0x2C, 0xC0, 0x02 },  // BIT $2C0 (Time Pilot)
      { 0x8D, 0xC0, 0x02 },  // STA $2C0 (Fathom, Vanguard)
      { 0xAD, 0xC0, 0x02 },  // LDA $2C0 (Mickey)
      { 0x2C, 0xC0, 0x0F }   // BIT $FC0 (H.E.R.O., Kung-Fu Master)
   };

   for(int i = 0; i < 7; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbablySC(unsigned int size, unsigned char *bytes) {
   unsigned int banks = size/4096;

   // check 2K for SC
   if(banks == 0 && size >= 256)
      banks++;

   for(int i = 0; i < banks; i++) {
      for(int j = 0; j < 128; j++) {
         if(bytes[i*4096+j] != bytes[i*4096+j+128])
            return 0;
      }
   }

   return 1;
}

int isProbablyFE(unsigned int size, unsigned char *bytes) {
   // These signatures are attributed to the MESS project
   unsigned char signature[4][5] = {
      { 0x20, 0x00, 0xD0, 0xC6, 0xC5 },  // JSR $D000; DEC $C5
      { 0x20, 0xC3, 0xF8, 0xA5, 0x82 },  // JSR $F8C3; LDA $82
      { 0xD0, 0xFB, 0x20, 0x73, 0xFE },  // BNE $FB; JSR $FE73
      { 0x20, 0x00, 0xF0, 0x84, 0xD6 }   // JSR $F000; STY $D6
   };

   for(int i = 0; i < 4; ++i)
      if(searchForBytes(bytes, size, signature[i], 5, 1))
         return 1;

   return 0;
}

int isProbably3F(unsigned int size, unsigned char *bytes) {
   // 3F cart bankswitching is triggered by storing the bank number
   // in address 3F using 'STA $3F'
   // We expect it will be present at least 2 times, since there are
   // at least two banks
   unsigned char signature[] = { 0x85, 0x3F };  // STA $3F
   return searchForBytes(bytes, size, signature, 2, 2);
}

int isProbably3E(unsigned int size, unsigned char *bytes) {
   // 3E cart bankswitching is triggered by storing the bank number
   // in address 3E using 'STA $3E', commonly followed by an
   // immediate mode LDA
   unsigned char  signature[] = { 0x85, 0x3E, 0xA9, 0x00 };  // STA $3E; LDA #$00
   return searchForBytes(bytes, size, signature, 4, 1);
}

int isProbably3EPlus(unsigned int size, unsigned char *bytes) {
   // 3E+ cart is identified by key 'TJ3E' in the ROM
   unsigned char  signature[] = { 'T', 'J', '3', 'E' };
   return searchForBytes(bytes, size, signature, 4, 1);
}

int isProbablyE0(unsigned int size, unsigned char *bytes) {
   // E0 cart bankswitching is triggered by accessing addresses
   // $FE0 to $FF9 using absolute non-indexed addressing
   // These signatures are attributed to the MESS project
   unsigned char signature[8][3] = {
      { 0x8D, 0xE0, 0x1F },  // STA $1FE0
      { 0x8D, 0xE0, 0x5F },  // STA $5FE0
      { 0x8D, 0xE9, 0xFF },  // STA $FFE9
      { 0x0C, 0xE0, 0x1F },  // NOP $1FE0
      { 0xAD, 0xE0, 0x1F },  // LDA $1FE0
      { 0xAD, 0xE9, 0xFF },  // LDA $FFE9
      { 0xAD, 0xED, 0xFF },  // LDA $FFED
      { 0xAD, 0xF3, 0xBF }   // LDA $BFF3
   };

   for(int i = 0; i < 8; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbably0840(unsigned int size, unsigned char *bytes) {
   // 0840 cart bankswitching is triggered by accessing addresses 0x0800
   // or 0x0840 at least twice
   unsigned char signature1[3][3] = {
      { 0xAD, 0x00, 0x08 },  // LDA $0800
      { 0xAD, 0x40, 0x08 },  // LDA $0840
      { 0x2C, 0x00, 0x08 }   // BIT $0800
   };

   for(int i = 0; i < 3; ++i)
      if(searchForBytes(bytes, size, signature1[i], 3, 2))
         return 1;

   unsigned char signature2[2][4] = {
      { 0x0C, 0x00, 0x08, 0x4C },  // NOP $0800; JMP ...
      { 0x0C, 0xFF, 0x0F, 0x4C }   // NOP $0FFF; JMP ...
   };

   for(int i = 0; i < 2; ++i)
      if(searchForBytes(bytes, size, signature2[i], 4, 2))
         return 1;

   return 0;
}

int isProbablyCV(unsigned int size, unsigned char *bytes) {
   // CV RAM access occurs at addresses $f3ff and $f400
   // These signatures are attributed to the MESS project
   unsigned char signature[2][3] = {
      { 0x9D, 0xFF, 0xF3 },  // STA $F3FF.X
      { 0x99, 0x00, 0xF4 }   // STA $F400.Y
   };

   for(int i = 0; i < 2; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbablyEF(unsigned int size, unsigned char *bytes) {
   // EF cart bankswitching switches banks by accessing addresses
   // 0xFE0 to 0xFEF, usually with either a NOP or LDA
   // It's likely that the code will switch to bank 0, so that's what is tested
   unsigned char signature[4][3] = {
      { 0x0C, 0xE0, 0xFF },  // NOP $FFE0
      { 0xAD, 0xE0, 0xFF },  // LDA $FFE0
      { 0x0C, 0xE0, 0x1F },  // NOP $1FE0
      { 0xAD, 0xE0, 0x1F }   // LDA $1FE0
   };

   for(int i = 0; i < 4; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbablyE7(unsigned int size, unsigned char *bytes) {
   // These signatures are attributed to the MESS project
   unsigned char signature[7][3] = {
      { 0xAD, 0xE2, 0xFF },  // LDA $FFE2
      { 0xAD, 0xE5, 0xFF },  // LDA $FFE5
      { 0xAD, 0xE5, 0x1F },  // LDA $1FE5
      { 0xAD, 0xE7, 0x1F },  // LDA $1FE7
      { 0x0C, 0xE7, 0x1F },  // NOP $1FE7
      { 0x8D, 0xE7, 0xFF },  // STA $FFE7
      { 0x8D, 0xE7, 0x1F }   // STA $1FE7
   };

   for(int i = 0; i < 7; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbablyE78K(unsigned int size, unsigned char *bytes) {
   // E78K cart bankswitching is triggered by accessing addresses
   // $FE4 to $FE6 using absolute non-indexed addressing
   // To eliminate false positives (and speed up processing), we
   // search for only certain known signatures
   unsigned char signature[3][3] = {
      { 0xAD, 0xE4, 0xFF },  // LDA $FFE4
      { 0xAD, 0xE5, 0xFF },  // LDA $FFE5
      { 0xAD, 0xE6, 0xFF },  // LDA $FFE6
   };

   for(int i = 0; i < 3; ++i)
      if(searchForBytes(bytes, size, signature[i], 3, 1))
         return 1;

   return 0;
}

int isProbablyBF(unsigned char *tail) {
   return !memcmp(tail + 8, "BFBF", 4);
}

int isProbablyBFSC(unsigned char *tail) {
   return !memcmp(tail + 8, "BFSC", 4);
}

int isProbablyDF(unsigned char *tail) {
   return !memcmp(tail + 8, "DFBF", 4);
}

int isProbablyDFSC(unsigned char *tail) {
   return !memcmp(tail + 8, "DFSC", 4);
}

int isProbablyDPCplus(unsigned int size, unsigned char *bytes) {
   // DPC+ ARM code has 2 occurrences of the string DPC+
   // Note: all Harmony/Melody custom drivers also contain the value
   // 0x10adab1e (LOADABLE) if needed for future improvement
   unsigned char  signature[] = { 'D', 'P', 'C', '+' };
   return searchForBytes(bytes, size, signature, 4, 2);
}

int isProbablySB(unsigned int size, unsigned char *bytes) {
   // SB cart bankswitching switches banks by accessing address 0x0800
   unsigned char signature[2][3] = {
      { 0xBD, 0x00, 0x08 },  // LDA $0800,x
      { 0xAD, 0x00, 0x08 }   // LDA $0800
   };

   if(searchForBytes(bytes, size, signature[0], 3, 1))
      return 1;
   else
      return searchForBytes(bytes, size, signature[1], 3, 1);
}
