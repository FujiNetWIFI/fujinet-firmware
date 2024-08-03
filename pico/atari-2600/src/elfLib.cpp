#include <string.h>
#include "elfLib.h"

// Assumes rname is null terminated
int strCmp(const char* lname, const char* rname) {
   for(int i = 0; ; i++) {
      if(lname[i] != rname[i]) {
         return 0;
      }

      if(rname[i] == 0) {
         break;
      }
   }

   return 1;
}

uint8_t* wordAlign(uint8_t* pointer) {
   uint32_t bytesToAdd = ((uint32_t)pointer) & 3;

   if(bytesToAdd != 0) {
      bytesToAdd = 4 - bytesToAdd;
   }

   return pointer += bytesToAdd;
}

int initSectionsMeta(uint8_t* elfBuffer, SectionMetaEntry meta[], uint32_t nobitsLoadAddress) {

   ElfHeader* elf = (ElfHeader*)elfBuffer;
   meta[0].header = 0;
   meta[0].buffer = 0;
   meta[0].loadAddress = 0;
   meta[0].name = 0;
   meta[0].relocation = 0;

   SectionHeader* pStringSection = (SectionHeader*)&elfBuffer[elf->e_shoff + (elf->e_shstrndx * elf->e_shentsize)];

   for(uint32_t i = 1; i < elf->e_shnum; i++) {
      SectionHeader* header = (SectionHeader*)&elfBuffer[elf->e_shoff + i * elf->e_shentsize];
      meta[i].header = header;
      meta[i].buffer = &elfBuffer[header->sh_offset];
      meta[i].loadAddress = header->sh_type == SHT_PROGBITS ? (uint32_t)meta[i].buffer : 0;

      if(header->sh_type == SHT_NOBITS) {
         meta[i].loadAddress = nobitsLoadAddress;
         nobitsLoadAddress += meta[i].header->sh_size;
         nobitsLoadAddress = (uint32_t)wordAlign((uint8_t*)nobitsLoadAddress);
      }

      meta[i].name = (char*)&elfBuffer[pStringSection->sh_offset + header->sh_name];
      meta[i].relocation = 0;
   }

   for(int i = 1; i < elf->e_shnum; i++) {
      if(meta[i].header->sh_type == SHT_REL) {
         for(int j = 1; j < elf->e_shnum; j++) {
            if((meta[j].header->sh_type == SHT_PROGBITS || meta[j].header->sh_type == SHT_PREINIT_ARRAY || meta[j].header->sh_type == SHT_INIT_ARRAY) && strCmp(&meta[i].name[4], meta[j].name)) {
               meta[j].relocation = meta[i].header;
            }
         }
      }
   }

   return 1;
}

uint8_t ElfIdent[7] = {
   0x7f, 'E', 'L', 'F', // Magic #
   1, // 32 bit
   1, // little endian
   1, // elf version
};

int isElf(uint32_t size, uint8_t* buffer) {
   // Check magic number
   ElfHeader* pHeader = (ElfHeader*)buffer;

   for(int i = 0; i < 7; i++) {
      if(pHeader->e_ident[i] != ElfIdent[i]
            || pHeader->e_type != 1 // Relocatable
            || pHeader->e_machine != 40 // ARM
            || pHeader->e_version != 1) {
         // Identity failure
         return 0;
      }
   }

   return 1;
}

// pMainAddress will have bit0 set since it's pointing to a thumb function. Mask bit0 off if looking for physical address of first byte in function
int loadElf(uint8_t* elfBuffer, uint32_t metaCount, SectionMetaEntry meta[], uint32_t* pMainAddress, int* usesVcsWrite3) {
   *pMainAddress = 0;
   *usesVcsWrite3 = 0;
   SectionHeader* symTableSectionHeader = 0;
   ElfHeader* pHeader = (ElfHeader*)elfBuffer;

   // Then iterate the sections and use the strings table to search for sections by name
   SectionHeader* pSection = (SectionHeader*)&elfBuffer[pHeader->e_shoff];

   for(int i = 1; i < pHeader->e_shnum; i++) { // Skip the first section SHN_UNDEF
      pSection++;

      switch(pSection->sh_type) {
         case SHT_SYMTAB: {
            symTableSectionHeader = pSection;
            break;
         }

         default:
            break;
      }
   }

   if(symTableSectionHeader == 0) {
      return 0;
   }

   SectionHeader* pSymStringSection = (SectionHeader*)&elfBuffer[pHeader->e_shoff + (sizeof(SectionHeader) * symTableSectionHeader->sh_link)];

   for(uint32_t i = 0; i < symTableSectionHeader->sh_size; i += symTableSectionHeader->sh_entsize) {
      SymbolEntry* pSymbol = (SymbolEntry*)&elfBuffer[symTableSectionHeader->sh_offset + i];
      char* name = (char*)&elfBuffer[pSymStringSection->sh_offset + pSymbol->st_name];

      if(strCmp(name, "main") || strCmp(name, "elf_main")) {
         if(meta[pSymbol->st_shndx].header->sh_type == SHT_PROGBITS) {
            *pMainAddress = meta[pSymbol->st_shndx].loadAddress + pSymbol->st_value;
         } else {
            return 0;
         }
      } else if(strCmp(name, "vcsWrite3")) {
         *usesVcsWrite3 = 1;
      }
   }

   if(*pMainAddress == 0) {
      return 0;
   }

   for(int i = 1; i < metaCount; i++) {
      if(meta[i].header->sh_type == SHT_NOBITS) {
         memset((uint8_t*)meta[i].loadAddress, 0, meta[i].header->sh_size);
      } else if(meta[i].relocation) {
         if(!relocateSection(&meta[i], symTableSectionHeader, pSymStringSection, elfBuffer, meta)) {
            return 0;
         }
      }
   }

   return 1;
}

int relocateSection(
   SectionMetaEntry* sectionMeta,
   SectionHeader* symTableSectionHeader,
   SectionHeader* pSymStringSection,
   uint8_t* elfBuffer,
   SectionMetaEntry meta[]
) {
   uint8_t* sectionsBuffer = sectionMeta->buffer;
   SectionHeader* relSection = sectionMeta->relocation;

   for(uint32_t i = 0; i < relSection->sh_size; i += relSection->sh_entsize) {
      uint8_t op2 = 0x90;
      RelEntry* pReloc = (RelEntry*)&elfBuffer[relSection->sh_offset + i];
      SymbolEntry* pSymbol = (SymbolEntry*)&elfBuffer[symTableSectionHeader->sh_offset + ((pReloc->info >> 4) & 0xfffffff0)];
      char* name = (char*)&elfBuffer[pSymStringSection->sh_offset + pSymbol->st_name];

      switch(pReloc->info & 0xff) {
         case R_ARM_TARGET1:
         case R_ARM_ABS32: {
            uint32_t val = 0;

            if(pSymbol->st_shndx != 0) {
               val = meta[pSymbol->st_shndx].loadAddress + pSymbol->st_value + *((uint32_t*)(&sectionsBuffer[pReloc->offset]));
            } else {
               for(int i = 0; i < sizeof(NameAddressMap) / sizeof(NameAddressMapEntry); i++) {
                  if(strCmp(name, NameAddressMap[i].name)) {
                     val = NameAddressMap[i].address;
                     break;
                  }
               }
            }

            if(val == 0) {
               return 0;
            }

            *((uint32_t*)(&sectionsBuffer[pReloc->offset])) = val;
            break;
         }

         case R_ARM_THM_CALL:
            op2 |= 0x40;

         case R_ARM_THM_JUMP24: {
            uint32_t targetAddress = 0;

            if(pSymbol->st_shndx != 0) {
               targetAddress = meta[pSymbol->st_shndx].loadAddress + pSymbol->st_value;
            } else {
               for(int i = 0; i < sizeof(NameAddressMap) / sizeof(NameAddressMapEntry); i++) {
                  if(strCmp(name, NameAddressMap[i].name)) {
                     targetAddress = NameAddressMap[i].address;
                     break;
                  }
               }
            }

            if(targetAddress != 0) {
               // Only support BL and B.W instructions

               if(((sectionsBuffer[pReloc->offset + 1] & 0xf8) != 0xf0) || ((sectionsBuffer[pReloc->offset + 3] & 0xd0) != op2)) {
                  return 0;
               }

               // Clear bit0 in target address, cause function pointer will typically have the bit set to indicate thumb instruction for BX and BLX calls
               uint32_t displacement = (targetAddress & 0xfffffffe) - (sectionMeta->loadAddress + pReloc->offset + 4);

               if((((displacement >> 24) != 0xff) && ((displacement >> 24) != 0)) || (displacement & 1)) {
                  return 0;
               }

               uint32_t imm11 = (displacement >> 1) & 0x7ff;
               uint32_t imm10 = (displacement >> 12) & 0x3ff;
               uint32_t t1 = (displacement >> 22) & 1;
               uint32_t t2 = (displacement >> 23) & 1;
               uint32_t s = (displacement >> 24) & 1;
               uint32_t j1 = s ^ !t1;
               uint32_t j2 = s ^ !t2;
               // --SE---S 12---imm10-----imm11---0
               sectionsBuffer[pReloc->offset] = (uint8_t)imm10;
               sectionsBuffer[pReloc->offset + 1] = (uint8_t)(0xf0 | (s << 2) | (imm10 >> 8));
               sectionsBuffer[pReloc->offset + 2] = (uint8_t)imm11;
               sectionsBuffer[pReloc->offset + 3] = (uint8_t)(op2 | (j1 << 5) | (j2 << 3) | (imm11 >> 8));
            } else {
               return 0;
            }

            break;
         }

         default:
            return 0;
      }
   }

   return 1;
}

static void runFuncs(uint32_t sh_type, uint32_t metaCount, SectionMetaEntry meta[]) {
   for(int i = 1; i < metaCount; i++) {
      if(meta[i].header->sh_type == sh_type) {
         uint32_t* funcs = (uint32_t*)meta[i].buffer;

         for(int j = 0; j < meta[i].header->sh_size >> 2; j++) {
            ((void (*)())funcs[j])();
         }
      }
   }
}

void runPreInitFuncs(uint32_t metaCount, SectionMetaEntry meta[]) {
   runFuncs(SHT_PREINIT_ARRAY, metaCount, meta);
}

void runInitFuncs(uint32_t metaCount, SectionMetaEntry meta[]) {
   runFuncs(SHT_INIT_ARRAY, metaCount, meta);
}
