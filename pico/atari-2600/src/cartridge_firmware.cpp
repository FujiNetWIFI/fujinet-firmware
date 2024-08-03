#include <string.h>

#include "global.h"
#include "cartridge_firmware.h"
#include "hardware/sync.h"

#if MENU_TYPE == UNOCART
#include "firmware_uno_pal_rom.h"
#include "firmware_uno_pal60_rom.h"
#include "firmware_uno_ntsc_rom.h"
#else
#include "firmware_pal_rom.h"
#include "firmware_pal60_rom.h"
#include "firmware_ntsc_rom.h"
#endif

#include "font.h"
#include "user_settings.h"

#define lineCounter 0x82
#define lineBackColour 0x84

#define PATCH 0

// These are the colours between the top title bar and the rest of the text lines...

#define BACK_COL_NTSC     0x92
#define BACK_COL_PAL      0xD2

//#define HEADER_BACK_COL_NTSC     0x20
//#define HEADER_BACK_COL_PAL      0x40

#define t2c(fontType, l, r, s) \
	(uint8_t)(sharedFont[ convertAsciiToCharnum(fontType, l) * 12 + s ] << 4 | \
	sharedFont[ convertAsciiToCharnum(fontType, r) * 12 + s ])

const uint16_t DATAMODE_OUT = 0x5555;

static char menu_header[CHARS_PER_LINE];
static char pendingStatusMessage[STATUS_MESSAGE_LENGTH];
static unsigned char menu_status[STATUS_MAX];
static unsigned const char *firmware_rom = firmware_pal_rom;

inline void add_zeroSprites();
inline void add_prefix_a(bool both);
inline void add_prefix_b();
inline void add_start_bank(uint8_t bank_id);
inline void add_end_bank(uint8_t bank_id);
inline void add_kernel_a(uint8_t fontType, uint8_t scanline, uint8_t *text);
inline void add_kernel_b(uint8_t fontType, uint8_t scanline, uint8_t *text);
inline void add_header_bottom();
inline void add_normal_bottom(uint8_t line);
inline void add_text_colour(uint8_t colour);
inline void add_wsync();

inline void add_normal_top(uint8_t colour);
inline void add_exit_kernel();

const uint8_t __in_flash("start_bank") start_bank[] = {
#define PATCH_START_BANK 8

   0xd8,					// cld
   0x8d, 0xf4, 0xff,		// sta HOTSPOT
   0x4c, 0x62, 0x12,		// jmp ContDrawScreen
   0x8d, 0xf5, 0xff		// sta $FFF5,x					*** PATCH LOW BYTE OF ADDRESS ***

};

const uint8_t __in_flash("end_bank") end_bank[] = {

   0x8d, 0xf4, 0xff,		// sta HOTSPOT
   0x4c, 0x6B, 0x10,		// jmp DoStart					???

   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,			// hotspots

   0x0a, 0x10,				// .word FirstStart
   0x0a, 0x10,				// .word FirstStart
};

const uint8_t __in_flash("switch_bank") switch_bank[] = {

   0x4c, 0x07, 0x10		// jmp SwitchBank
};

const uint8_t __in_flash("mack_kernel_a_both") mac_kernel_a_both[] = {

   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea					// nop
};

const uint8_t __in_flash("mack_kernel_a") mac_kernel_a[] = {
   0x04, 0x00,				// nop 0			SLEEP 13
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea					// nop
};

const uint8_t __in_flash("mack_kernel_b_both") mac_kernel_b_both[] = {

   0x85, 0x2a,				// sta HMOVE
   0x85, 0x10,				// sta RESP0

   // sleep 10

   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea,					// nop
   0xea					// nop
};

const uint8_t __in_flash("kernel_a") kernel_a[] = {

#define PATCH_A_1	1
#define PATCH_A_2 	3
#define PATCH_A_3 	7
#define PATCH_A_4 	11
#define PATCH_A_5 	17
#define PATCH_A_6 	19
#define PATCH_A_7 	23
#define PATCH_A_8 	37

   0xa2, PATCH,			// ldx #??			(*** PATCHED ***) @1
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @3
   0x85, 0x1c,				// sta GRP1
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @7
   0x85, 0x1b,				// sta GRP0
   0xa0, PATCH,			// ldy #??			(*** PATCHED ***) @11
   0x8e, 0x1b, 0x00,		// stx.w GRP0
   0xea,					// nop
   0xa2, PATCH,			// ldx #??			(*** PATCHED ***) @17
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @19
   0x85, 0x1c,				// sta GRP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @23
   0x8d, 0x1c, 0x00,		// sta GRP0
   0x85, 0x10,				// sta RESP0
   0x84, 0x1b,				// sty GRP0
   0x85, 0x10,				// sta RESP0
   0x8e, 0x1b, 0x00,		// stx.w GRP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @37
   0x8d, 0x1b, 0x00,		// sta GRP0
   0xa2, 0x80,				// ldx #$80
   0x86, 0x21,				// stx HMP1
   0xea,					// nop
   0x85, 0x10				// sta RESP0
};

const uint8_t __in_flash("kernel_b") kernel_b[] = {

#define PATCH_B_1 	1
#define PATCH_B_2	3
#define PATCH_B_3	7
#define PATCH_B_4	9
#define PATCH_B_5	13
#define PATCH_B_6	17
#define PATCH_B_7	30
#define PATCH_B_8	35

   0xa0, PATCH,			// ldy #??			(*** PATCHED ***) @1
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @3
   0x85, 0x1c,				// sta GRP1
   0xa2, PATCH,			// ldx #??			(*** PATCHED ***) @7
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @9
   0x85, 0x1b,				// sta GRP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @13
   0x85, 0x1b,				// sta GRP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @17
   0x8d, 0x1b, 0x00,		// sta GRP0
   0x86, 0x1c,				// stx GRP1
   0x85, 0x10,				// sta RESP0
   0x84, 0x1c,				// sty GRP0
   0x85, 0x10,				// sta RESP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @30
   0x8d, 0x1b, 0x00,		// sta GRP0
   0xa9, PATCH,			// lda #??			(*** PATCHED ***) @35
   0x85, 0x1b,				// sta GRP0
   0xa2, 0x00,				// ldx #0
   0x86, 0x21,				// stx HMP1
   0x8d, 0x2a, 0x00,		// sta.w HMOVE
   0x8d, 0x10, 0x00		// sta.w RESP0
};

const uint8_t __in_flash("header_bottom") header_bottom[] = {
#define PATCH_HEADER_BOTTOM_BACKGROUND_COLOUR2 11

   0xa9, 0x00,				// lda #0
   0x85, 0x1b,				// sta GRP0
   0x85, 0x1c,				// sta GRP1

   0x85, 0x02,				// sta WSYNC
   0x85, 0x02,				// sta WSYNC

   0xa9, PATCH,			// lda *** PATCHED ***
   0x85, 0x09,				// sta COLUBK
};

const uint8_t __in_flash("normal_bottom") normal_bottom[] = {
#define PATCH_NORMAL_BOTTOM_LINE 9

   0xa9, 0x00,				// lda #0
   0x85, 0x1b,				// sta GRP0
   0x85, 0x1c,				// sta GRP1

   0xe6, lineCounter,		// inc lineCounter
   0xa5, PATCH,			// lda LineBackColor+{1}
};

const uint8_t __in_flash("normal_bottom2") normal_bottom2[] = {

   0x85, 0x09,				// sta COLUBK
};

const uint8_t __in_flash("wsync") wsync[] = {

   0x85, 0x02// sta WSYNC
};

const uint8_t __in_flash("text_colour") text_colour[] = {
#define PATCH_TEXT_COLOUR 1

   0xa9, 0x00,				// lda #??			 (*** PATCHED ***)
   0x85, 0x06,				// sta COLUP0
   0x85, 0x07,				// sta COLUP1

   0xa9, 0x00,
   0x85, lineCounter,		// sta lineCounter

   0x85, 0x02				// sta WSYNC
};

const uint8_t __in_flash("normal_top") normal_top[] = {
#define PATCH_NORMAL_TOP_TEXT_COLOUR 1

   0xa9, PATCH,			// lda #{1}			(*** PATCHED ***)
   0x85, 0x06,				// sta COLUP0
   0x85, 0x07,				// sta COLUP1
};

const uint8_t __in_flash("exit_kernel") exit_kernel[] = {
#define PATCH_EXIT_BG 1
   // bottom of screen, switch BG to black after bottom of last menu line

   0xa9, PATCH,			// lda #??
   0x85, 0x09,				// sta COLUBK
   0x4c, 0x00, 0x10		// jmp ExitKernel
};

const uint8_t __in_flash("zeroSprites") zeroSprites[] = {
   0xa2, 0x00,				// ldx #0
   0x86, 0x1b,				// stx GRP0
   0x86, 0x1c,				// stx GRP1
};

uint8_t *bufferp;

// for colours see...
// https://www.randomterrain.com/atari-2600-memories-tia-color-charts.html

// COLOUR		NTSC		PAL
// 0			WHITE		WHITE
// 1			YELLOW		WHITE
// 2			ORANGE		YELLOW
// 3			RED			GREEN
// 4			PINKY		ORANGE
// 5			PURPLE		GREEN
// 6			PURPLE/BLUE	PINK
// 7			BLUE		PASTEL GREEN
// 8			AQUA		PINK/PURPLE
// 9			AQUA		AQUA
// A			PASTEL GREEN	PURPLE
// B			GREEN		BLUE
// C			GREEN		PURPLE
// D			OLIVE		PURPLE/BLUE
// E			YELLOW		WHITE
// F			ORANGE		WHITE

const uint8_t __in_flash("textColour") textColour[2][14] = {

   {
      // NTSC...

      // see MENU_ENTRY_Type

      0x0a, //C8,//Leave_Menu,
      // --> ..
      // --> (Go Back)
      // .txt file

      0x2a, //2A,//Sub_Menu,
      0x0a, //48, //Cart_File,
      0x0A, //Input_Field,
      0x0A, //Keyboard_Char,
      0x0A, // keyboard row
      0X0A, //Menu_Action,
      0x0A, //Delete_Keyboard_Char,
      0x0A, //Offline_Cart_File,
      0x0A, //Offline_Sub_Menu,

      0xCA, //8C, //Setup_Menu
      // --> "Setup"
      // --> "Set TV Mode"
      // --> "Set Font Style"
      // --> "WiFi Setup"
      //		--> individual WiFi IDs

      //0x2A	// header line

      0x0a, //46, // Leave SubKeyboard Menu
      0x0A, //SD_Cart_File,
      0x0A, //SD_Sub_Menu,
   },

   {
      // PAL...

      //	0, //Root_Menu = -1,
      0x0A,//Leave_Menu,
      0x2E, //4A, //Sub_Menu,
      0x0a, //68, //Cart_File,
      0x0A, //Input_Field,
      0x0A, //Keyboard_Char,
      0x0A, // keyboard row
      0x0A, //Menu_Action,
      0x0A, //Delete_Keyboard_Char,
      0x0A, //Offline_Cart_File,
      0x0A, //Offline_Sub_Menu,

      0x3E, //BC, //Setup_Menu

      //0x0A	// header line

      0x0A, // Leave SubKeyboard Menu
      0x0A, //SD_Cart_File,
      0x0A, //SD_Sub_Menu,
   },

};

/*
 * Functions to append to the buffer the const "templates"
 * and fill in the dynamic values
 */

void add_wsync() {
   memcpy(bufferp, wsync, sizeof(wsync));
   bufferp += sizeof(wsync);
}

void add_start_bank(uint8_t bank_id) {
   bufferp = &buffer[(bank_id - 1) * 0x1000];
   memcpy(bufferp, start_bank, sizeof(start_bank));
   bufferp[PATCH_START_BANK] = (uint8_t)(bufferp[PATCH_START_BANK] + bank_id);
   bufferp += sizeof(start_bank);
}

void add_end_bank(uint8_t bank_id) {
   uint16_t next_bank = (uint16_t)(0x1000U * bank_id);
   memcpy(bufferp, switch_bank, sizeof(switch_bank));
   bufferp = &buffer[next_bank - sizeof(end_bank)];
   memcpy(bufferp, end_bank, sizeof(end_bank));
}

//displays: 00--00--11--11--11----00--00--00
void add_kernel_a(uint8_t fontType, uint8_t scanline, uint8_t *text) {

   memcpy(bufferp, kernel_a, sizeof(kernel_a));

   if(fontType > 3)
      fontType = 0;				// cater for uninit'd font

   bufferp[PATCH_A_1] = t2c(fontType, text[4], text[5], scanline);      // #{3}
   bufferp[PATCH_A_2] = t2c(fontType, text[8], text[9], scanline);      // #{4}
   bufferp[PATCH_A_3] = t2c(fontType, text[0], text[1], scanline);      // #{2}
   bufferp[PATCH_A_4] = (uint8_t)((t2c(fontType, text[22], text[23], scanline)) << 1); // #{7} << 1
   bufferp[PATCH_A_5] = (uint8_t)((t2c(fontType, text[26], text[27], scanline)) << 1); // #{8} << 1
   bufferp[PATCH_A_6] = t2c(fontType, text[12], text[13], scanline);    // #{5}
   bufferp[PATCH_A_7] = t2c(fontType, text[16], text[17], scanline);    // #{6}
   bufferp[PATCH_A_8] = (uint8_t)((t2c(fontType, text[30], text[31], scanline)) << 1); // #{9} << 1

   bufferp += sizeof(kernel_a);
}

//displays: --00--00--11--11--1100--00--00--
void add_kernel_b(uint8_t fontType, uint8_t scanline, uint8_t *text) {
   memcpy(bufferp, kernel_b, sizeof(kernel_b));

   if(fontType > 3)
      fontType = 0;				// cater for uninit'd font

   bufferp[PATCH_B_1] = t2c(fontType, text[18], text[19], scanline);    // #{6}
   bufferp[PATCH_B_2] = t2c(fontType, text[10], text[11], scanline);    // #{4}
   bufferp[PATCH_B_3] = t2c(fontType, text[14], text[15], scanline);    // #{5}
   bufferp[PATCH_B_4] = t2c(fontType, text[2], text[3], scanline);      // #{2}
   bufferp[PATCH_B_5] = t2c(fontType, text[6], text[7], scanline);      // #{3}
   bufferp[PATCH_B_6] = t2c(fontType, text[20], text[21], scanline);    // #{7}
   bufferp[PATCH_B_7] = t2c(fontType, text[24], text[25], scanline);    // #{8}
   bufferp[PATCH_B_8] = t2c(fontType, text[28], text[29], scanline);    // #{9}

   bufferp += sizeof(kernel_b);
}

void add_zeroSprites() {
   memcpy(bufferp, zeroSprites, sizeof(zeroSprites));
   bufferp += sizeof(zeroSprites);
}

void add_prefix_a(bool both) {
   if(both) {
      memcpy(bufferp, mac_kernel_a_both, sizeof(mac_kernel_a_both));
      bufferp += sizeof(mac_kernel_a_both);
   } else {
      memcpy(bufferp, mac_kernel_a, sizeof(mac_kernel_a));
      bufferp += sizeof(mac_kernel_a);
   }
}

void add_prefix_b() {
   memcpy(bufferp, mac_kernel_b_both, sizeof(mac_kernel_b_both));
   bufferp += sizeof(mac_kernel_b_both);
}

void addSpaceLines(int extra) {

   int lines = extra;

   switch(user_settings.line_spacing) {
      case 0:		//lines++;
         break;

      case 1:
         lines++;
         break;

      case 2:
         lines += 2;
         break;

      default:
         break;
   };

   for(uint8_t line = 0; line < lines; line++)
      add_wsync();
}

void add_header_bottom() {
   memcpy(bufferp, header_bottom, sizeof(header_bottom));
   bufferp[PATCH_HEADER_BOTTOM_BACKGROUND_COLOUR2] = user_settings.tv_mode == TV_MODE_NTSC ? BACK_COL_NTSC : BACK_COL_PAL;
   //	bufferp[PATCH_HEADER_BOTTOM_BACKGROUND_COLOUR2] = lineBackColour + 1;
   bufferp += sizeof(header_bottom);

   int extra = 0;

   switch(user_settings.line_spacing) {
      case 0:
         extra = 2;
         break;

      case 1:
         extra = 3;
         break;

      case 2:
         extra = 4;
         break;

      default:
         break;
   };

   addSpaceLines(extra);

   //	memcpy(bufferp, header_bottom2, sizeof(header_bottom2));
   //	bufferp[PATCH_HEADER_BOTTOM2_TEXT_COLOUR] = (uint8_t)lineBackColour;
   //	bufferp += sizeof(header_bottom2);
}

void add_normal_bottom(uint8_t line) {

   memcpy(bufferp, normal_bottom, sizeof(normal_bottom));
   bufferp[PATCH_NORMAL_BOTTOM_LINE] = (uint8_t)(lineBackColour + line + 1);
   bufferp += sizeof(normal_bottom);

   int extra = 0;

   if(user_settings.font_style == 0 && user_settings.line_spacing == 1)
      extra++;

   addSpaceLines(extra);

   memcpy(bufferp, normal_bottom2, sizeof(normal_bottom2));
   bufferp += sizeof(normal_bottom2);
}

void add_normal_top(uint8_t colour) {
   memcpy(bufferp, normal_top, sizeof(normal_top));
   bufferp[PATCH_NORMAL_TOP_TEXT_COLOUR] = colour;
   bufferp += sizeof(normal_top);

   int extra = 1;

   if(user_settings.font_style == 0 && user_settings.line_spacing == 1)
      extra--;

   addSpaceLines(extra);
}

void add_text_colour(uint8_t colour) {
   memcpy(bufferp, text_colour, sizeof(text_colour));
   bufferp[PATCH_TEXT_COLOUR] = colour;
   bufferp += sizeof(text_colour);
}

void add_exit_kernel() {

   memcpy(bufferp, zeroSprites, sizeof(zeroSprites));
   bufferp += sizeof(zeroSprites);

   addSpaceLines(0);

   memcpy(bufferp, exit_kernel, sizeof(exit_kernel));
   bufferp[PATCH_EXIT_BG] = user_settings.tv_mode == TV_MODE_NTSC ? BACK_COL_NTSC : BACK_COL_PAL;
   bufferp += sizeof(exit_kernel);
}

char cvtToNum(char *p) {
   const char *digits = "0123456789ABCDEF";
   return (char)(strchr(digits, *p) - digits);
}

void createMenuForAtari(
   MENU_ENTRY *menu_entries,
   uint8_t page_id,
   int num_menu_entries,
   bool is_connected,
   uint8_t *plus_store_status) {

   // create 7 banks of bytecode for the ATARI to execute.

   uint8_t menu_string[CHARS_PER_LINE];
   uint8_t sc, entry, odd_even;
   size_t str_len;
   uint8_t max_page = (uint8_t)((num_menu_entries - 1) / numMenuItemsPerPage[user_settings.line_spacing]);
   uint8_t items_on_last_page = (uint8_t)((num_menu_entries % numMenuItemsPerPage[user_settings.line_spacing]) ?
                                          (num_menu_entries % numMenuItemsPerPage[user_settings.line_spacing]) : numMenuItemsPerPage[user_settings.line_spacing]);
   uint8_t items_on_act_page = (uint8_t)((page_id < max_page) ? numMenuItemsPerPage[user_settings.line_spacing] : items_on_last_page);

   bufferp = buffer;
   memset(buffer, 0xff, 28 * 1024);

   unsigned int offset = (unsigned int)(numMenuItemsPerPage[user_settings.line_spacing] * page_id);

   set_menu_status_byte(STATUS_CurPage, page_id);
   set_menu_status_byte(STATUS_MaxPage, max_page);
   set_menu_status_byte(STATUS_ItemsOnActPage, items_on_act_page);

   memset(menu_header, ' ', sizeof(menu_header)/sizeof(char));

   // Display paging information

   uint8_t i = CHARS_PER_LINE - 1;

#if USE_WIFI

   // Account icon
   if(*plus_store_status == '1') {
      menu_header[i--] = CHAR_R_Account;
      menu_header[i--] = CHAR_L_Account;
   } else {
      menu_header[i--] = CHAR_R_NoAccount;
      menu_header[i--] = CHAR_L_NoAccount;
   }

   // Wifi icon
   if(is_connected) {
      menu_header[i--] = CHAR_R_Wifi;
      menu_header[i--] = CHAR_L_Wifi;
   } else {
      menu_header[i--] = CHAR_R_NoWifi;
      menu_header[i--] = CHAR_L_NoWifi;
   }

#endif

   // Page info
   if(max_page > 0) {

      uint8_t pagePos = i;
      i--;

      max_page++;

      while(max_page != 0) {
         menu_header[i--] = (char)((max_page % 10) + '0');
         max_page = max_page / 10;
      }

      menu_header[i--] = PATH_SEPARATOR;

      page_id++;

      while(page_id != 0) {
         menu_header[i--] = (char)((page_id % 10) + '0');
         page_id = page_id / 10;
      }

      // if the position would cause character glitching in 2-char page, then shift everything left

      if(i % 2 == 0) {
         for(uint8_t j = 8; j > 0; j--)
            menu_header[pagePos - j] = menu_header[pagePos - j + 1];

         i--;
      }

      menu_header[i--] = CHAR_R_Page;
      menu_header[i--] = CHAR_L_Page;

   }

   // "..." truncated status message
   // first, remove %XX encodings for visuals

   char *vp = pendingStatusMessage;

   for(char *p = pendingStatusMessage; *p; p++)
      if(*p == '%') {
         *vp++ = (char)(cvtToNum(p+1) * 16 + cvtToNum(p+2));
         p += 2;
      } else
         *vp++ = *p;

   *vp = 0;

   // now truncate path string to last visible n characters
   // put an "..." at the front of long status lines, and shift start point...

   vp = pendingStatusMessage;

   if(strlen(pendingStatusMessage) > i) {
      vp = pendingStatusMessage + strlen(pendingStatusMessage) - i;
      *vp = CHAR_PERIODPERIOD;
      *(vp+1) = CHAR_PERIODPERIOD2;
   }

   strncpy(menu_header, vp, i);

   uint8_t colourSet = user_settings.tv_mode == TV_MODE_NTSC ? 0 : 1;

   // Start of menu page generation

   uint8_t bank = 1;
   add_start_bank(bank);

   for(odd_even = 0; odd_even < 2; odd_even++) {

      for(entry = 0; entry <= numMenuItemsPerPage[user_settings.line_spacing]; entry++) {
         //bool is_kernel_a = bank < 4, isFolder = false;
         unsigned int list_entry = entry + offset;

         if(!entry) {		// header line

            add_text_colour(0x0A);
            memcpy(menu_string, menu_header, CHARS_PER_LINE);
            //isFolder = true;

         } else {

            list_entry--;
            add_normal_top(textColour[colourSet][menu_entries[list_entry].type]);

            memset(menu_string, ' ', CHARS_PER_LINE);

            if(list_entry < num_menu_entries) {
               str_len = strlen(menu_entries[list_entry].entryname);
               memcpy(menu_string, menu_entries[list_entry].entryname, str_len);
               //isFolder = (menu_entries[list_entry].type != Offline_Cart_File
               //		&& menu_entries[list_entry].type != Cart_File);
            }
         }

         for(uint8_t i = 0; i < CHARS_PER_LINE; i++)
            if(menu_string[i] < ' ' || menu_string[i] >= CHAR_END)
               menu_string[i] = ' ';

         for(sc = 0; sc < CHAR_HEIGHT; sc++) {

            if((odd_even + sc) & 1) {
               // BABABA...
               add_prefix_b();
               add_kernel_b(menu_entries[list_entry].font, sc, menu_string);

            } else {
               // ABABAB...
               add_prefix_a(sc==0);
               add_kernel_a(menu_entries[list_entry].font, sc, menu_string);
            }
         }

         //			add_zeroSprites();

         if(entry == 0) {
            add_header_bottom();
            add_normal_bottom(0);
         }

         else {

            if(entry < numMenuItemsPerPage[user_settings.line_spacing])
               add_normal_bottom(entry);
            else
               add_exit_kernel();

            if(entry == 4 || entry == 9 || entry == numMenuItemsPerPage[user_settings.line_spacing]) {
               add_end_bank(bank++);
               add_start_bank(bank);
            }
         }
      }
   }

   add_end_bank(bank);
}

void set_menu_status_msg(const char *message) {
   //memset(pendingStatusMessage, 0, sizeof(pendingStatusMessage)/sizeof(char));
   strncpy(pendingStatusMessage, message, sizeof(pendingStatusMessage)/sizeof(char) - 1);
}

void set_menu_status_byte(enum eStatus_bytes_id byte_id, uint8_t status_byte) {
   menu_status[byte_id] = status_byte;
}

void set_tv_mode(int tv_mode) {
   switch(tv_mode) {

      case TV_MODE_PAL:
         firmware_rom = firmware_pal_rom;
         break;

      case TV_MODE_PAL60:
         firmware_rom = firmware_pal60_rom;
         break;

      default:
      case TV_MODE_NTSC:
         firmware_rom = firmware_ntsc_rom;
         break;
   }
}

// We require the menu to do a write to $1FF4 to unlock the comms area.
// This is because the 7800 bios accesses this area on console startup, and we wish to ignore these
// spurious reads until it has started the cartridge in 2600 mode.
bool comms_enabled = false;

int __time_critical_func(emulate_firmware_cartridge)() {
   uint32_t irqstatus;
   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned const char *bankPtr = &firmware_rom[0];

   irqstatus = save_and_disable_interrupts(); // Disable interrupts

   while(true) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {  // A12 high
         if(comms_enabled) {

            // normal mode, once the cartridge code has done its init.
            // on a 7800, we know we are in 2600 mode now.

            // Quick-check range to prevent normal access doing 5+ comparisons...

            if(addr >= CART_CMD_HOTSPOT) {

               if(addr > 0x1FF4 && addr <= 0x1FFB) {	// bank-switch
                  bankPtr = &buffer[(addr - 0x1FF5) * 4 * 1024];
                  DATA_OUT(bankPtr[addr & 0xFFF]);
               }

               else if(addr == 0x1FF4) {
                  bankPtr = &firmware_rom[0];
                  DATA_OUT(bankPtr[addr & 0xFFF]);
               }

               else if(addr == CART_CMD_HOTSPOT) { // atari 2600 has send an command
                  while(ADDR_IN == addr) {
                     data_prev = data;
                     data = DATA_IN_BYTE;
                  }

                  addr = data_prev;
                  break;
               }

               else if(addr > CART_STATUS_BYTES_START - 1
                       && addr < CART_STATUS_BYTES_END + 1) {
                  DATA_OUT(menu_status[addr - CART_STATUS_BYTES_START]);
               }

               else if(addr > CART_STATUS_BYTES_END) {
                  DATA_OUT(end_bank[addr - (CART_STATUS_BYTES_END + 1)]);
               } else {
                  DATA_OUT(bankPtr[addr & 0xFFF]);
               }

            } else
               DATA_OUT(bankPtr[addr & 0xFFF]);

         } else {// prior to an access to $1FF4, we might be running on a 7800 with the CPU at
            // ~1.8MHz so we've got less time than usual - keep this short.
            if(addr > CART_STATUS_BYTES_END) {
               DATA_OUT(end_bank[addr - (CART_STATUS_BYTES_END + 1)]);
            } else {
               DATA_OUT(bankPtr[addr & 0xFFF]);
            }

            if(addr == 0x1FF4)  // we should move this comm enable hotspot because it is in the bankswitch area..
               comms_enabled = true;
         }

         SET_DATA_MODE_OUT

         while(ADDR_IN == addr);

         SET_DATA_MODE_IN
      }
   }

   restore_interrupts(irqstatus);
   return addr;
}

bool reboot_into_cartridge() {
   set_menu_status_byte(STATUS_StatusByteReboot, 1);
   return emulate_firmware_cartridge() == CART_CMD_START_CART;
}
