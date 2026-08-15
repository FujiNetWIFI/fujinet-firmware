
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "memory.h"
#include "cartridge.h"
#include "fingerprints.h"
#include "intellicart.h"
#include "utils.h"
#include "vfs.h"
#include "audio.h"
#include "rommeta_parser.h"

#if CONFIG_FUJINET
#include "fuji_mailbox.h"
#endif

typedef enum {
   NONE,
   MAPPING,
   MEMATTR,
   VARS,
   MACRO,
   MAIN
} cfgSection;

Cartridge cart;     // main data structure for cart emulation

extern mm_map_t m;

extern uint8_t tv_mode;      // 0: PAL, 1: NTSC
extern uint8_t ecs_present;  // 0: ECS absent, 1: ECS present
extern uint8_t voice_present; // 0: iVoice absent, 1: iVoice Absent
extern uint8_t audio_volume;

void init_cart(void) {

   memset((uint16_t *) cart.ROM, 0, sizeof(cart.ROM));
   memset((uint16_t *) cart.RAM, 0, sizeof(cart.RAM));

   cart.pagingSupport = false;
#if CONFIG_JLP
   cart.JLPSupport = false;
   cart.JLPFlash = false;
   cart.JLPFlashSize = 0;
   cart.JLPAccel = 0;
   cart.flashfile[0] = '\0';
   // cart.FujiSupport is NOT reset here -- it's a board capability
   // (RunFujiConfig() sets it once, at boot, before any game loads) rather
   // than per-game state.
   update_ram_window();
#endif
#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
   cart.ECSSupport = false;
   cart.IntellivoiceSupport = false;
#endif
}

#if CONFIG_JLP
// Recomputes the RAM window the bus loop claims ahead of mm_lookup() (see
// cartridge.c). Call after changing JLPAccel, JLPFlash, or FujiSupport.
void update_ram_window(void) {
#if CONFIG_FUJINET
   if (cart.JLPAccel || cart.JLPFlash) {
      cart.ramLo = 0x8000;
      cart.ramHi = 0x9FFF;
      cart.ramWindow = true;
   } else if (cart.FujiSupport && cart.MailboxActive) {
      cart.ramLo = FUJI_MB_ADDR_LO;
      cart.ramHi = FUJI_MB_ADDR_HI;
      cart.ramWindow = true;
   } else {
      cart.ramWindow = false;
   }
#else
   cart.ramLo = 0x8000;
   cart.ramHi = 0x9FFF;
   cart.ramWindow = cart.JLPAccel || cart.JLPFlash;
#endif
}
#endif

#if CONFIG_JLP
inline void config_jlp(int jlp_value, int jlpflash_value, char *filename) {

   if (jlp_value != 0) {
      // JLP required
      cart.JLPSupport = true;
      printf("JLP support ON\n");

      if (JLP_FEATURE_ACCEL(jlp_value)) {
         cart.JLPAccel = true;
         printf("JLP accelerators ON\n");
      }
   }

   cart.JLPFlashSize = jlpflash_value;
   if (cart.JLPFlashSize > 0) {
      printf("JLP flash ON\n");
      printf("JLP flash size: %d\n", cart.JLPFlashSize);
      cart.JLPFlash = true;
   } else {
      if (JLP_FEATURE_FLASH(jlp_value)) {
         printf("JLP flash ON\n");
         printf("JLP flash requested but no flash size specified, use default size (16 sectors)\n");
         cart.JLPFlash = true;
         cart.JLPFlashSize = 16; // default size
      }
      else {
         printf("JLP flash OFF\n");
         cart.JLPFlash = false;
      }
   }

   if (cart.JLPFlash) {

      // check if JLP flash file exists
      vfs_file_t *f;
      vfs_stat_t st;
      uint32_t chunk = 0;
      uint32_t remaining = 0;
      uint32_t written = 0;

      char *dot = strrchr(filename, '.');
      strncpy(cart.flashfile, filename, (dot - filename));
      strcat(cart.flashfile, ".save");

      if(vfs_stat(cart.flashfile, &st) == -1) {

         uint8_t buffer[JLP_FLASH_ROW_BYTES];
         uint32_t size = cart.JLPFlashSize * JLP_FLASH_SECTOR_BYTES;
         
         // create JLP flash file
         f = vfs_open(cart.flashfile, "w");
         if (f == NULL) {
            printf("E: file create error\n");
            return;
         }

         printf("creating JLP flash file: %s, size: %ld\n", cart.flashfile, size);

         memset(buffer, 0xFF, sizeof(buffer));

         remaining = size;

         while (remaining) {

            chunk = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;

            written = vfs_write(f, buffer, chunk);

            if (written != chunk) {
               printf("E: write error\n");
               vfs_close(f);
               break;
            }

            remaining -= written;
         }

         vfs_close(f);
      }

      printf("I: use available JLP flash file: %s\n", cart.flashfile);

   }  // end if(JLPFlash)

   update_ram_window();
}
#endif

int load_cfg(char *filename) {
   char tmp_buffer[512] = {0};
   cfgSection cfgsec; 
   vfs_file_t *f;
   int ret;
   int num_pokes = 0;

#if CONFIG_JLP
   int jlp_value = 0;
   int jlpflash_value = 0;
#endif

   char *dot = strrchr(filename, '.');
   strncpy(tmp_buffer, filename, (dot - filename));
   strcat(tmp_buffer, ".cfg");

   // config file not available, try to config memory using fingerprint
   if ((f = vfs_open(tmp_buffer, "r")) == NULL) {
      int fp = 0;

      for (int i = 0; i < 128; i++)
         fp += ((cart.ROM[i] & 0xFF00) >> 8) + (cart.ROM[i] & 0x00FF);

      printf("filename: %s, fp: %d\n", filename, fp);

      for (int i=0; i<sizeof(romcoll)/sizeof(struct romConfig); i++) {
         if (fp == romcoll[i].fingerprint) {
            if (fp == 11349) {
               // Baseball or MTE Test Cart?
               if (cart.len > 8192)
                  config_memory(8);
               else
                  config_memory(0);

               return num_pokes;
            }

            config_memory(romcoll[i].mem_id);

#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
            if (romcoll[i].flags & VAR_VOICE) {
               cart.IntellivoiceSupport = true;
               printf("Intellivoice emulation enabled\n");
            }

            if (romcoll[i].flags & VAR_ECS) {
               cart.ECSSupport = true;
               printf("ECS support found\n");
            }
#endif

            return num_pokes;
         }
      }

      // if this line is reached no config file and no fingerprint so use default config
      config_memory(0);
      return num_pokes;
   }

   printf("load_cfg: use %s config file\n", tmp_buffer);
  
   mm_init(&m);

   cfgsec = NONE;

   while (!(vfs_eof(f))) {

      if (vfs_gets(f, tmp_buffer, sizeof(tmp_buffer)) == NULL)
         continue;

      strcpy(tmp_buffer, trim(tmp_buffer));
      to_lower(tmp_buffer);

      //printf("line: %s, len: %d\n", tmp_buffer, strlen(tmp_buffer));

      // skip comments
      if ( (tmp_buffer[0] == ';') || !(stralpha(tmp_buffer)) )
         continue;

      if (strstr(tmp_buffer, "[mapping]") != NULL) {
         cfgsec = MAPPING;
         printf("[mapping] section\n");
         continue;
      } else if (strstr(tmp_buffer, "[memattr]") != NULL) {
         cfgsec = MEMATTR;
         printf("[memattr] section\n");
         continue;
      } else if (strstr(tmp_buffer, "[vars]") != NULL) {
         cfgsec = VARS;
         printf("[vars] section\n");
         continue;
      } else if (strstr(tmp_buffer, "[macro]") != NULL) {
         cfgsec = MACRO;
         printf("[macro] section\n");
         continue;
      }

      if (cfgsec == MAPPING) {
         // example:
         // $2200 - $30FF = $7100

         uint32_t a, b, c, p;

         if(strstr(tmp_buffer, "page") != NULL) {

            ret = sscanf(tmp_buffer, "$%lx - $%lx = $%lx%*[^p]page%lx", &a, &b, &c, &p);
            if (ret != 4) {
               printf("E: parsing error in line: \n\t %s\n", tmp_buffer);
               return num_pokes;
            }
            cart.pagingSupport = true;
            mm_add(&m, a, b, c, p);

         } else {

            ret = sscanf(tmp_buffer, "$%lx - $%lx = $%lx", &a, &b, &c); 
            if (ret != 3) {
               printf("E: parsing error in line: \n\t %s\n", tmp_buffer);
               return num_pokes;
            }
            mm_add(&m, a, b, c, MM_NO_PAGE);
         }

      } else if (cfgsec == MEMATTR) {
         // example:
         // $8800 - $8FFF = RAM 8
         
         uint32_t a, b;
         int w;
         char type[4];

         ret = sscanf(tmp_buffer, "$%lx - $%lx = %3s %d", &a, &b, type, &w);
         if (ret != 4) {
            printf("E: parsing error in line: \n\t %s\n", tmp_buffer);
            return num_pokes;
         }
         if ( (strcmp(type, "rom") != 0) && (strcmp(type, "ram") != 0) ) {
            printf("E: parsing error in line: \n\t %s\n", tmp_buffer);
            return num_pokes;
         }
         if (w == 8) {
            mm_add_ram(&m, a, b, 8);
         } else {
            mm_add_ram(&m, a, b, 16);
         }

      } else if (cfgsec == VARS) {

#if CONFIG_JLP
         if ( sscanf(tmp_buffer, "jlp = %d", &jlp_value) == 1  ||
               sscanf(tmp_buffer, "jlp_accel = %d", &jlp_value) == 1  || 
               sscanf(tmp_buffer, "jlpaccel = %d", &jlp_value) == 1 ) {
            printf("JLP config found\n");
         }

         if ( sscanf(tmp_buffer, "jlp_flash = %d", &jlpflash_value) == 1  ||
               sscanf(tmp_buffer, "jlpflash = %d", &jlpflash_value) == 1 ) {

            printf("JLP flash config found\n");
         }
#endif

#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
         if (ecs_present == 0) {
            int ecs_value = 0;
            if ( sscanf(tmp_buffer, "ecs = %d", &ecs_value) == 1 ) {
               if (ecs_value == 1) {
                  cart.ECSSupport = true;
                  printf("ECS support found\n");
               }
            }
         }
         if (voice_present == 0) { 
            int intellivoice_value = 0;
            if ( sscanf(tmp_buffer, "voice = %d", &intellivoice_value) == 1 ) {
               cart.IntellivoiceSupport = true;
               printf("Intellivoice emulation enabled\n");
            }
         }
#endif


      } else if (cfgsec == MACRO) {
         // example: 
         // p 66fe 34 or poke 66fe 34
         uint32_t poke_address, poke_value;
         char cmd[16];
            
         ret = sscanf(tmp_buffer, "%15s %lx %lx", cmd, &poke_address, &poke_value);

         if (ret == 3 && (strcasecmp(cmd, "p") == 0 || strcasecmp(cmd, "poke") == 0)) {
            num_pokes++;
            printf("Poke %d detected : %lx @ %lx\n",num_pokes,poke_value,poke_address);
         }
         else {
            printf("E: not a MACRO/poke cmd in line: \n\t %s\n", tmp_buffer);
         }
      }
   }

   vfs_close(f);

#if CONFIG_JLP
   config_jlp(jlp_value, jlpflash_value, filename);
#endif

   // debug
   //mm_print_internals(&m);

   printf("load_cfg done\n");

   return num_pokes;
}

void apply_pokes(char *filename) {

   char tmp_buffer[512] = {0};
   cfgSection cfgsec;
   vfs_file_t *f;
   int ret;

   char *dot = strrchr(filename, '.');
   strncpy(tmp_buffer, filename, (dot - filename));
   strcat(tmp_buffer, ".cfg");

   printf("applying pokes\n");

   f = vfs_open(tmp_buffer, "r");

   cfgsec = NONE;
   while (!(vfs_eof(f))) {
      vfs_gets(f, tmp_buffer, sizeof(tmp_buffer));
      strcpy(tmp_buffer, trim(tmp_buffer));

      // skip comments
      if ( (tmp_buffer[0] == ';') || !(stralpha(tmp_buffer)) ) {
         continue;
      }

      // detect start of MACRO section
      if (strstr(tmp_buffer, "[macro]") != NULL) {
         cfgsec = MACRO;
         printf("[macro] section\n");
         continue;
      }

      if (cfgsec == MACRO) {
         // detect end of MACRO section
         if (strstr(tmp_buffer, "[") != NULL) {
            cfgsec = NONE;
            printf("End of MACRO section\n");
         }
         else {
            // example: 
            // p 66fe 34 or poke 66fe 34
            uint32_t poke_address, poke_value;
            char cmd[16];
            
            ret = sscanf(tmp_buffer, "%15s %lx %lx", cmd, &poke_address, &poke_value);

            if (ret == 3 && (strcasecmp(cmd, "p") == 0 || strcasecmp(cmd, "poke") == 0)) {
               // Modify actual value @corresponding address in binary
               uint32_t romaddr;
               
               if(mm_lookup(&m, poke_address, 0, (uint32_t *) &romaddr)) {
                  cart.ROM[romaddr] = poke_value;
                  printf("Apply poke : value %lx @ address %lx (%lx)\n",poke_value, poke_address, romaddr);
               }
               else {
                  printf("E: failure to map address %lx\n", poke_address);
               }
            }
            else {
               printf("E: not a MACRO/poke cmd in line: \n\t %s\n", tmp_buffer);
            }
         }
      }
   }
}

int add_info_page(int cur_page, INFO_ENTRY **info_entries, cfgSection section) {
   // if no more pages overwrite last page
   if (cur_page < MAX_INFO_PAGES) {
      cur_page++;
      (*info_entries)++;
   }
   (*info_entries)->section = section;
   // clear new page
   for (int i=0; i<10 ; i++)
      for (int j=0; j<20; j++)
         (*info_entries)->line[i][j] = 0;
         
   return cur_page;
}

int collect_info(char *filename, INFO_ENTRY *info_entries) {

   char tmp_buffer[512] = {0};
   vfs_file_t *f;
   cfgSection cfgsec = NONE;
   cfgSection new_cfgsec = NONE;
   int cur_line = 0;
   int cur_page = 1;
   vfs_stat_t st;

   // Get file name and size for MAIN section
   info_entries->section = MAIN;
   // clear new page
   for (int i=0; i<10 ; i++)
      for (int j=0; j<20; j++)
         info_entries->line[i][j] = 0;

   char *basename = strrchr(filename, '/') + 1;
   snprintf(info_entries->line[cur_line++], 20, "%-19s", "Filename");
   snprintf(info_entries->line[cur_line++], 20, "%19s", basename);

   if (vfs_stat(filename, &st) == 0) {
      printf("file size: %d bytes\n", st.size);
      snprintf(info_entries->line[cur_line++], 20, "%-19s", "File size");
      snprintf(info_entries->line[cur_line++], 20, "%7dKB (Max %3d)", st.size/1024, MAX_ROM_SIZE*2/1024);
   }

   if (is_rom_file(filename)) {
      // ROM file, info collection parsing complete file to get mapping and memattr info as well as JLP attributes if available
      snprintf(info_entries->line[cur_line++], 20, "%-19s", "File Type");
      snprintf(info_entries->line[cur_line++], 20, "%19s", "ROM");
      
      f = vfs_open(filename, "r");
      if (f != NULL) {
         uint32_t from, prev_from;
         uint16_t prev_size, target;
         char inputBuffer[3];

         vfs_read(f, inputBuffer, sizeof(inputBuffer));
         
         // read number of segments
         int slots = inputBuffer[1];

         // fill current page so that new page is started for MAPPING section
         cur_line = 10;

         for(int i=0; i<slots; i++) {
            vfs_read(f, inputBuffer, 2);

            int lo = inputBuffer[0] << 8;
            int hi = (inputBuffer[1] << 8) + 0x100;

            target = lo;
            if (i == 0)
               from = 0x0000;
            else
               from = prev_from + prev_size + 1;

            prev_size = (hi - lo) - 1;
            prev_from = from;

            // store mapping info in info_entries
            // if page is full start a new one
            if (cur_line == 10) {
               cur_line = 0;
               cur_page = add_info_page(cur_page, &info_entries, MAPPING);
            }
            sprintf(tmp_buffer, "$%04X - $%04X", lo, hi-1);
            snprintf(info_entries->line[cur_line++], 20, "%-19s", tmp_buffer);
            sprintf(tmp_buffer, "$%04X", target);
            snprintf(info_entries->line[cur_line++], 20, "%19s", tmp_buffer);

            // skip actual ROM data
            for (int j = lo; j < hi; j++) {
               vfs_read(f, inputBuffer, 2);
            }
            // skip CRC (2 bytes)
            vfs_read(f, inputBuffer, 2);
         }

         // fill current page so that new page is started for MEMATTR section
         cur_line = 10;
         
         // read memory block (2Kb) attributes
         char memattr[50];
         vfs_read(f, memattr, sizeof(memattr));
         for (int i = 0; i < 32; i++) {
            int attr = 0xF & (memattr[(i >> 1)] >> ((i & 1) * 4));
            int lohi = memattr[16 + ((i >> 1) | ((i & 1) << 4))];
            int lo   = (lohi >> 4) & 0x7;
            int hi   = (lohi & 0x7) + 1;

            // check if memory block has write attribute
            if(attr & 0x02) {              
               // store memattr info in info_entries, if page is full start a new one
               if (cur_line == 10) {
                  cur_line = 0;
                  cur_page = add_info_page(cur_page, &info_entries, MEMATTR);
               }
               sprintf(tmp_buffer, "$%04X - $%04X", i * 0x800, (i * 0x800) + ((hi - lo) * 0x100) - 1);
               snprintf(info_entries->line[cur_line++], 20, "%-19s", tmp_buffer);
               if(attr & 0x04)
                  snprintf(info_entries->line[cur_line++], 20, "%19s", "RAM 8");
               else
                  snprintf(info_entries->line[cur_line++], 20, "%19s", "RAM 16");
            }
         }
         // fill current page so that new page is started for VARS section
         cur_line = 10;

         // check for metadata section 
         if (!vfs_eof(f)) {
            rommeta_status_t st;
            rommeta_parser_t parser;
            char key[ROMMETA_STR_SIZE];
            char value[ROMMETA_STR_SIZE];

            rommeta_parser_init(&parser);

            for (;;)
            {
               st = rommeta_read_next(&parser, f, key, value);

               if (st == ROMMETA_END)
                  break;

               if (st == ROMMETA_ERR)
               {
                  printf("Error parsing ROM metadata\n");
                  break;
               }

               // need to store VARS attributes in info_entries for display in UI
               // store memattr info in info_entries, if page is full start a new one
               if (cur_line == 10) {
                  cur_line = 0;
                  cur_page = add_info_page(cur_page, &info_entries, VARS);
               }
               snprintf(info_entries->line[cur_line++], 20, "%-19s", key);
               snprintf(info_entries->line[cur_line++], 20, "%19s", value);      
            }
         }
         vfs_close(f);
      }
   }
   else {
      // not a ROM file, try to collect info from corresponding config file
      char *dot = strrchr(filename, '.');
      strncpy(tmp_buffer, filename, (dot - filename));
      strcat(tmp_buffer, ".cfg");  
     
      f = vfs_open(tmp_buffer, "r");
      if (f == NULL) {
         printf("collect_info: could not open file %s\n", tmp_buffer);
         snprintf(info_entries->line[cur_line++], 20, "%-19s", "file Type");
         snprintf(info_entries->line[cur_line++], 20, "%19s", "BIN (no cfg)");
      }
      else {
         snprintf(info_entries->line[cur_line++], 20, "%-19s", "file Type");
         snprintf(info_entries->line[cur_line++], 20, "%19s", "BIN+CFG");
         // now parse the file to collect info entries
         while (!(vfs_eof(f))) {
            vfs_gets(f, tmp_buffer, sizeof(tmp_buffer));
            strcpy(tmp_buffer, trim(tmp_buffer));

            // skip comments
            if ( (tmp_buffer[0] == ';') || !(stralpha(tmp_buffer)) ) {
               continue;
            }

            if (strstr(tmp_buffer, "[mapping]") != NULL) {
               new_cfgsec = MAPPING;
               printf("[mapping] section\n");
            } else if (strstr(tmp_buffer, "[memattr]") != NULL) {
               new_cfgsec = MEMATTR;
               printf("[memattr] section\n");
            } else if (strstr(tmp_buffer, "[vars]") != NULL) {
               new_cfgsec = VARS;
               printf("[vars] section\n");
            } else if (strstr(tmp_buffer, "[macro]") != NULL) {
               new_cfgsec = MACRO;
               printf("[macro] section\n");
            }

            if (new_cfgsec != cfgsec) {
               // new config section detected => fill current page so that new page gets started for new section
               cfgsec = new_cfgsec;
               cur_line = 10;
               continue;
            }

            if (cfgsec != NONE) {
               printf("collecting info from line: %s\n", tmp_buffer);
               char *equal_sign = strchr(tmp_buffer, '=');
               if (equal_sign) {
                  // split the line into key and value
                  *equal_sign = 0;
                  char *key = trim(tmp_buffer);
                  char *value = trim(equal_sign + 1);
                  // remove trailing comment
                  char *comment_sign = strchr(value, ';');
                  if (comment_sign) *comment_sign = 0;
                  value = trim(value);
                  // if page is full start a new one
                  if (cur_line == 10) {
                     cur_line = 0;
                     cur_page = add_info_page(cur_page, &info_entries, cfgsec);
                  }
                  printf("key: %s, value: %s\n", key, value);
                  snprintf(info_entries->line[cur_line++], 20, "%-19s", key);
                  snprintf(info_entries->line[cur_line++], 20, "%19s", value);
               } else {
                  printf("E: invalid line:\t %s\n", tmp_buffer);
               }
            }
         }
         vfs_close(f);
      }
   }
   return cur_page;
}
