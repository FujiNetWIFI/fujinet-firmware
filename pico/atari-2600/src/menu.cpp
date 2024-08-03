#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"

#include "global.h"
#include "menu.h"
#include "user_settings.h"

const char __in_flash("status_message") *status_message[] = {

#if MENU_TYPE == UNOCART
   "UnoCart 2600",
#else
   "PlusCart(+)",
#endif
   "Select WiFi Network",
   "No WiFi",
   "WiFi connected",
   "Request timeout",
   "Enter WiFi Password",
   "Enter email or username",
   "Your Chat Message",
   "Offline ROMs erased",
   "ROM file too big!",
   "ACE file unsupported",
   "Unknown/invalid ROM",
   "Done",
   "Failed",
   "Firmware download failed",
   "Offline ROMs detected",
   "No offline ROMs detected",
   "DPC+ is not supported",
   "Emulation exited",
   "ROM Download Failed",
   "Setup",
   "Select TV Mode",
   "Select Font",
   "Select Line Spacing",
   "Setup/System Info",
   MENU_TEXT_SEARCH_FOR_ROM,
   MENU_TEXT_SEARCH_FOR_SD_ROM,
   "Enter search details",
   "Search results",

   // MENU_TEXT_APPEARANCE,
};

const uint8_t numMenuItemsPerPage[] = {
   // ref: SPACING enum
   14,                           // dense
   12,                           // medium
   10                         // sparse
};

const char __in_flash("keyboardUppercase") *keyboardUppercase[] = {
   " 1  2  3  4  5  6  7  8  9  0",
   "  Q  W  E  R  T  Y  U  I  O  P",
   "   A  S  D  F  G  H  J  K  L",
   "    Z  X  C  V  B  N  M",
   "     " MENU_TEXT_SPACE " !  ?  ,  .",
   0
};

const char __in_flash("keyboardLowercase") *keyboardLowercase[] = {
   " 1  2  3  4  5  6  7  8  9  0",
   "  q  w  e  r  t  y  u  i  o  p",
   "   a  s  d  f  g  h  j  k  l",
   "    z  x  c  v  b  n  m",
   "     " MENU_TEXT_SPACE " !  ?  ,  .",
   0
};

const char __in_flash("keyboardSymbols") *keyboardSymbols[] = {
   " " MENU_TEXT_SPACE "   ( )  { }  [ ]  < >",
   "  !  ?  .  ,  :  ;  \"  '  `",
   "   @  ^  |  \\  ~  #  $  %  &",
   "    +  -  *  /  =  _",
   0
};

const char __in_flash("keyboards") **keyboards[] = {
   keyboardUppercase,
   keyboardLowercase,
   keyboardSymbols,
   0
};

// functions

char *get_filename_ext(char *filename) {
   char *dot = strrchr(filename, '.');

   if(!dot || dot == filename) return (char *)"";

   return (dot + 1);
}

/*inline*/ void make_menu_entry_font(MENU_ENTRY **dst, const char *name, int type, uint8_t font, int* num_menu_entries) {
   (*dst)->type = (MENU_ENTRY_Type)type;
   strcpy((*dst)->entryname, name);
   (*dst)->filesize = 0U;
   (*dst)->font = font;
   (*dst)++;
   (*num_menu_entries)++;
}

// helper for make_menu_entry_font with font from user_settings
void make_menu_entry(MENU_ENTRY **dst, const char *name, int type, int* num_menu_entries) {
   make_menu_entry_font(dst, name, type, user_settings.font_style, num_menu_entries);
}

void make_keyboardFromLine(MENU_ENTRY **dst, char *line, int* num_menu_entries) {

   make_menu_entry(dst, MENU_TEXT_GO_BACK, Leave_SubKeyboard_Menu, num_menu_entries);
   char item[33];

   while(*line) {
      char *entry = item;

      while(*line && *line == ' ')
         line++;

      while(*line && *line != ' ')
         *entry++ = *line++;

      *entry = 0;

      if(*item)
         make_menu_entry(dst, item, Keyboard_Char, num_menu_entries);
   }
}

void make_keyboard(MENU_ENTRY **dst, enum keyboardType selector, int* num_menu_entries, char *input_field) {

   make_menu_entry(dst, MENU_TEXT_GO_BACK, Leave_Menu, num_menu_entries);

   for(const char **kbRow = keyboards[selector]; *kbRow; kbRow++)
      make_menu_entry(dst, *kbRow, Keyboard_Row, num_menu_entries);

   if(selector != KEYBOARD_LOWERCASE)
      make_menu_entry(dst, MENU_TEXT_LOWERCASE, Keyboard_Row, num_menu_entries);

   if(selector != KEYBOARD_UPPERCASE)
      make_menu_entry(dst, MENU_TEXT_UPPERCASE, Keyboard_Row, num_menu_entries);

   if(selector != KEYBOARD_SYMBOLS)
      make_menu_entry(dst, MENU_TEXT_SYMBOLS, Keyboard_Row, num_menu_entries);

   if(input_field)
      make_menu_entry(dst, MENU_TEXT_DELETE_CHAR, Delete_Keyboard_Char, num_menu_entries);

   make_menu_entry(dst, "Enter", Menu_Action, num_menu_entries);
}
