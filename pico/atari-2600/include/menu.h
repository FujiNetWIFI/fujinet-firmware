#ifndef MENU_H_
#define MENU_H_

#include <stdint.h>

#define NUM_MENU_ITEMS                 1024
#define CHARS_PER_LINE                   32
#define STATUS_MESSAGE_LENGTH           256

#define MENU_TEXT_GO_BACK                   "(Go Back)"
#define MENU_TEXT_DELETE_CHAR               "Delete Character"
#define MENU_TEXT_SD_CARD_CONTENT           "SD-Card Content"
#define MENU_TEXT_OFFLINE_ROMS              "Offline ROMs"
#define MENU_TEXT_DETECT_OFFLINE_ROMS       "Detect Offline ROMs"
#define MENU_TEXT_DELETE_OFFLINE_ROMS       "Erase Offline ROMs"
#define MENU_TEXT_SETUP                     "Setup"
#define MENU_TEXT_WIFI_SETUP                "WiFi Connection"
#define MENU_TEXT_WIFI_SELECT               "Scan for WiFi Access Points"
#define MENU_TEXT_WIFI_WPS_CONNECT          "WiFi WPS Connect"
#define MENU_TEXT_WIFI_MANAGER              "Start WiFi Manager Portal"
#define MENU_TEXT_ESP8266_RESTORE           "Delete WiFi Connections"
#define MENU_TEXT_ESP8266_UPDATE            "WiFi Firmware Update"
//#define MENU_TEXT_APPEARANCE                "Appearance"
#define MENU_TEXT_DISPLAY                   "Display Preferences"
#define MENU_TEXT_DISABLE_EMU_EXIT        "Disable Right+Reset Exit"
#define MENU_TEXT_ENABLE_EMU_EXIT         "Enable Right+Reset Exit"
#define MENU_TEXT_WIFI_RECONNECT            "WiFi Retry"
#define MENU_TEXT_TV_MODE_SETUP             "TV Mode"
#define MENU_TEXT_TV_MODE_PAL               "  PAL"
#define MENU_TEXT_TV_MODE_PAL60             "  PAL 60 Hz"
#define MENU_TEXT_TV_MODE_NTSC              "  NTSC"
#define MENU_TEXT_FONT_SETUP                "Font Style"
#define MENU_TEXT_FONT_TJZ                  (const char *) "  Small Caps"
#define MENU_TEXT_FONT_TRICHOTOMIC12        "  Trichotomic-12"
#define MENU_TEXT_FONT_CAPTAIN_MORGAN_SPICE "  Captain Morgan Spice"
#define MENU_TEXT_FONT_GLACIER_BELLE        "  Glacier Belle"

#define MENU_TEXT_FORMAT_EEPROM             "Format User Settings"

#define MENU_TEXT_SPACING_SETUP             "Line Spacing"
#define MENU_TEXT_SPACING_DENSE             "  Dense"
#define MENU_TEXT_SPACING_MEDIUM            "  Regular"
#define MENU_TEXT_SPACING_SPARSE            "  Sparse"

#define MENU_TEXT_PRIVATE_KEY               "Private Key"
#define MENU_TEXT_FIRMWARE_UPDATE           "** WiFi Firmware Update **"
#define MENU_TEXT_SD_FIRMWARE_UPDATE        "** SD-Card Firmware Update **"
#define MENU_TEXT_OFFLINE_ROM_UPDATE        "Download Offline ROMs"
#define MENU_TEXT_PLUS_CONNECT              "PlusStore Connect"
#define MENU_TEXT_PLUS_REMOVE               "PlusStore Disconnect"

#define MENU_TEXT_SYSTEM_INFO               "System Info"
#define MENU_TEXT_SEARCH_FOR_ROM            "Search ROM"
#define MENU_TEXT_SEARCH_FOR_SD_ROM         "Search SD ROM"
#define MENU_TEXT_SPACE                     "Space"
#define MENU_TEXT_LOWERCASE                 "Lowercase"
#define MENU_TEXT_UPPERCASE                 "Uppercase"
#define MENU_TEXT_SYMBOLS                   "Symbols"

#define URLENCODE_MENU_TEXT_SYSTEM_INFO     "System%20Info"
#define URLENCODE_MENU_TEXT_PLUS_CONNECT    "PlusStore%20Connect"
#define URLENCODE_MENU_TEXT_SETUP           "Setup"

#define STD_ROM_AUTOSTART_FILENAME_PREFIX   "Autostart."
#define PLUSROM_AUTOSTART_FILENAME_PREFIX   "PlusROM.Autostart."
#define EXIT_FUNCTION_DISABLE_FILENAME_KEY  ".noexit"

#define PATH_SEPARATOR '/' /*CHAR_SELECTION*/

#define SIZEOF_WIFI_SELECT_BASE_PATH        sizeof(MENU_TEXT_SETUP) + sizeof(MENU_TEXT_WIFI_SETUP) + sizeof(MENU_TEXT_WIFI_SELECT)

enum MENU_ENTRY_Type {
   Root_Menu = -1,
   Leave_Menu,
   Sub_Menu,  // should be PlusStore or WiFi_Sub_Menu
   Cart_File,
   Input_Field,
   Keyboard_Char,
   Keyboard_Row,
   Menu_Action,
   Delete_Keyboard_Char,
   Offline_Cart_File,  // should be Flash_Sub_Menu
   Offline_Sub_Menu,  // should be Flash_Sub_Menu
   Setup_Menu,
   Leave_SubKeyboard_Menu,
   SD_Cart_File,
   SD_Sub_Menu,
};

typedef struct {
   enum MENU_ENTRY_Type type;
   char entryname[CHARS_PER_LINE+1];
   uint32_t filesize;
   uint32_t flash_base_address;
   uint8_t font;
} MENU_ENTRY;

enum eStatus_bytes_id {
   STATUS_StatusByteReboot,
   STATUS_CurPage,
   STATUS_MaxPage,
   STATUS_ItemsOnActPage,
   STATUS_PageType,
   STATUS_Unused1,
   STATUS_Unused2,

   STATUS_MAX
};

enum eStatus_bytes_PageTypes {
   Directory,
   Menu,
   Keyboard
};

enum e_status_message {
   STATUS_NONE = -2,
   STATUS_MESSAGE_STRING,
   STATUS_ROOT,
   select_wifi_network,
   wifi_not_connected,
   wifi_connected,
   esp_timeout,
   insert_password,
   plus_connect,
   STATUS_YOUR_MESSAGE,
   offline_roms_deleted,
   not_enough_menory,
   romtype_ACE_unsupported,
   romtype_unknown,
   done,
   failed,
   download_failed,
   offline_roms_detected,
   no_offline_roms_detected,
   romtype_DPCplus_unsupported,
   exit_emulation,
   rom_download_failed,

   STATUS_SETUP,
   STATUS_SETUP_TV_MODE,
   STATUS_SETUP_FONT_STYLE,
   STATUS_SETUP_LINE_SPACING,
   STATUS_SETUP_SYSTEM_INFO,
   STATUS_SEARCH_FOR_ROM,
   STATUS_SEARCH_FOR_SD_ROM,
   STATUS_SEARCH_DETAILS,
   STATUS_CHOOSE_ROM,

   STATUS_APPEARANCE,

};

extern const char *status_message[];
extern const uint8_t numMenuItemsPerPage[];

extern const char *keyboardUppercase[];
extern const char *keyboardLowercase[];
extern const char *keyboardSymbols[];
extern const char **keyboards[];

enum keyboardType {
   KEYBOARD_UPPERCASE,
   KEYBOARD_LOWERCASE,
   KEYBOARD_SYMBOLS,
   KEYBOARD_NONE,
};

char *get_filename_ext(char *filename);
void make_menu_entry_font(MENU_ENTRY **dst, const char *name, int type, uint8_t font, int* num_menu_entries);
void make_menu_entry(MENU_ENTRY **dst, const char *name, int type, int* num_menu_entries);

void make_keyboardFromLine(MENU_ENTRY **dst, char *line, int* num_menu_entries);
void make_keyboard(MENU_ENTRY **dst, enum keyboardType selector, int* num_menu_entries, char *input_field = NULL);

#endif
