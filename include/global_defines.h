// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef GLOBAL_DEFINES_H
#define GLOBAL_DEFINES_H

#include "ansi_codes.h"

#define PRODUCT_ID "MEATLOAF CBM"
#define PLATFORM_DETAILS "C64; 6510; 2; NTSC; EN;" // Make configurable. This will help server side to select appropriate content.

#define BIN_DIR     "/.bin"
#define CACHE_DIR   "/.cache"
#define PRINT_DIR   "/.print"
#define ROM_DIR     "/.rom"
#define SYSTEM_DIR  "/.sys"
#define WWW_ROOT    "/.www"

#define HOSTNAME "meatloaf"
#define SERVER_PORT 80   // HTTPd & WebDAV Server Port
#define LISTEN_PORT 6400 // Listen to this if not connected. Set to zero to disable.

#endif // GLOBAL_DEFINES_H
