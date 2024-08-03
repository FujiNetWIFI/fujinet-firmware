#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "flash.h"
#include "cartridge_emulation_ACE.h"
#include "cartridge_firmware.h"
#if USE_SD_CARD
#include "sd.h"
#endif
#include "flash.h"

#define ACE_MAJOR_REV 1
#define ACE_MINOR_REV 0
#define ACE_BUGFIX_REV 0

#define BUFFER_SIZE_BYTES (BUFFER_SIZE_KB * 1024)

// Adapted from the Unocart ACE scheme by ZackAttack.
// ACE Implementation - For Pluscart. Adapted by Marco Johannes.
// Uses the same header format as the Unocart scheme, except with a uinque magic number for Pluscart.
// This header must exist at the beginning of every valid ACE file
// The bootloader automatically offsets the entry point if the user ROM is not at 0x0802000. For dynamic handling of user's offline rom storage.

typedef struct __attribute__((packed)) {
   uint8_t magic_number[8]; // Always ascii "ACE-PC00" for Pluscart ACE files
   uint8_t driver_name[16]; // emulators care about this
   uint32_t driver_version; // emulators care about this
   uint32_t rom_size;		 // size of ROM to be copied to flash, 996KB max
   uint32_t rom_checksum;	 // used to verify if flash already contains valid image
   uint32_t entry_point;	 // Absolute address of execution. This is 0x0802000 + the rom's ARM code segment offset.
}
ACEFileHeader;

enum ace_type {
   None,
   Unified,
   PlusOnly,
   UnoOnly,
};

static const char* UnifiedMagicNumber[3] = {
   "ACE-UF00", // works with unified firmware on any supported cart
   "ACE-PC00", // works with only the plus cart
   "ACE-UC00", // works with only unocart, primarily for downgrading to legacy firmware
};

int get_ace_type(unsigned int image_size, uint8_t *buffer) {
   if(image_size < sizeof(ACEFileHeader))
      return 0;

   ACEFileHeader * header = (ACEFileHeader *)buffer;

   if(header->rom_size > (896*1024)) return 0;

   // check unified magic number
   for(int i = 0; i < 3; i++) {
      int j = 0;

      for(; j < 8; j++) {
         if(UnifiedMagicNumber[i][j] != header->magic_number[j])
            break;
      }

      if(j == 8) {
         return i + 1;
      }
   }

   return None;
}

int is_ace_cartridge(unsigned int image_size, uint8_t *buffer) {
   return get_ace_type(image_size, buffer) != 0;
}

int launch_ace_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d, bool withPlusFunctions, uint8_t *eram) {

   int ace_type = get_ace_type(image_size, buffer);

   if(ace_type == None)
      return 0;

   ACEFileHeader header = *((ACEFileHeader *)buffer);


   uint8_t* cart_rom = 0; //Set NULL pointer for now
   void *EntryVector = 0; //Set NULL pointer for now
   //FIXME
   // - STM32 -
   // DOWNLOAD_AREA_START_ADDRESS = 0x08020000
   // 128 * 1024 = 0x20000
   // cart_rom = DOWNLOAD_AREA_START_ADDRESS + (128 * 1024) = 0x08040000
   // entry_offset = 0x08020200
   // header.entry_point = 0x0802D47D     (FadeToGrey rom)
   // EntryVector = cart_rom - entry_offset + header.entry_point = 0x08040000 - 0x08020200 + 0x0802D47D = 0x0804D27D
   uint32_t entry_offset = (ace_type == PlusOnly) ? 0x08020200 : 0;
   uint32_t buffered_size = image_size > BUFFER_SIZE_BYTES ? BUFFER_SIZE_BYTES : image_size;
   uint32_t unbuffered_size = image_size > BUFFER_SIZE_BYTES ? image_size - BUFFER_SIZE_BYTES : 0;

   if(d->type == Cart_File) {
      if(ace_type == UnoOnly)
         return 0;

#if USE_WIFI
      // Copy what was already downloaded (in buffer) from network to flash
      //FIXME
      //flash_erase_storage(user_settings.first_free_flash_sector);
      //cart_rom = (uint8_t*)(DOWNLOAD_AREA_START_ADDRESS + 128U * 1024U * (uint8_t)(user_settings.first_free_flash_sector - 5));
      cart_rom = (uint8_t *) (XIP_BASE + FLASH_AREA_OFFSET);
      flash_buffer_at(buffer, buffered_size, FLASH_AREA_OFFSET);

      // Then download the rest if any
      if(unbuffered_size > 0)
         flash_download_at((char*)filename, unbuffered_size, buffered_size, FLASH_AREA_OFFSET + buffered_size);

      //FIXME
      //EntryVector = cart_rom - entry_offset + header.entry_point; // Adjust vector dependent on position in ROM
      EntryVector = cart_rom + (header.entry_point - 0x08020000); // Adjust vector dependent on position in ROM
#else
      return 0;
#endif
   } else if(d->type == Offline_Cart_File) {
      if(ace_type == UnoOnly)
         return 0;

      cart_rom = (uint8_t *) (XIP_BASE + FLASH_AREA_OFFSET);
      //cart_rom = (uint8_t*)(d->flash_base_address+512); //Rom from flash only (ignore RAM and CCM download), 512 = header for tar used in offline flash
      //EntryVector = (void*)(d->flash_base_address+512 - entry_offset  + header.entry_point);//Adjust vector dependent on position in ROM
      //FIXME
      EntryVector = cart_rom + (header.entry_point - 0x08020000);
   } 
#if USE_SD_CARD
   else if(d->type == SD_Cart_File) {

      //dbg("ACE entry_point: 0x%X\r\n", header.entry_point);
      //dbg("ACE entry_offset: 0x%X\r\n", entry_offset);
      //dbg("buffered_size: %ld\r\n", buffered_size);
      //dbg("unbuffered_size: %ld\r\n", unbuffered_size);
      /*
      if(ace_type == PlusOnly) {
         return 0;
      } else */ if(ace_type == UnoOnly) {
         // Uno based ACE is hardcoded to run from buffer
         cart_rom = buffer;
         EntryVector = (void*)header.entry_point;
      } else {
         // Copy what was already in buffer to flash
         //FIXME
         //flash_erase_storage(user_settings.first_free_flash_sector);
         //cart_rom = (uint8_t*)(DOWNLOAD_AREA_START_ADDRESS + 128U * 1024U * (uint8_t)(user_settings.first_free_flash_sector - 5));
         cart_rom = (uint8_t *) (XIP_BASE + FLASH_AREA_OFFSET);
         flash_buffer_at(buffer, buffered_size, FLASH_AREA_OFFSET);
         // Then flash the rest of the file if any
         uint32_t start_pos = buffered_size;
         uint32_t bytes_remaining = unbuffered_size;

         while(bytes_remaining > 0) {
            uint32_t length = bytes_remaining > BUFFER_SIZE_BYTES ? BUFFER_SIZE_BYTES : bytes_remaining;
            uint32_t bytes_read = sd_card_file_request(buffer, (char *) filename, start_pos, length);

            if(bytes_read == length) {
               flash_buffer_at(buffer, bytes_read, FLASH_AREA_OFFSET+start_pos);
            } else {
               return 0;
            }

            start_pos += bytes_read;
            bytes_remaining -= bytes_read;
         }

         //FIXME
         //EntryVector = cart_rom - entry_offset  + header.entry_point; //Adjust vector dependent on position in ROM
         EntryVector = cart_rom + (header.entry_point - 0x08020000);

         //dbg("cart_rom: 0x%X\r\n", cart_rom);
         //dbg("EntryVector: 0x%X\r\n", EntryVector);

         //dbg("buffer[0]: 0x%X\r\n", buffer[0]);
         //dbg("buffer[1]: 0x%X\r\n", buffer[1]);
         //dbg("buffer[2]: 0x%X\r\n", buffer[2]);
         //dbg("buffer[3]: 0x%X\r\n", buffer[3]);
         //dbg("buffer[4]: 0x%X\r\n", buffer[4]);
         //dbg("buffer[5]: 0x%X\r\n", buffer[5]);

         //dbg("cart_rom[0]: 0x%X\r\n", cart_rom[0]);
         //dbg("cart_rom[1]: 0x%X\r\n", cart_rom[1]);
         //dbg("cart_rom[2]: 0x%X\r\n", cart_rom[2]);
         //dbg("cart_rom[3]: 0x%X\r\n", cart_rom[3]);
         //dbg("cart_rom[4]: 0x%X\r\n", cart_rom[4]);
         //dbg("cart_rom[5]: 0x%X\r\n", cart_rom[5]);
         //dbg("cart_rom[0xC014]: 0x%X\r\n", cart_rom[0xC014]);
         //dbg("cart_rom[0xC015]: 0x%X\r\n", cart_rom[0xC015]);
      }
   } 
#endif
   else {
      return 0;
   }

   unsigned major = 0, minor = 0, bugfix = 0;
   sscanf(VERSION, "%u.%u.%u", &major, &minor, &bugfix); //Split up the Pluscart version number string into numeric form

   //Setup virtual arguments to be passed to ACE code in the beginning of buffer RAM. It is optional for the ACE application to use all of them though.
   uint32_t* buffer32 = (uint32_t*)(0x20000000); //Set up temporary buffer pointer for writing arguments
   *buffer32 = (uint32_t)cart_rom; //0. Declare the start position of the cart in flash as a 32 bit pointer. This changes depending on the state of the user's offline roms.
   buffer32++;
   *buffer32 = (uint32_t)(eram);//1. Declare the usage of CCM Memory so ACE application can use remainder, and not disturb the Pluscart variables.
   buffer32++;
   *buffer32 = (uint32_t)(&reboot_into_cartridge); //2. Pass Pluscart library function pointer for reboot_into_cartridge. Used for bootstrapping the 2600 system before running the ACE application.
   buffer32++;
   *buffer32 = (uint32_t)(&emulate_firmware_cartridge); //3. Pass Pluscart library function pointer for emulate_firmware_cartridge. Used to exit out of a 2600 rom and back into the Pluscart menu.
   buffer32++;
   *buffer32 = (uint32_t)(clock_get_hz(clk_sys));//4. Pass the system clock frequency for time dependent functions.
   buffer32++;
   *buffer32 = (uint32_t)((ACE_MAJOR_REV<<16)+(ACE_MINOR_REV<<8)+ACE_BUGFIX_REV);//5. Pass the ACE Interface version number to the ACE application,encoded as  "00",major,minior,bugfix as a UINT32. The ACE code can then accept or reject compatibility before execution.
   buffer32++;
   *buffer32 = (uint32_t)((major<<16)+(minor<<8)+bugfix);//6. Pass the Pluscart Firmware version number to the ACE application,encoded as  "00",major,minior,bugfix as a UINT32. The ACE code can then accept or reject compatibility before execution.
   buffer32++;

   // Pointers to the GPIO registers necessary for communicating with the 6502
   // This will vary depending on cart hardware
   *buffer32 = (uint32_t)(ADDR_IDR);//7.
   buffer32++;
   *buffer32 = (uint32_t)(DATA_IDR);//8.
   buffer32++;
   *buffer32 = (uint32_t)(DATA_ODR);//9.
   buffer32++;
   *buffer32 = (uint32_t)(DATA_MODER);//a.
   buffer32++;

   *buffer32 = (uint32_t)(0xACE42600);//b. Argument list termination magic number

   //Stock code uses "EntryVector" below, this jumps to the function pointer set in the ACE header(and adjusted by the bootloader dependent on the state of offline roms).
   //The ROM based ACE code has no arguments. However, "virtual" arguments are passed in the form of a uint32_t lookup table in buffer memory (0x20000000). It is optional for the ROM ace code to use all of the arguments.
   //This arrangement exists so that the ARM stack is unchanged for future versions of ACE which might pass more virtual arguments. In c, functions need to have the exact same number and type of arguments in their library code as from the function that called them.
   return ((int (*)())EntryVector)(); /*Uncomment this line to run in ACE mode (code in ROM)*/
   //	emulate_ACEROM_cartridge();  /*Uncomment this line to run in firmware mode(code in Pluscart fimrware) */

}
