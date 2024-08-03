#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "global.h"
#include "menu.h"
#include "cartridge_firmware.h"
#include "user_settings.h"

#if USE_WIFI
#include "esp8266.h"
#endif

#if USE_SD_CARD

#include "sd.h"

size_t sd_get_num() {
   return 1;
}
sd_card_t *sd_get_by_num(size_t num) {
   if(0 == num) {
      return &sd_card;
   } else {
      return NULL;
   }
}

bool is_text_file(char * filename) {
   char *dot = strrchr(filename, '.');

   if(!dot || dot == filename) return false;

   dot++;
   return (strcasecmp(dot, "txt") == 0);
}

int find_last_path_seperator(const char * path) {
   int i = (int)strlen(path) - 1;

   for(; i >= 0 ; i--) {
      if(path[i] == PATH_SEPARATOR) break;
   }

   return i;
}

void basename(const char * path, char * filename) {
   int pos = find_last_path_seperator(path) + 1;
   memset(filename, 0, 33);
   strncpy(filename, &path[pos], 32);
}

char * strcasestr(const char *s, const char *find) {
   char c, sc;
   size_t len;

   if((c = *find++) != 0) {
      c = (char)tolower((unsigned char)c);
      len = strlen(find);

      do {
         do {
            if((sc = *s++) == 0)
               return (NULL);
         } while((char)tolower((unsigned char)sc) != c);
      } while(strncasecmp(s, find, len) != 0);

      s--;
   }

   return ((char *)s);
}

FRESULT recursive_search(char *path, char *pattern, MENU_ENTRY **dst, int *num_menu_entries, FIL* search_results_file) {

   FRESULT res;
   FILINFO fno;
   DIR dir;
   UINT i;

   res = f_opendir(&dir, path);                       /* Open the directory */

   if(res == FR_OK) {
      for(;;) {
         res = f_readdir(&dir, &fno);                    /* Read a directory item */

         if(res != FR_OK || fno.fname[0] == 0) break;    /* Break on error or end of dir */

         if(fno.fattrib & (AM_HID | AM_SYS)) continue;   /* Skip hidden and system file/dir */

         if(fno.fattrib & AM_DIR) {                      /* It is a directory */
            i = strlen(path);
            strcat(path, "/");
            strcat(path, fno.fname);
            res = recursive_search(path, pattern, dst, num_menu_entries, search_results_file);  /* Enter the directory */

            if(res != FR_OK) break;

            path[i] = 0;
         } else {
            // check if filename contains search string (case insensitive)
            char * test = strcasestr(fno.fname, pattern);

            // basically we are looking for a word boundary before the match.
            if(test && (strlen(test) == strlen(fno.fname) ||
                        test[-1] < 48 || test[-1] > 122 ||
                        (test[-1] > 90 && test[-1] < 97))) {
               if(is_text_file(fno.fname)) {
                  continue;
               }

               f_puts(path, search_results_file);
               f_puts("/", search_results_file);
               f_puts(fno.fname, search_results_file);
               f_puts("\n", search_results_file);

               (*dst)->type = SD_Cart_File;
               (*dst)->filesize = (uint32_t) fno.fsize;
               memset((*dst)->entryname, 0, CHARS_PER_LINE+1);
               strncpy((*dst)->entryname, fno.fname, CHARS_PER_LINE);
               (*dst)++;
               (*num_menu_entries)++;
            }
         }
      }

      f_closedir(&dir);
   }

   return res;
}

FRESULT open_system_file(FIL* sys_file, const char *name, BYTE mode) {
   char path[40] = "/System";
   FILINFO fno;

   if(f_stat(path, &fno) != FR_OK) {
      f_mkdir(path);
      f_chmod(path, AM_HID, AM_HID);
   }

   strcat(path, "/");
   strncat(path, name, 32);
   return f_open(sys_file, path, mode);
}

bool sd_card_file_list(char *path, MENU_ENTRY **dst, int *num_menu_entries) {

   FATFS FatFs;
   FILINFO fno;
   bool list_needs_sorting = true;

   if(f_mount(&FatFs, "", 1) == FR_OK) {
      if(is_text_file(path)) {
         FIL fil;
         list_needs_sorting = false;

         if(f_open(&fil, path, FA_READ) == FR_OK) {
            while(f_gets((*dst)->entryname, CHARS_PER_LINE, &fil) != 0 && (*num_menu_entries) < NUM_MENU_ITEMS) {
               (*dst)->type = Leave_Menu;
               (*dst)->filesize = 0;
               (*dst)->font = user_settings.font_style;
               (*dst)++;
               (*num_menu_entries)++;
            }

            f_close(&fil);
         }

      } else {

         DIR dir;
         (*dst)->type = Leave_Menu;
         (*dst)->filesize = 0;
         strcpy((*dst)->entryname, "..");
         (*dst)->font = user_settings.font_style;
         (*dst)++;
         (*num_menu_entries)++;

         if(f_opendir(&dir, path) == FR_OK) {
            while((*num_menu_entries) < NUM_MENU_ITEMS) {
               if(f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
                  break;

               if(fno.fattrib & (AM_HID | AM_SYS))
                  continue;

               (*dst)->type = fno.fattrib & AM_DIR ? SD_Sub_Menu : SD_Cart_File;

               if((*dst)->type == SD_Cart_File) {
                  if(is_text_file(fno.fname))
                     (*dst)->type = SD_Sub_Menu; // text files are "fake" directories
               }

               // copy file record
               (*dst)->filesize = (uint32_t) fno.fsize;

               if(strlen(fno.fname) < CHARS_PER_LINE+1)
                  strncpy((*dst)->entryname, fno.fname, CHARS_PER_LINE);
               else
                  strncpy((*dst)->entryname, fno.altname, 14);

               (*dst)->font = user_settings.font_style;
               (*dst)++;
               (*num_menu_entries)++;
            } // end while

            f_closedir(&dir);
         }
      }

      f_mount(0, "", 1);
   }

   return list_needs_sorting;
}

uint32_t sd_card_file_request(uint8_t *ext_buffer, char *path, uint32_t start_pos, uint32_t length) {

   UINT bytes_read = 0;
   FATFS FatFs;
   FIL fil;
   FRESULT read_result;
   const char *sd_file = 0;

   if(f_mount(&FatFs, "", 1) == FR_OK) {
      if(strstr(path, MENU_TEXT_SD_CARD_CONTENT) == path) {
         sd_file = &path[sizeof(MENU_TEXT_SD_CARD_CONTENT)];
      } else if(strstr(path, MENU_TEXT_SEARCH_FOR_SD_ROM) == path) {
         if(open_system_file(&fil, "Search", (FA_OPEN_EXISTING | FA_READ)) == FR_OK) {
            char filename[33];
            basename(path, filename);
            char sd_path[256];
            strcpy(sd_path, path);

            while(f_gets(sd_path, 255, &fil)) {
               int pos = find_last_path_seperator(sd_path) + 1;

               if(strstr(&sd_path[pos], filename) == &sd_path[pos]) {
                  sd_file = sd_path;
                  break;
               }
            }

            f_close(&fil);
         }
      }

      if(sd_file && f_open(&fil, sd_file, FA_READ) == FR_OK) {
         if(start_pos == 0 || f_lseek(&fil, start_pos) == FR_OK) {
            read_result = f_read(&fil, ext_buffer, length, &bytes_read);

            if(read_result != FR_OK) {
               bytes_read = 0;
            }
         }

         f_close(&fil);
      }

      f_mount(0, "", 1);
   }

   return (uint32_t) bytes_read;
}

bool sd_card_find_file(char *path, char *pattern, MENU_ENTRY **dst, int *num_menu_entries) {
   FATFS FatFs;
   FRESULT res = f_mount(&FatFs, "", 1);
   FIL search_results_file;

   if(res == FR_OK) {
      res = open_system_file(&search_results_file, "Search", (FA_CREATE_ALWAYS | FA_WRITE | FA_READ));

      if(res == FR_OK) {
         res = recursive_search(path, pattern, dst, num_menu_entries, &search_results_file);
         f_close(&search_results_file);
      }

      f_mount(0, "", 1);
   }

   return (res == FR_OK);
}

int sd_card_file_size(char * path) {
   FATFS FatFs;
   FILINFO fno;
   int file_size = -1;

   if(f_mount(&FatFs, "", 1) == FR_OK) {
      if(f_stat(path, &fno) == FR_OK)
         file_size = (int)fno.fsize;

      f_mount(0, "", 1);
   }

   return file_size;
}

int *sd_card_statistics(void) {
   FATFS FatFs;
   static int response[2] = {0, 0};

   if(f_mount(&FatFs, "", 1) == FR_OK) {
      DWORD free_clusters, used_size, total_size;
      FATFS* getFreeFs;

      if(f_getfree("", &free_clusters, &getFreeFs) == FR_OK) {
         // Formula comes from ChaN's documentation
         total_size = (getFreeFs->n_fatent - 2) * getFreeFs->csize;
         used_size = total_size - (free_clusters * getFreeFs->csize);
         response[0] = (int)(total_size / 2048);
         response[1] = (int)(used_size / 2048);
      }

      f_mount(0, "", 1);
   }

   return response;
}

#endif
