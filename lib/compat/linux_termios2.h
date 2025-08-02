#ifndef TERMIOS2_H
#define TERMIOS2_H

/*
   Termios2.h - defines for struct termios2 missing in glibc

   Copyright (C) 2018-2019 Matthias Reichl <hias@horus.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <sys/ioctl.h>
#include <termios.h>

#ifndef BOTHER
#define BOTHER		CBAUDEX
#endif

#ifndef LINUX_NCCS
#define LINUX_NCCS	19
#endif

#ifndef LINUX_IBSHIFT
#define LINUX_IBSHIFT	16
#endif

struct termios2 {
        tcflag_t c_iflag;               /* input mode flags */
        tcflag_t c_oflag;               /* output mode flags */
        tcflag_t c_cflag;               /* control mode flags */
        tcflag_t c_lflag;               /* local mode flags */
        cc_t c_line;                    /* line discipline */
        cc_t c_cc[LINUX_NCCS];          /* control characters */
        speed_t c_ispeed;               /* input speed */
        speed_t c_ospeed;               /* output speed */
};

#ifndef TCGETS2
#define TCGETS2 0x2A
#endif

#ifndef TCSETS2
#define TCSETS2 0x2B
#endif

#ifndef TCSETSW2
#define TCSETSW2 0x2C
#endif

#ifndef TCSETSF2
#define TCSETSF2 0x2D
#endif

#endif

