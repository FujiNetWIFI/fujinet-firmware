#ifndef USER_SETTINGS_H_
#define USER_SETTINGS_H_

#include <stdint.h>

typedef struct {
   uint8_t tv_mode;
   uint8_t font_style;
   uint8_t line_spacing;
   uint8_t first_free_flash_sector;
} USER_SETTINGS;

enum TV_MODE {
   TV_MODE_UNKNOWN,

   TV_MODE_NTSC,     TV_MODE_DEFAULT = 1,
   TV_MODE_PAL,
   TV_MODE_PAL60,

   TV_MODE_MAX

};

enum FONT_TYPE {
   FONT_TJZ,
   FONT_AD,
   FONT_MORGAN,
   FONT_GLACIER,     FONT_DEFAULT = 3,

   FONT_MAX

};

enum SPACING {
   SPACING_DENSE,
   SPACING_REGULAR,  SPACING_DEFAULT = 1,
   SPACING_SPARSE,

   SPACING_MAX
};

extern USER_SETTINGS user_settings;

#endif
