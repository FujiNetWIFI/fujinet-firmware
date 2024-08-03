#ifndef ATARI_MENU_H_
#define ATARI_MENU_H_

#include "menu.h"

MENU_ENTRY* generateSetupMenu(MENU_ENTRY *dst, int *num_menu_entries);
MENU_ENTRY* generateSystemInfo(MENU_ENTRY *dst, int* num_menu_entries, char *input_field);

#endif
