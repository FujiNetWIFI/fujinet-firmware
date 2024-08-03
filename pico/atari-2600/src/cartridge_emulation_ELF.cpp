#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "cartridge_emulation.h"
#include "cartridge_emulation_ELF.h"
#include "cartridge_firmware.h"
#include "user_settings.h"
#include "vcsLib.h"
#include "global.h"

#define NTSC_CLOCK 1193182UL
#define PAL_CLOCK 1182298UL

#define ELF_RAM_KB      16

NameAddressMapEntry NameAddressMap[] = {
   // Color lookup table is updated based on detected system. Keep at index 0
   {(uint32_t)&Ntsc2600[0], (char *)"ColorLookup" },
   // Low level bus access - handy for distributing prototype banking schemes without updating firmware
   {(uint32_t)&ADDR_IDR, (char *)"ADDR_IDR" },
   {(uint32_t)&DATA_IDR, (char *)"DATA_IDR" },
   {(uint32_t)&DATA_ODR, (char *)"DATA_ODR" },
   {(uint32_t)&DATA_MODER, (char *)"DATA_MODER" },
   // Used by GCC/CRT
   {(uint32_t)memset, (char *)"memset" },
   {(uint32_t)memcpy, (char *)"memcpy" },
   // Strong-ARM framework
   {(uint32_t)&ReverseByte[0], (char *)"ReverseByte" },
   {(uint32_t)vcsLdaForBusStuff2, (char *)"vcsLdaForBusStuff2" },
   {(uint32_t)vcsLdxForBusStuff2, (char *)"vcsLdxForBusStuff2" },
   {(uint32_t)vcsLdyForBusStuff2, (char *)"vcsLdyForBusStuff2" },
   {(uint32_t)vcsWrite3, (char *)"vcsWrite3" },
   {(uint32_t)vcsJmp3, (char *)"vcsJmp3" },
   {(uint32_t)vcsNop2, (char *)"vcsNop2" },
   {(uint32_t)vcsNop2n, (char *)"vcsNop2n" },
   {(uint32_t)vcsWrite5, (char *)"vcsWrite5" },
   {(uint32_t)vcsWrite6, (char *)"vcsWrite6" },
   {(uint32_t)vcsLda2, (char *)"vcsLda2" },
   {(uint32_t)vcsLdx2, (char *)"vcsLdx2" },
   {(uint32_t)vcsLdy2, (char *)"vcsLdy2" },
   {(uint32_t)vcsSax3, (char *)"vcsSax3" },
   {(uint32_t)vcsSta3, (char *)"vcsSta3" },
   {(uint32_t)vcsStx3, (char *)"vcsStx3" },
   {(uint32_t)vcsSty3, (char *)"vcsSty3" },
   {(uint32_t)vcsSta4, (char *)"vcsSta4" },
   {(uint32_t)vcsStx4, (char *)"vcsStx4" },
   {(uint32_t)vcsSty4, (char *)"vcsSty4" },
   {(uint32_t)vcsCopyOverblankToRiotRam, (char *)"vcsCopyOverblankToRiotRam" },
   {(uint32_t)vcsStartOverblank, (char *)"vcsStartOverblank" },
   {(uint32_t)vcsEndOverblank, (char *)"vcsEndOverblank" },
   {(uint32_t)vcsRead4, (char *)"vcsRead4" },
   {(uint32_t)randint, (char *)"randint" },
   {(uint32_t)vcsTxs2, (char *)"vcsTxs2" },
   {(uint32_t)vcsJsr6, (char *)"vcsJsr6" },
   {(uint32_t)vcsPha3, (char *)"vcsPha3" },
   {(uint32_t)vcsPhp3, (char *)"vcsPhp3" },
   {(uint32_t)vcsPla4, (char *)"vcsPla4" },
   {(uint32_t)vcsPlp4, (char *)"vcsPlp4" },
   {(uint32_t)vcsPla4Ex, (char *)"vcsPla4Ex" },
   {(uint32_t)vcsPlp4Ex, (char *)"vcsPlp4Ex" },
   {(uint32_t)vcsJmpToRam3, (char *)"vcsJmpToRam3" },
   {(uint32_t)vcsWaitForAddress, (char *)"vcsWaitForAddress" },
   {(uint32_t)injectDmaData, (char *)"injectDmaData" },
};

int __time_critical_func(launch_elf_file)(const char* filename, uint32_t buffer_size, uint8_t* buffer) {
   uint32_t mainArgs[MP_COUNT] = {
      0, // MP_SYSTEM_TYPE (TBD below)		0
      clock_get_hz(clk_sys), // MP_CLOCK_HZ			1
      FF_MULTI_CART, // MP_FEARTURE_FLAGS		2
   };

   // Transfer control back to ROM
   uint32_t irqstatus = save_and_disable_interrupts();
   vcsLibInit();

   switch(user_settings.tv_mode) {
      case TV_MODE_PAL:
         mainArgs[MP_SYSTEM_TYPE] = ST_PAL_2600;
         break;

      case TV_MODE_PAL60:
         mainArgs[MP_SYSTEM_TYPE] = ST_PAL60_2600;
         break;

      default:
      case TV_MODE_NTSC:
         mainArgs[MP_SYSTEM_TYPE] = ST_NTSC_2600;
         break;
   }

   if(mainArgs[MP_SYSTEM_TYPE] == ST_PAL_2600 || mainArgs[MP_SYSTEM_TYPE] == ST_PAL60_2600)
      NameAddressMap[0].address = (uint32_t)&Pal2600[0];

   int usesVcsWrite3;
   uint32_t pMainAddress;
   uint32_t metaCount = ((ElfHeader*)buffer)->e_shnum;
   SectionMetaEntry* meta = (SectionMetaEntry *) malloc(sizeof(SectionMetaEntry) * metaCount);

   // pointer to extra ram buffer
   uint8_t *eram;
   eram = (uint8_t *) malloc(ELF_RAM_KB * 1024); 

   if(eram == NULL)
      return 0;

   if(!initSectionsMeta(buffer, meta, (uint32_t)eram)) {
      return 0;
   }

   if(!loadElf(buffer, metaCount, meta, &pMainAddress, &usesVcsWrite3)) {
      return 0;
   }

   runPreInitFuncs(metaCount, meta);
   runInitFuncs(metaCount, meta);

   if(usesVcsWrite3)
      vcsInitBusStuffing();

   vcsEndOverblank();
   vcsNop2n(1024);

   // Run game
   ((void (*)(uint32_t *))pMainAddress)(mainArgs);

   if(eram)
      free(eram);

   // elf rom should have jumped to 0x1000 and put nop on bus
   restore_interrupts(irqstatus);
   exit_cartridge(0x1100, 0x1000);

   return 1;
}
