#ifdef BUILD_ATARI

/*
 * This file contains code from the SIO2BSD project by KMK (drac030)
 *
 * based on sio2bsd.c version 1.22
 * adapted for FujiNet
 *
 */

#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <ctime>
#include <ctype.h>
#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

#include "compat_dirent.h"

#include "modem.h"
#include "utils.h"
#include "fnConfig.h"
#include "fnSystem.h"

#include "pclink.h"

#include "../../include/debug.h"

#if defined(_WIN32)
#include <direct.h>
#define mkdir(A, B) _mkdir(A)
#endif

# ifndef uchar
#  define uchar unsigned char
# endif

# ifndef ushort
#  define ushort unsigned short
# endif

# ifndef ulong
#  define ulong unsigned long
# endif

# define SIOTRACE

# ifdef SIOTRACE
static int log_flag = 0;                /* enable more SIO messages, if 1 */
# endif

# define SDX_MAXLEN 16777215L

/* SDX required attribute mask */
# define RA_PROTECT     0x01
# define RA_HIDDEN      0x02
# define RA_ARCHIVED    0x04
# define RA_SUBDIR      0x08
# define RA_NO_PROTECT  0x10
# define RA_NO_HIDDEN   0x20
# define RA_NO_ARCHIVED 0x40
# define RA_NO_SUBDIR   0x80

/* SDX set attribute mask */
# define SA_PROTECT     0x01
# define SA_UNPROTECT   0x10
# define SA_HIDE        0x02
# define SA_UNHIDE      0x20
# define SA_ARCHIVE     0x04
# define SA_UNARCHIVE   0x40
# define SA_SUBDIR      0x08    /* illegal mode */
# define SA_UNSUBDIR    0x80    /* illegal mode */

# define DEVICE_LABEL   ".PCLINK.VOLUME.LABEL"

# define PCL_MAX_FNO    0x14

static const char *fun[] =
{
        "FREAD", "FWRITE", "FSEEK", "FTELL", "FLEN", "(none)", "FNEXT", "FCLOSE",
        "INIT", "FOPEN", "FFIRST", "RENAME", "REMOVE", "CHMOD", "MKDIR", "RMDIR",
        "CHDIR", "GETCWD", "SETBOOT", "DFREE", "CHVOL"
};

/* Atari SIO status block */
typedef struct
{
        uchar stat;
        uchar err;
        uchar tmot;
        uchar none;
} STATUS;

typedef struct                  /* PCLink parameter buffer */
{
        uchar fno;                  /* function number */
        uchar handle;           /* file handle */
        uchar f1,f2,f3,f4;      /* general-purpose bytes */
        uchar f5,f6;            /* more general-purpose bytes */
        uchar fmode;            /* fmode */
        uchar fatr1;            /* fatr1 */
        uchar fatr2;            /* fatr2 */
        uchar name[12];         /* name */
        uchar names[12];        /* names */
        uchar path[65];         /* path */
} PARBUF;

typedef struct
{
        STATUS status;          /* the 4-byte status block */
        int on;                     /* PCLink mount flag */
        char dirname[1024];     /* PCLink root directory path */
        uchar cwd[65];          /* PCLink current working dir, relative to the above */
        PARBUF parbuf;          /* PCLink parameter buffer */
} DEVICE;

typedef struct
{
        uchar status;
        uchar map_l, map_h;
        uchar len_l, len_m, len_h;
        char fname[11];
        uchar stamp[6];
} DIRENTRY;

static struct
{
        union
        {
                FILE *file;
                DIR *dir;
        } fps;

        DIRENTRY *dir_cache;    /* used only for directories */

        uchar devno;
        uchar cunit;
        uchar fpmode;
        uchar fatr1;
        uchar fatr2;
        uchar t1,t2,t3;
        uchar d1,d2,d3;
        struct stat fpstat;
        char fpname[12];
        long fppos;
        long fpread;
        int eof;
        char pathname[1024];
} iodesc[16];

static struct
{
        uchar handle;
        uchar dirbuf[23];
} pcl_dbf;

//static ulong upper_dir = UPPER_DIR;
static ulong upper_dir = 0;

static DEVICE device[16];       /* one PCLINK device with 16 units */

#  define COM_COMD 0
#  define COM_DATA 1

static void pclink_ack(ushort devno, ushort d, uchar what);
static uint8_t pclink_read(uint8_t *buf, int len);
static void pclink_write(uint8_t *buf, int len);

/* Calculate Atari-style CRC for the given buffer
 */
static uchar
calc_checksum(uchar *buf, int how_much)
{
        uchar cksum = 0;
        ushort nck;
        int i;

        for (i = 0; i < how_much; i++)
        {
                nck = cksum + buf[i];
                cksum = (nck > 0x00ff) ? (nck + 1) : nck;
                cksum &= 0x00ff;
        }

        return cksum;
}

static void
unix_time_2_sdx(time_t *todp, uchar *ob)
{
        struct tm *t;
        uchar yy;

        memset(ob, 0, 6);

        if (*todp == 0)
                return;

        t = localtime(todp);

        yy = t->tm_year;
        while (yy >= 100)
                yy-=100;

        ob[0] = t->tm_mday;
        ob[1] = t->tm_mon + 1;
        ob[2] = yy;
        ob[3] = t->tm_hour;
        ob[4] = t->tm_min;
        ob[5] = t->tm_sec;
}

static long
dos_2_allowed(uchar c)
{
//# ifndef __CYGWIN__
#if 1
        if (upper_dir)
                return (isupper(c) || isdigit(c) || (c == '_') || (c == '@'));

        return (islower(c) || isdigit(c) || (c == '_') || (c == '@'));
# else
        return (isalpha(c) || isdigit(c) || (c == '_') || (c == '@'));
# endif
}

static long
dos_2_term(uchar c)
{
        return ((c == 0) || (c == 0x20));
}

static long
validate_fn(uchar *name, int len)
{
        int x;

        for (x = 0; x < len; x++)
        {
                if (dos_2_term(name[x]))
                        return (x != 0);
                if (name[x] == '.')
                        return 1;
                if (!dos_2_allowed(name[x]))
                        return 0;
        }

        return 1;
}

static void
ugefina(char *src, char *out)
{
        char *dot;
        ushort i;

        memset(out, 0x20, 8+3);

        dot = strchr(src, '.');

        if (dot)
        {
                i = 1;
                while (dot[i] && (i < 4))
                {
                        out[i+7] = toupper((uchar)dot[i]);
                        i++;
                }
        }

        i = 0;
        while ((src[i] != '.') && !dos_2_term(src[i]) && (i < 8))
        {
                out[i] = toupper((uchar)src[i]);
                i++;
        }
}

static void
uexpand(uchar *rawname, char *name83)
{
        ushort x, y;
        uchar t;

        name83[0] = 0;

        for (x = 0; x < 8; x++)
        {
                t = rawname[x];
                if (t && (t != 0x20))
                        name83[x] = upper_dir ? toupper(t) : tolower(t);
                else
                        break;
        }

        y = 8;

        if (rawname[y] && (rawname[y] != 0x20))
        {
                name83[x] = '.';
                x++;

                while ((y < 11) && rawname[y] && (rawname[y] != 0x20))
                {
                        name83[x] = upper_dir ? toupper(rawname[y]) : tolower(rawname[y]);
                        x++;
                        y++;
                }
        }

        name83[x] = 0;
}

static int
match_dos_names(char *name, char *mask, uchar fatr1, struct stat *sb)
{
        ushort i;

        if (log_flag)
        {
                Debug_printf("match: %c%c%c%c%c%c%c%c%c%c%c with %c%c%c%c%c%c%c%c%c%c%c: ",
                name[0], name[1], name[2], name[3], name[4], name[5], name[6], name[7], \
                name[8], name[9], name[10], \
                mask[0], mask[1], mask[2], mask[3], mask[4], mask[5], mask[6], mask[7], \
                mask[8], mask[9], mask[10]);
        }

        for (i = 0; i < 11; i++)
        {
                if (mask[i] != '?')
                        if (toupper((uchar)name[i]) != toupper((uchar)mask[i]))
                        {
                                if (log_flag)
                                        Debug_printf("no match\n");
                                return 1;
                        }
        }

        /* There are no such attributes in Unix */
        fatr1 &= ~(RA_NO_HIDDEN|RA_NO_ARCHIVED);

        /* Now check the attributes */
        if (fatr1 & (RA_HIDDEN | RA_ARCHIVED))
        {
                if (log_flag)
                        Debug_printf("atr mismatch: not HIDDEN or ARCHIVED\n");
                return 1;
        }

        if (fatr1 & RA_PROTECT)
        {
                if (sb->st_mode & (S_IWUSR|S_IWGRP))
                {
                        if (log_flag)
                                Debug_printf("atr mismatch: not PROTECTED\n");

                        return 1;
                }
        }

        if (fatr1 & RA_NO_PROTECT)
        {
                if ((sb->st_mode & (S_IWUSR|S_IWGRP)) == 0)
                {
                        if (log_flag)
                                Debug_printf("atr mismatch: not UNPROTECTED\n");

                        return 1;
                }
        }

        if (fatr1 & RA_SUBDIR)
        {
                if (!S_ISDIR(sb->st_mode))
                {
                        if (log_flag)
                                Debug_printf("atr mismatch: not SUBDIR\n");

                        return 1;
                }
        }

        if (fatr1 & RA_NO_SUBDIR)
        {
                if (S_ISDIR(sb->st_mode))
                {
                        if (log_flag)
                                Debug_printf("atr mismatch: not FILE\n");

                        return 1;
                }
        }

        if (log_flag)
                Debug_printf("match\n");

        return 0;
}

static int
validate_dos_name(char *fname)
{
        char *dot = strchr(fname, '.');
        long valid_fn, valid_xx;

        if ((dot == NULL) && (strlen(fname) > 8))
                return 1;
        if (dot)
        {
                long dd = strlen(dot);

                if (dd > 4)
                        return 1;
                if ((dot - fname) > 8)
                        return 1;
                if ((dot == fname) && (dd == 1))
                        return 1;
                if ((dd == 2) && (dot[1] == '.'))
                        return 1;
                if ((dd == 3) && ((dot[1] == '.') || (dot[2] == '.')))
                        return 1;
                if ((dd == 4) && ((dot[1] == '.') || (dot[2] == '.') || (dot[3] == '.')))
                        return 1;
        }

        valid_fn = validate_fn((uchar *)fname, 8);
        if (dot != NULL)
                valid_xx = validate_fn((uchar *)(dot + 1), 3);
        else
                valid_xx = 1;

        if (!valid_fn || !valid_xx)
                return 1;

        return 0;
}

static int
check_dos_name(char *newpath, struct dirent *dp, struct stat *sb)
{
        char temp_fspec[1024], fname[256];

        strcpy(fname, dp->d_name);

        if (log_flag)
                Debug_printf("%s: got fname '%s'\n", __func__, fname);

        if (validate_dos_name(fname))
                return 1;

        /* stat() the file (fetches the length) */
        snprintf(temp_fspec, sizeof(temp_fspec), "%s/%s", newpath, fname);

        if (log_flag)
                Debug_printf("%s: stat '%s'\n", __func__, temp_fspec);

        if (stat(temp_fspec, sb))
        {
                Debug_printf("cannot stat '%s'\n", temp_fspec);
                return 1;
        }

        if (!S_ISREG(sb->st_mode) && !S_ISDIR(sb->st_mode))
        {
                Debug_printf("'%s' is not regular file nor directory\n", temp_fspec);
                return 1;
        }

        /*if (sb->st_uid != our_uid)
        {
                Debug_printf("'%s' wrong uid\n", temp_fspec);
                return 1;
        }*/

        if ((sb->st_mode & S_IRUSR) == 0)
        {
                Debug_printf("'%s' is unreadable\n", temp_fspec);
                return 1;
        }

        if (S_ISDIR(sb->st_mode) && ((sb->st_mode & S_IXUSR) == 0))
        {
                Debug_printf("dir '%s' is unbrowseable\n", temp_fspec);
                return 1;
        }

        return 0;
}

static void
fps_close(int i)
{
        if (iodesc[i].fps.file != NULL)
        {
                if (iodesc[i].fpmode & 0x10)
                        closedir(iodesc[i].fps.dir);
                else
                        fclose(iodesc[i].fps.file);
        }

        if (iodesc[i].dir_cache != NULL)
        {
                free(iodesc[i].dir_cache);
                iodesc[i].dir_cache = NULL;
        }

        iodesc[i].fps.file = NULL;

        iodesc[i].devno = 0;
        iodesc[i].cunit = 0;
        iodesc[i].fpmode = 0;
        iodesc[i].fatr1 = 0;
        iodesc[i].fatr2 = 0;
        iodesc[i].t1 = 0;
        iodesc[i].t2 = 0;
        iodesc[i].t3 = 0;
        iodesc[i].d1 = 0;
        iodesc[i].d2 = 0;
        iodesc[i].d3 = 0;
        iodesc[i].fpname[0] = 0;
        iodesc[i].fppos = 0;
        iodesc[i].fpread = 0;
        iodesc[i].eof = 0;
        iodesc[i].pathname[0] = 0;
        memset(&iodesc[i].fpstat, 0, sizeof(struct stat));
}

static ulong
get_file_len(uchar handle)
{
        ulong filelen;
        struct dirent *dp;
        struct stat sb;

        if (iodesc[handle].fpmode & 0x10)       /* directory */
        {
                rewinddir(iodesc[handle].fps.dir);
                filelen = sizeof(DIRENTRY);

                while ((dp = readdir(iodesc[handle].fps.dir)) != NULL)
                {
                        if (check_dos_name(iodesc[handle].pathname, dp, &sb))
                                continue;
                        filelen += sizeof(DIRENTRY);
                }
                rewinddir(iodesc[handle].fps.dir);
        }
        else
                filelen = iodesc[handle].fpstat.st_size;

        if (filelen > SDX_MAXLEN)
                filelen = SDX_MAXLEN;

        return filelen;
}

static DIRENTRY *
cache_dir(uchar handle)
{
        char *bs, *cwd;
        uchar dirnode = 0x00;
        ushort node;
        ulong dlen, flen, sl, dirlen = iodesc[handle].fpstat.st_size;
        DIRENTRY *dbuf, *dir;
        struct dirent *dp;
        struct stat sb;

        if (iodesc[handle].dir_cache != NULL)
        {
                Debug_printf("Internal error: dir_cache should be NULL!\n");
                //sig(0);
        }

        dir = dbuf = (DIRENTRY*)malloc(dirlen + sizeof(DIRENTRY));
        memset(dbuf, 0, dirlen + sizeof(DIRENTRY));

        dir->status = 0x28;
        dir->map_l = 0x00;                      /* low 11 bits: file number, high 5 bits: dir number */
        dir->map_h = dirnode;
        dir->len_l = dirlen & 0x000000ffL;
        dir->len_m = (dirlen & 0x0000ff00L) >> 8;
        dir->len_h = (dirlen & 0x00ff0000L) >> 16;

        memset(dir->fname, 0x20, 11);

        sl = strlen(device[iodesc[handle].cunit].dirname);

        cwd = iodesc[handle].pathname + sl;

        bs = strrchr(cwd, '/');

        if (bs == NULL)
                memcpy(dir->fname, "MAIN", 4);
        else
        {
                char *cp = cwd;

                ugefina(bs+1, (char *)dir->fname);

                node = 0;

                while (cp <= bs)
                {
                        if (*cp == '/')
                                dirnode++;
                        cp++;
                }

                dir->map_h = (dirnode & 0x1f) << 3;
        }

        unix_time_2_sdx(&iodesc[handle].fpstat.st_mtime, dir->stamp);

        dir++;
        flen = sizeof(DIRENTRY);

        node = 1;

        while ((dp = readdir(iodesc[handle].fps.dir)) != NULL)
        {
                ushort map;

                if (check_dos_name(iodesc[handle].pathname, dp, &sb))
                        continue;

                dlen = sb.st_size;
                if (dlen > SDX_MAXLEN)
                        dlen = SDX_MAXLEN;

                dir->status = (sb.st_mode & (S_IWUSR|S_IWGRP)) ? 0x08 : 0x09;

                if (S_ISDIR(sb.st_mode))
                {
                        dir->status |= 0x20;            /* directory */
                        dlen = sizeof(DIRENTRY);
                }

                map = dirnode << 11;
                map |= (node & 0x07ff);

                dir->map_l = map & 0x00ff;
                dir->map_h = ((map & 0xff00) >> 8);
                dir->len_l = dlen & 0x000000ffL;
                dir->len_m = (dlen & 0x0000ff00L) >> 8;
                dir->len_h = (dlen & 0x00ff0000L) >> 16;

                ugefina(dp->d_name, (char *)dir->fname);

                unix_time_2_sdx(&sb.st_mtime, dir->stamp);

                node++;
                dir++;
                flen += sizeof(DIRENTRY);

                if (flen >= dirlen)
                        break;
        }

        return dbuf;
}

static ulong
dir_read(uchar *mem, ulong blk_size, uchar handle, int *eof_sig)
{
        uchar *db = (uchar *)iodesc[handle].dir_cache;
        ulong dirlen = iodesc[handle].fpstat.st_size, newblk;

        eof_sig[0] = 0;

        newblk = dirlen - iodesc[handle].fppos;

        if (newblk < blk_size)
        {
                blk_size = newblk;
                eof_sig[0] = 1;
        }

        if (blk_size)
                memcpy(mem, db+iodesc[handle].fppos, blk_size);

        return blk_size;
}

static void
do_pclink_init(int server_cold_start)
{
        uchar handle;

        if (server_cold_start == 0)
                Debug_printf("closing all files\n");

        for (handle = 0; handle < 16; handle++)
        {
                if (server_cold_start)
                        iodesc[handle].fps.file = NULL;
                fps_close(handle);
                memset(&device[handle].parbuf, 0, sizeof(PARBUF));
        }

        if (server_cold_start)
        {
                int unit;

                for (unit = 0; unit < 16; unit++)
                {
                        device[unit].status.stat = 0;
                        device[unit].status.err = 1;
                        device[unit].status.tmot = 0;
                        device[unit].status.none = SIO_DEVICEID_PCLINK;
                }
        }
}

static void
set_status_size(uchar devno, uchar cunit, ushort size)
{
        device[cunit].status.tmot = (size & 0x00ff);
        device[cunit].status.none = (size & 0xff00) >> 8;
}

#ifdef ESP_PLATFORM
static int
validate_user_path(char *defwd, char *newpath)
{
        char *d;
        struct stat st;

        d = strstr(newpath, defwd);
        if (d == NULL || d != newpath)
                return 0;
        d = newpath + strlen(defwd);
        if (*d != '\0' && *d != '/')
                return 0;

        if (stat(newpath, &st) < 0)
                return 0;
        if (!S_ISDIR(st.st_mode))
                return 0;

        return 1;
}
#else
static int
validate_user_path(char *defwd, char *newpath)
{
        char *d, oldwd[1024], newwd[1024];

        (void)getcwd(oldwd, sizeof(oldwd));
        if (chdir(newpath) < 0)
                return 0;
        (void)getcwd(newwd, sizeof(newwd));
        (void)chdir(oldwd);

        d = strstr(newwd, defwd);

        if (d == NULL)
                return 0;
        if (d != newwd)
                return 0;

        return 1;
}

static int
abs_path(const char *path, char *abspath, int size)
{
        char oldwd[1024];
    char *cwd;

        if (getcwd(oldwd, sizeof(oldwd)) == NULL)
        return 0;
        if (chdir(path) < 0)
                return 0;
        cwd = getcwd(abspath, size);
        if (chdir(oldwd) < 0 || cwd == NULL)
        return 0;
        return 1;
}
#endif

static int
ispathsep(uchar c)
{
        return ((c == '>') || (c == '\\'));
}

static void
path_copy(uchar *dst, uchar *src)
{
        uchar a;

        while (*src)
        {
                a = *src;
                if (ispathsep(a))
                {
                        while (ispathsep(*src))
                                src++;
                        src--;
                }
                *dst = a;
                src++;
                dst++;
        }

        *dst = 0;
}

static void
path2unix(uchar *out, uchar *path)
{
        int i, y = 0;

        for (i = 0; path[i] && (i < 64); i++)
        {
                char a;

                a = upper_dir ? toupper(path[i]) : tolower(path[i]);

                if (ispathsep(a))
                        a = '/';
                else if (a == '<')
                {
                        a = '.';
                        out[y++] = '.';
                }
                out[y++] = a;
        }

        if (y && (out[y-1] != '/'))
                out[y++] = '/';

        out[y] = 0;
}

static void
create_user_path(uchar devno, uchar cunit, char *newpath)
{
        long sl, cwdo = 0;
        uchar lpath[128], upath[128];

        strcpy(newpath, device[cunit].dirname);

        /* this is user-requested new path */
        path_copy(lpath, device[cunit].parbuf.path);
        path2unix(upath, lpath);

        if (upath[0] != '/')
        {
                sl = strlen(newpath);
                if (sl && (newpath[sl-1] != '/'))
                        strcat(newpath, "/");
                if (device[cunit].cwd[0] == '/')
                        cwdo++;
                strcat(newpath, (char *)device[cunit].cwd + cwdo);
                sl = strlen(newpath);
                if (sl && (newpath[sl-1] != '/'))
                        strcat(newpath, "/");
        }
        strcat(newpath, (char *)upath);
        sl = strlen(newpath);
        // resolve ".." and "."
        strcpy(newpath, util_get_canonical_path(std::string(newpath)).c_str());
        sl = strlen(newpath);
        if (sl && (newpath[sl-1] == '/'))
                newpath[sl-1] = 0;
}

static time_t
timestamp2mtime(uchar *stamp)
{
        struct tm sdx_tm;

        memset(&sdx_tm, 0, sizeof(struct tm));

        sdx_tm.tm_sec = stamp[5];
        sdx_tm.tm_min = stamp[4];
        sdx_tm.tm_hour = stamp[3];
        sdx_tm.tm_mday = stamp[0];
        sdx_tm.tm_mon = stamp[1];
        sdx_tm.tm_year = stamp[2];

        if ((sdx_tm.tm_mday == 0) || (sdx_tm.tm_mon == 0))
                return 0;

        if (sdx_tm.tm_mon)
                sdx_tm.tm_mon--;

        if (sdx_tm.tm_year < 80)
                sdx_tm.tm_year += 2000;
        else
                sdx_tm.tm_year += 1900;

        sdx_tm.tm_year -= 1900;

        return mktime(&sdx_tm);
}

/* Command: DDEVIC+DUNIT-1 = $6f, DAUX1 = parbuf size, DAUX2 = %vvvvuuuu
 * where: v - protocol version number (0), u - unit number
 */

static void
do_pclink(uchar devno, uchar ccom, uchar caux1, uchar caux2)
{
        uchar ck, sck, fno, ob[7], handle;
        ushort cunit = caux2 & 0x0f, parsize;
        ulong faux;
        struct stat sb;
        struct dirent *dp;
        static uchar old_ccom = 0;

        if (caux2 & 0xf0)       /* protocol version number must be 0 */
        {
                pclink_ack(devno, cunit, 'N');
                return;
        }

        parsize = caux1 ? caux1 : 256;

        if (parsize > (ushort)sizeof(PARBUF))   /* and not more than fits in parbuf */
        {
                pclink_ack(devno, cunit, 'N');
                return;
        }

        if (ccom == 'P')
        {
                PARBUF pbuf;

                pclink_ack(devno, cunit, 'a');  /* ack the command (late_ack) */

                memset(&pbuf, 0, sizeof(PARBUF));
                sck = pclink_read((uchar *)&pbuf, (int)parsize);  // read data + checksum byte
                ck = calc_checksum((uchar *)&pbuf, (int)parsize); // calculate checksum from data

                device[cunit].status.stat &= ~0x02;

                pclink_ack(devno, cunit, 'A');  /* ack the received block */

                if (ck != sck)
                {
                        device[cunit].status.stat |= 0x02;
                        Debug_printf("PARBLK CRC error, Atari: $%02x, PC: $%02x\n", sck, ck);
                        device[cunit].status.err = 143;
                        goto complete;
                }

        Debug_printf("PARBLK size %d, dump: ", (int)parsize);
        {
            int dumpi;
            uchar *dumpp = (uchar *)&pbuf;

            for (dumpi = 0; dumpi < parsize; dumpi++)
            {
                Debug_printf("%02x ", dumpp[dumpi]);
            }
#ifdef ESP_PLATFORM
            Debug_printf("\n");
#endif
        }

                device[cunit].status.stat &= ~0x04;

# if 0
                /* True if Atari didn't catch the ACK above and retried the command */
                if (pbuf.fno > PCL_MAX_FNO)
                {
                        device[cunit].status.stat |= 0x04;
                        Debug_printf("PARBLK error, invalid fno $%02x\n", pbuf.fno);
                        device[cunit].status.err = 144;
                        goto complete;
                }
# endif

                if (memcmp(&pbuf, &device[cunit].parbuf, sizeof(PARBUF)) == 0)
                {
                        /* this is a retry of P-block. Most commands don't like that */
                        if ((pbuf.fno != 0x00) && (pbuf.fno != 0x01) && (pbuf.fno != 0x03) \
                                && (pbuf.fno != 0x04) && (pbuf.fno != 0x06) && \
                                        (pbuf.fno != 0x11) && (pbuf.fno != 0x13))
                        {
                                Debug_printf("PARBLK retry, ignored\n");
                                goto complete;
                        }
                }

                memcpy(&device[cunit].parbuf, &pbuf, sizeof(PARBUF));
        }

//      device[cunit].status.err = 1;
//      set_status_size(devno, cunit, 0);

        fno = device[cunit].parbuf.fno;
        faux = device[cunit].parbuf.f1 + device[cunit].parbuf.f2 * 256 + \
                device[cunit].parbuf.f3 * 65536;

        if (fno < (PCL_MAX_FNO+1))
                Debug_printf("%s (fno $%02x):\n", fun[fno], fno);

        handle = device[cunit].parbuf.handle;

        if (fno == 0x00)        /* FREAD */
        {
                uchar *mem;
                ulong blk_size = (faux & 0x0000FFFFL), buffer;

                if (ccom == 'P')
                {
                        if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                        {
                                Debug_printf("bad handle %d\n", handle);
                                device[cunit].status.err = 134; /* bad file handle */
                                goto complete;
                        }

                        if (blk_size == 0)
                        {
                                Debug_printf("bad size $0000 (0)\n");
                                device[cunit].status.err = 176;
                                set_status_size(devno, cunit, 0);
                                goto complete;
                        }

                        device[cunit].status.err = 1;
                        iodesc[handle].eof = 0;

                        buffer = iodesc[handle].fpstat.st_size - iodesc[handle].fppos;

                        if (buffer < blk_size)
                        {
                                blk_size = buffer;
                                device[cunit].parbuf.f1 = (buffer & 0x00ff);
                                device[cunit].parbuf.f2 = (buffer & 0xff00) >> 8;
                                iodesc[handle].eof = 1;
                                if (blk_size == 0)
                                        device[cunit].status.err = 136;
                        }

                        Debug_printf("size $%04lx (%ld), buffer $%04lx (%ld)\n", blk_size, blk_size, buffer, buffer);

                        set_status_size(devno, cunit, (ushort)blk_size);
                        goto complete;
                }

                if ((ccom == 'R') && (old_ccom == 'R'))
                {
                        pclink_ack(devno, cunit, 'N');
                        Debug_printf("serial communication error, abort\n");
                        fps_close(handle);
                        return;
                }

                pclink_ack(devno, cunit, 'A');  /* ack the command */

                Debug_printf("handle %d\n", handle);

                //mem = (uchar*)malloc(blk_size + 1);
                mem = (uchar*)malloc(blk_size);

                if (device[cunit].status.err == 1)
                {
                        iodesc[handle].fpread = blk_size;

                        if (iodesc[handle].fpmode & 0x10)
                        {
                                ulong rdata;
                                int eof_sig;

                                rdata = dir_read(mem, blk_size, handle, &eof_sig);

                                if (rdata != blk_size)
                                {
                                        Debug_printf("FREAD: cannot read %ld bytes from dir\n", blk_size);
                                        if (eof_sig)
                                        {
                                                iodesc[handle].fpread = rdata;
                                                device[cunit].status.err = 136;
                                        }
                                        else
                                        {
                                                iodesc[handle].fpread = 0;
                                                device[cunit].status.err = 255;
                                        }
                                }
                        }
                        else
                        {
                                if (fseek(iodesc[handle].fps.file, iodesc[handle].fppos, SEEK_SET))
                                {
                                        Debug_printf("FREAD: cannot seek to $%04lx (%ld)\n", iodesc[handle].fppos, iodesc[handle].fppos);
                                        device[cunit].status.err = 166;
                                }
                                else
                                {
                                        long fdata = fread(mem, sizeof(char), blk_size, iodesc[handle].fps.file);

                                        if ((ulong)fdata != blk_size)
                                        {
                                                Debug_printf("FREAD: cannot read %ld bytes from file\n", blk_size);
                                                if (feof(iodesc[handle].fps.file))
                                                {
                                                        iodesc[handle].fpread = fdata;
                                                        device[cunit].status.err = 136;
                                                }
                                                else
                                                {
                                                        iodesc[handle].fpread = 0;
                                                        device[cunit].status.err = 255;
                                                }
                                        }
                                }
                        }
                }

                iodesc[handle].fppos += iodesc[handle].fpread;

                if (device[cunit].status.err == 1)
                {
                        if (iodesc[handle].eof)
                                device[cunit].status.err = 136;
                        else if (iodesc[handle].fppos == iodesc[handle].fpstat.st_size)
                                device[cunit].status.err = 3;
                }

                set_status_size(devno, cunit, iodesc[handle].fpread);

                Debug_printf("FREAD: send $%04lx (%ld), status $%02x\n", blk_size, blk_size, device[cunit].status.err);

                //sck = calc_checksum(mem, blk_size);
                //mem[blk_size] = sck;
                pclink_ack(devno, cunit, 'C');
                //com_write(mem, blk_size + 1);
                pclink_write(mem, blk_size); // write data + checksum byte

                free(mem);

                goto exit;
        }

        if (fno == 0x01)        /* FWRITE */
        {
                uchar *mem;
                ulong blk_size = (faux & 0x0000FFFFL);

                if (ccom == 'P')
                {
                        if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                        {
                                Debug_printf("bad handle %d\n", handle);
                                device[cunit].status.err = 134; /* bad file handle */
                                goto complete;
                        }

                        if (blk_size == 0)
                        {
                                Debug_printf("bad size $0000 (0)\n");
                                device[cunit].status.err = 176;
                                set_status_size(devno, cunit, 0);
                                goto complete;
                        }

                        device[cunit].status.err = 1;

                        Debug_printf("size $%04lx (%ld)\n", blk_size, blk_size);
                        set_status_size(devno, cunit, (ushort)blk_size);
                        goto complete;
                }

                if ((ccom == 'R') && (old_ccom == 'R'))
                {
                        pclink_ack(devno, cunit, 'N');
                        Debug_printf("serial communication error, abort\n");
                        return;
                }

                pclink_ack(devno, cunit, 'a');  /* ack the command (late_ack) */

                Debug_printf("handle %d\n", handle);

                if ((iodesc[handle].fpmode & 0x10) == 0)
                {
                        if (fseek(iodesc[handle].fps.file, iodesc[handle].fppos, SEEK_SET))
                        {
                                Debug_printf("FWRITE: cannot seek to $%06lx (%ld)\n", iodesc[handle].fppos, iodesc[handle].fppos);
                                device[cunit].status.err = 166;
                        }
                }

                //mem = (uchar*)malloc(blk_size + 1);
                mem = (uchar*)malloc(blk_size);

                sck = pclink_read(mem, blk_size);  // read data + checksum byte
                ck = calc_checksum(mem, blk_size); // calculate checksum from data

                pclink_ack(devno, cunit, 'A');  /* ack the block of data */

                if (ck != sck)
                {
                        Debug_printf("FWRITE: block CRC mismatch (sent $%02x, calculated $%02x)\n", sck, ck);
                        device[cunit].status.err = 143;
                        free(mem);
                        goto complete;
                }

                if (device[cunit].status.err == 1)
                {
                        long rdata;

                        iodesc[handle].fpread = blk_size;

                        if (iodesc[handle].fpmode & 0x10)
                        {
                                /* ignore raw dir writes */
                        }
                        else
                        {
                                rdata = fwrite(mem, sizeof(char), blk_size, iodesc[handle].fps.file);

                                if ((ulong)rdata != blk_size)
                                {
                                        Debug_printf("FWRITE: cannot write %ld bytes to file\n", blk_size);
                                        iodesc[handle].fpread = rdata;
                                        device[cunit].status.err = 255;
                                }
                        }
                }

                iodesc[handle].fppos += iodesc[handle].fpread;

                set_status_size(devno, cunit, iodesc[handle].fpread);

                Debug_printf("FWRITE: received $%04lx (%ld), status $%02x\n", blk_size, blk_size, device[cunit].status.err);

                free(mem);
                goto complete;
        }

        if (fno == 0x02)        /* FSEEK */
        {
                ulong newpos = faux;

                if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                {
                        Debug_printf("bad handle %d\n", handle);
                        device[cunit].status.err = 134; /* bad file handle */
                        goto complete;
                }

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        Debug_printf("bad exec\n");
                        device[cunit].status.err = 176;
                        goto complete;
                }

                device[cunit].status.err = 1;

                Debug_printf("handle %d, newpos $%06lx (%ld)\n", handle, newpos, newpos);

                if (iodesc[handle].fpmode & 0x08)
                        iodesc[handle].fppos = newpos;
                else
                {
                        if ((off_t)newpos <= iodesc[handle].fpstat.st_size)
                                iodesc[handle].fppos = newpos;
                        else
                                device[cunit].status.err = 166;
                }

                goto complete;
        }

        if ((fno == 0x03) || (fno == 0x04))     /* FTELL/FLEN */
        {
                ulong outval = 0;
                //uchar out[4];
                uchar out[3];

                if (ccom == 'P')
                {
                        if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                        {
                                Debug_printf("bad handle %d\n", handle);
                                device[cunit].status.err = 134; /* bad file handle */
                                goto complete;
                        }

                        device[cunit].status.err = 1;

                        Debug_printf("device $%02x\n", cunit);
                        goto complete;
                }

                pclink_ack(devno, cunit, 'A');  /* ack the command */

                if (fno == 0x03)
                        outval = iodesc[handle].fppos;
                else
                        outval = iodesc[handle].fpstat.st_size;

                Debug_printf("handle %d, send $%06lx (%ld)\n", handle, outval, outval);

                out[0] = (uchar)(outval & 0x000000ffL);
                out[1] = (uchar)((outval & 0x0000ff00L) >> 8);
                out[2] = (uchar)((outval & 0x00ff0000L) >> 16);

                //out[3] = calc_checksum(out, sizeof(out)-1);
                pclink_ack(devno, cunit, 'C');
                //com_write(out, sizeof(out));
                pclink_write(out, sizeof(out)); // write data + checksum byte

                goto exit;
        }

        if (fno == 0x06)        /* FNEXT */
        {
                if (ccom == 'P')
                {
                        device[cunit].status.err = 1;

                        Debug_printf("device $%02x\n", cunit);
                        goto complete;
                }

                if ((ccom == 'R') && (old_ccom == 'R'))
                {
                        pclink_ack(devno, cunit, 'N');
                        Debug_printf("serial communication error, abort\n");
                        return;
                }

                pclink_ack(devno, cunit, 'A');  /* ack the command */

                memset(pcl_dbf.dirbuf, 0, sizeof(pcl_dbf.dirbuf));

                if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                {
                        Debug_printf("bad handle %d\n", handle);
                        device[cunit].status.err = 134; /* bad file handle */
                }
                else
                {
                        int eof_flg, match = 0;

                        Debug_printf("handle %d\n", handle);

                        do
                        {
                                struct stat ts;

                                memset(&ts, 0, sizeof(ts));
                                memset(pcl_dbf.dirbuf, 0, sizeof(pcl_dbf.dirbuf));
                                iodesc[handle].fppos += dir_read(pcl_dbf.dirbuf, sizeof(pcl_dbf.dirbuf), handle, &eof_flg);

                                if (!eof_flg)
                                {
                                        /* fake stat to satisfy match_dos_names() */
                                        if ((pcl_dbf.dirbuf[0] & 0x01) == 0)
                                                ts.st_mode |= (S_IWUSR|S_IWGRP);
                                        if (pcl_dbf.dirbuf[0] & 0x20)
                                                ts.st_mode |= S_IFDIR;
                                        else
                                                ts.st_mode |= S_IFREG;

                                        match = !match_dos_names((char *)pcl_dbf.dirbuf+6, iodesc[handle].fpname, iodesc[handle].fatr1, &ts);
                                }

                        } while (!eof_flg && !match);

                        if (eof_flg)
                        {
                                Debug_printf("FNEXT: EOF\n");
                                device[cunit].status.err = 136;
                        }
                        else if (iodesc[handle].fppos == iodesc[handle].fpstat.st_size)
                                device[cunit].status.err = 3;
                }

                /* avoid the 4th execution stage */
                pcl_dbf.handle = device[cunit].status.err;

                Debug_printf("FNEXT: status %d, send $%02x $%02x%02x $%02x%02x%02x %c%c%c%c%c%c%c%c%c%c%c %02d-%02d-%02d %02d:%02d:%02d\n", \
                        pcl_dbf.handle,
                        pcl_dbf.dirbuf[0],
                        pcl_dbf.dirbuf[2], pcl_dbf.dirbuf[1],
                        pcl_dbf.dirbuf[5], pcl_dbf.dirbuf[4], pcl_dbf.dirbuf[3],
                        pcl_dbf.dirbuf[6], pcl_dbf.dirbuf[7], pcl_dbf.dirbuf[8], pcl_dbf.dirbuf[9],
                        pcl_dbf.dirbuf[10], pcl_dbf.dirbuf[11], pcl_dbf.dirbuf[12], pcl_dbf.dirbuf[13],
                        pcl_dbf.dirbuf[14], pcl_dbf.dirbuf[15], pcl_dbf.dirbuf[16],
                        pcl_dbf.dirbuf[17], pcl_dbf.dirbuf[18], pcl_dbf.dirbuf[19],
                        pcl_dbf.dirbuf[20], pcl_dbf.dirbuf[21], pcl_dbf.dirbuf[22]);

                //sck = calc_checksum((uchar *)&pcl_dbf, sizeof(pcl_dbf));
                pclink_ack(devno, cunit, 'C');
                //com_write((uchar *)&pcl_dbf, sizeof(pcl_dbf));
                //com_write(&sck, 1);
                pclink_write((uchar *)&pcl_dbf, sizeof(pcl_dbf)); // write data + checksum byte

                goto exit;
        }

        if (fno == 0x07)        /* FCLOSE */
        {
                uchar fpmode;
                time_t mtime;
                char pathname[1024];

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                {
                        Debug_printf("bad handle %d\n", handle);
                        device[cunit].status.err = 134; /* bad file handle */
                        goto complete;
                }

                Debug_printf("handle %d\n", handle);

                device[cunit].status.err = 1;

                fpmode = iodesc[handle].fpmode;
                mtime = iodesc[handle].fpstat.st_mtime;
# if 0
                Debug_printf("FCLOSE: mtime $%08x\n", mtime);
# endif
                strcpy(pathname, iodesc[handle].pathname);

                fps_close(handle);      /* this clears out iodesc[handle] */

                if (mtime && (fpmode & 0x08))
                {
            utimbuf ub;
            ub.actime = mtime;
            ub.modtime = mtime;

# if 0
                        Debug_printf("FCLOSE: setting timestamp in '%s'\n", pathname);
# endif

            utime(pathname,&ub);
                }
                goto complete;
        }

        if (fno == 0x08)        /* INIT */
        {
                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("INIT: bad exec\n");
                        goto complete;
                }

                do_pclink_init(0);

                device[cunit].parbuf.handle = 0xff;
                device[cunit].status.none = SIO_DEVICEID_PCLINK;
                device[cunit].status.err = 1;
                goto complete;
        }

        if ((fno == 0x09) || (fno == 0x0a))     /* FOPEN/FFIRST */
        {
                if (ccom == 'P')
                {
                        Debug_printf("mode: $%02x, atr1: $%02x, atr2: $%02x, path: '%s', name: '%s'\n", \
                                device[cunit].parbuf.fmode, device[cunit].parbuf.fatr1, \
                                device[cunit].parbuf.fatr2, device[cunit].parbuf.path, \
                                device[cunit].parbuf.name);
# if 0
                        Debug_printf("date: %02d-%02d-%02d time: %02d:%02d:%02d\n", \
                                device[cunit].parbuf.f1, device[cunit].parbuf.f2, \
                                device[cunit].parbuf.f3, device[cunit].parbuf.f4, \
                                device[cunit].parbuf.f5, device[cunit].parbuf.f6);
# endif

                        device[cunit].status.err = 1;

                        if (fno == 0x0a)
                                device[cunit].parbuf.fmode |= 0x10;
                        goto complete;
                }
                else    /* ccom not 'P', execution stage */
                {
                        DIR *dh;
                        uchar i;
                        long sl;
                        struct stat tempstat;
                        char newpath[1024], raw_name[12];

                        if ((ccom == 'R') && (old_ccom == 'R'))
                        {
                                pclink_ack(devno, cunit, 'N');
                                Debug_printf("serial communication error, abort\n");
                                fps_close(handle);
                                return;
                        }

                        pclink_ack(devno, cunit, 'A');  /* ack the command */

                        memset(raw_name, 0, sizeof(raw_name));
                        memcpy(raw_name, device[cunit].parbuf.name, 8+3);

                        if (((device[cunit].parbuf.fmode & 0x0c) == 0) || \
                                ((device[cunit].parbuf.fmode & 0x18) == 0x18))
                        {
                                Debug_printf("unsupported fmode ($%02x)\n", device[cunit].parbuf.fmode);
                                device[cunit].status.err = 146;
                                goto complete_fopen;
                        }

                        create_user_path(devno, cunit, newpath);

                        if (!validate_user_path(device[cunit].dirname, newpath))
                        {
                                Debug_printf("invalid path '%s'\n", newpath);
                                device[cunit].status.err = 150;
                                goto complete_fopen;
                        }

                        Debug_printf("local path '%s'\n", newpath);

                        for (i = 0; i < 16; i++)
                        {
# if 0
                                Debug_printf("FOPEN: find handle: %d is $%08lx\n", i, (ulong)iodesc[i].fps.file);
# endif
                                if (iodesc[i].fps.file == NULL)
# if 1
                                        break;
# else
                                {
                                        Debug_printf("FOPEN: find handle: found %d\n", i);
                                        break;
                                }
# endif
                        }
                        if (i > 15)
                        {
                                Debug_printf("FOPEN: too many channels open\n");
                                device[cunit].status.err = 161;
                                goto complete_fopen;
                        }

                        if (stat(newpath, &tempstat) < 0)
                        {
                                Debug_printf("FOPEN: cannot stat '%s'\n", newpath);
                                device[cunit].status.err = 150;
                                goto complete_fopen;
                        }

                        dh = opendir(newpath);

                        if (device[cunit].parbuf.fmode & 0x10)
                        {
                                iodesc[i].fps.dir = dh;
                                memcpy(&sb, &tempstat, sizeof(sb));
                        }
                        else
                        {
                                while ((dp = readdir(dh)) != NULL)
                                {
                                        if (check_dos_name(newpath, dp, &sb))
                                                continue;

                                        /* convert 8+3 to NNNNNNNNXXX */
                                        ugefina(dp->d_name, raw_name);

                                        /* match */
                                        if (match_dos_names(raw_name, \
                                                (char *)device[cunit].parbuf.name, \
                                                        device[cunit].parbuf.fatr1, &sb) == 0)
                                                break;
                                }

                                sl = strlen(newpath);
                                if (sl && (newpath[sl-1] != '/'))
                                        strcat(newpath, "/");

                                if (dp)
                                {
                                        strcat(newpath, dp->d_name);
                                        ugefina(dp->d_name, raw_name);
                                        if ((device[cunit].parbuf.fmode & 0x0c) == 0x08)
                                                sb.st_mtime = timestamp2mtime(&device[cunit].parbuf.f1);
                                }
                                else
                                {
                                        if ((device[cunit].parbuf.fmode & 0x0c) == 0x04)
                                        {
                                                Debug_printf("FOPEN: file not found\n");
                                                device[cunit].status.err = 170;
                                                closedir(dh);
                                                dp = NULL;
                                                goto complete_fopen;
                                        }
                                        else
                                        {
                                                char name83[12];

                                                Debug_printf("FOPEN: creating file\n");

                                                uexpand(device[cunit].parbuf.name, name83);

                                                if (validate_dos_name(name83))
                                                {
                                                        Debug_printf("FOPEN: bad filename '%s'\n", name83);
                                                        device[cunit].status.err = 165; /* bad filename */
                                                        goto complete_fopen;
                                                }

                                                strcat(newpath, name83);
                                                ugefina(name83, raw_name);

                                                memset(&sb, 0, sizeof(struct stat));
                                                sb.st_mode = S_IFREG|S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP;

                                                sb.st_mtime = timestamp2mtime(&device[cunit].parbuf.f1);
                                        }
                                }

                                Debug_printf("FOPEN: full local path '%s'\n", newpath);

                                if (stat(newpath, &tempstat) < 0)
                                {
                                        if ((device[cunit].parbuf.fmode & 0x0c) == 0x04)
                                        {
                                                Debug_printf("FOPEN: cannot stat '%s'\n", newpath);
                                                device[cunit].status.err = 170;
                                                goto complete_fopen;
                                        }
                                }
                                else
                                {
                                        if (device[cunit].parbuf.fmode & 0x08)
                                        {
                                                if ((tempstat.st_mode & (S_IWUSR|S_IWGRP)) == 0)
                                                {
                                                        Debug_printf("FOPEN: '%s' is read-only\n", newpath);
                                                        device[cunit].status.err = 151;
                                                        goto complete_fopen;
                                                }
                                        }
# if 0
                                        if ((device[cunit].parbuf.fmode & 0x0d) == 0x08)
                                        {
                                                if (!S_ISDIR(tempstat.st_mode))
                                                {
                                                        Debug_printf("FOPEN: delete '%s'\n", newpath);
                                                        if (unlink(newpath))
                                                        {
                                                                Debug_printf("FOPEN: cannot delete '%s'\n", newpath);
                                                                device[cunit].status.err = 255;
                                                        }
                                                }
                                        }
# endif
                                }

                                if ((device[cunit].parbuf.fmode & 0x0d) == 0x04)
                                        iodesc[i].fps.file = fopen(newpath, "r");
                                else if ((device[cunit].parbuf.fmode & 0x0d) == 0x08)
                                {
                                        iodesc[i].fps.file = fopen(newpath, "w");
                                        if (iodesc[i].fps.file)
                                                sb.st_size = 0;
                                }
                                else if ((device[cunit].parbuf.fmode & 0x0d) == 0x09)
                                {
                                        iodesc[i].fps.file = fopen(newpath, "r+");
                                        if (iodesc[i].fps.file)
                                                fseek(iodesc[i].fps.file, sb.st_size, SEEK_SET);
                                }
                                else if ((device[cunit].parbuf.fmode & 0x0d) == 0x0c)
                                        iodesc[i].fps.file = fopen(newpath, "r+");

                                closedir(dh);
                                dp = NULL;
                        }

                        if (iodesc[i].fps.file == NULL)
                        {
                                Debug_printf("FOPEN: cannot open '%s', %s (%d)\n", newpath, strerror(errno), errno);
                                if (device[cunit].parbuf.fmode & 0x04)
                                        device[cunit].status.err = 170;
                                else
                                        device[cunit].status.err = 151;
                                goto complete_fopen;
                        }

# if 0
                        Debug_printf("FOPEN: handle %d is $%08lx\n", i, (ulong)iodesc[i].fps.file);
# endif
                        handle = device[cunit].parbuf.handle = i;

                        iodesc[handle].devno = devno;
                        iodesc[handle].cunit = cunit;
                        iodesc[handle].fpmode = device[cunit].parbuf.fmode;
                        iodesc[handle].fatr1 = device[cunit].parbuf.fatr1;
                        iodesc[handle].fatr2 = device[cunit].parbuf.fatr2;
                        iodesc[handle].t1 = device[cunit].parbuf.f1;
                        iodesc[handle].t2 = device[cunit].parbuf.f2;
                        iodesc[handle].t3 = device[cunit].parbuf.f3;
                        iodesc[handle].d1 = device[cunit].parbuf.f4;
                        iodesc[handle].d2 = device[cunit].parbuf.f5;
                        iodesc[handle].d3 = device[cunit].parbuf.f6;
                        iodesc[handle].fppos = 0L;
                        strcpy(iodesc[handle].pathname, newpath);
                        memcpy((void *)&iodesc[handle].fpstat, (void *)&sb, sizeof(struct stat));
                        if (iodesc[handle].fpmode & 0x10)
                                memcpy(iodesc[handle].fpname, device[cunit].parbuf.name, sizeof(iodesc[i].fpname));
                        else
                                memcpy(iodesc[handle].fpname, raw_name, sizeof(iodesc[handle].fpname));

                        iodesc[handle].fpstat.st_size = get_file_len(handle);

                        if ((iodesc[handle].fpmode & 0x1d) == 0x09)
                                iodesc[handle].fppos = iodesc[handle].fpstat.st_size;

                        memset(pcl_dbf.dirbuf, 0, sizeof(pcl_dbf.dirbuf));

                        if ((handle > 15) || (iodesc[handle].fps.file == NULL))
                        {
                                Debug_printf("FOPEN: bad handle %d\n", handle);
                                device[cunit].status.err = 134; /* bad file handle */
                                pcl_dbf.handle = 134;
                        }
                        else
                        {
                                pcl_dbf.handle = handle;

                                unix_time_2_sdx(&iodesc[handle].fpstat.st_mtime, ob);

# if 0
                                Debug_printf("FOPEN: time %02d-%02d-%02d %02d:%02d.%02d\n", ob[0], ob[1], ob[2], ob[3], ob[4], ob[5]);
# endif

                                Debug_printf("FOPEN: %s handle %d\n", (iodesc[handle].fpmode & 0x08) ? "write" : "read", handle);

                                memset(pcl_dbf.dirbuf, 0, sizeof(pcl_dbf.dirbuf));

                                if (iodesc[handle].fpmode & 0x10)
                                {
                                        int eof_sig;

                                        iodesc[handle].dir_cache = cache_dir(handle);
                                        iodesc[handle].fppos += dir_read(pcl_dbf.dirbuf, sizeof(pcl_dbf.dirbuf), handle, &eof_sig);

                                        if (eof_sig)
                                        {
                                                Debug_printf("FOPEN: dir EOF?\n");
                                                device[cunit].status.err = 136;
                                        }
                                        else if (iodesc[handle].fppos == iodesc[handle].fpstat.st_size)
                                                        device[cunit].status.err = 3;
                                }
                                else
                                {
                                        int x;
                                        ulong dlen = iodesc[handle].fpstat.st_size;

                                        memset(pcl_dbf.dirbuf+6, 0x20, 11);
                                        pcl_dbf.dirbuf[3] = (uchar)(dlen & 0x000000ffL);
                                        pcl_dbf.dirbuf[4] = (uchar)((dlen & 0x0000ff00L) >> 8);
                                        pcl_dbf.dirbuf[5] = (uchar)((dlen & 0x00ff0000L) >> 16);
                                        memcpy(pcl_dbf.dirbuf+17, ob, 6);

                                        pcl_dbf.dirbuf[0] = 0x08;

                                        if ((iodesc[handle].fpstat.st_mode & (S_IWUSR|S_IWGRP)) == 0)
                                                pcl_dbf.dirbuf[0] |= 0x01;      /* protected */
                                        if (S_ISDIR(iodesc[handle].fpstat.st_mode))
                                                pcl_dbf.dirbuf[0] |= 0x20;      /* directory */

                                        x = 0;
                                        while (iodesc[handle].fpname[x] && (x < 11))
                                        {
                                                pcl_dbf.dirbuf[6+x] = iodesc[handle].fpname[x];
                                                x++;
                                        }
                                }

                                Debug_printf("FOPEN: send $%02x $%02x%02x $%02x%02x%02x %c%c%c%c%c%c%c%c%c%c%c %02d-%02d-%02d %02d:%02d:%02d\n", \
                                pcl_dbf.dirbuf[0],
                                pcl_dbf.dirbuf[2], pcl_dbf.dirbuf[1],
                                pcl_dbf.dirbuf[5], pcl_dbf.dirbuf[4], pcl_dbf.dirbuf[3],
                                pcl_dbf.dirbuf[6], pcl_dbf.dirbuf[7], pcl_dbf.dirbuf[8], pcl_dbf.dirbuf[9],
                                pcl_dbf.dirbuf[10], pcl_dbf.dirbuf[11], pcl_dbf.dirbuf[12], pcl_dbf.dirbuf[13],
                                pcl_dbf.dirbuf[14], pcl_dbf.dirbuf[15], pcl_dbf.dirbuf[16],
                                pcl_dbf.dirbuf[17], pcl_dbf.dirbuf[18], pcl_dbf.dirbuf[19],
                                pcl_dbf.dirbuf[20], pcl_dbf.dirbuf[21], pcl_dbf.dirbuf[22]);
                        }

complete_fopen:
                        //sck = calc_checksum((uchar *)&pcl_dbf, sizeof(pcl_dbf));
                        pclink_ack(devno, cunit, 'C');
                        //com_write((uchar *)&pcl_dbf, sizeof(pcl_dbf));
                        //com_write(&sck, 1);
                        pclink_write((uchar *)&pcl_dbf, sizeof(pcl_dbf)); // write data + checksum byte

                        goto exit;
                }
        }

        if (fno == 0x0b)        /* RENAME/RENDIR */
        {
                char newpath[1024];
                DIR *renamedir;
                ulong fcnt = 0;

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                create_user_path(devno, cunit, newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                renamedir = opendir(newpath);

                if (renamedir == NULL)
                {
                        Debug_printf("cannot open dir '%s'\n", newpath);
                        device[cunit].status.err = 255;
                        goto complete;
                }

                Debug_printf("local path '%s', fatr1 $%02x\n", newpath, \
                        device[cunit].parbuf.fatr1 | RA_NO_PROTECT);

                device[cunit].status.err = 1;

                while ((dp = readdir(renamedir)) != NULL)
                {
                        char raw_name[12];

                        if (check_dos_name(newpath, dp, &sb))
                                continue;

                        /* convert 8+3 to NNNNNNNNXXX */
                        ugefina(dp->d_name, raw_name);

                        /* match */
                        if (match_dos_names(raw_name, (char *)device[cunit].parbuf.name, \
                                device[cunit].parbuf.fatr1 | RA_NO_PROTECT, &sb) == 0)
                        {
                                char xpath[1024], xpath2[1024], newname[16];
                                uchar names[12];
                                struct stat dummy;
                                ushort x;

                                fcnt++;

                                strcpy(xpath, newpath);
                                strcat(xpath, "/");
                                strcat(xpath, dp->d_name);

                                memcpy(names, device[cunit].parbuf.names, 12);

                                for (x = 0; x < 12; x++)
                                {
                                        if (names[x] == '?')
                                                names[x] = raw_name[x];
                                }

                                uexpand(names, newname);

                                strcpy(xpath2, newpath);
                                strcat(xpath2, "/");
                                strcat(xpath2, newname);

                                Debug_printf("RENAME: renaming '%s' -> '%s'\n", dp->d_name, newname);

                                if (stat(xpath2, &dummy) == 0)
                                {
                                        Debug_printf("RENAME: '%s' already exists\n", xpath2);
                                        device[cunit].status.err = 151;
                                        break;
                                }

                                if (rename(xpath, xpath2))
                                {
                                        Debug_printf("RENAME: %s\n", strerror(errno));
                                        device[cunit].status.err = 255;
                                }
                        }
                }

                closedir(renamedir);

                if ((fcnt == 0) && (device[cunit].status.err == 1))
                        device[cunit].status.err = 170;
                goto complete;
        }

        if (fno == 0x0c)        /* REMOVE */
        {
                char newpath[1024];
                DIR *deldir;
                ulong delcnt = 0;

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                create_user_path(devno, cunit, newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                Debug_printf("local path '%s'\n", newpath);

                deldir = opendir(newpath);

                if (deldir == NULL)
                {
                        Debug_printf("cannot open dir '%s'\n", newpath);
                        device[cunit].status.err = 255;
                        goto complete;
                }

                device[cunit].status.err = 1;

                while ((dp = readdir(deldir)) != NULL)
                {
                        char raw_name[12];

                        if (check_dos_name(newpath, dp, &sb))
                                continue;

                        /* convert 8+3 to NNNNNNNNXXX */
                        ugefina(dp->d_name, raw_name);

                        /* match */
                        if (match_dos_names(raw_name, (char *)device[cunit].parbuf.name, \
                                RA_NO_PROTECT | RA_NO_SUBDIR | RA_NO_HIDDEN, &sb) == 0)
                        {
                                char xpath[1024];

                                strcpy(xpath, newpath);
                                strcat(xpath, "/");
                                strcat(xpath, dp->d_name);

                                if (!S_ISDIR(sb.st_mode))
                                {
                                        Debug_printf("REMOVE: delete '%s'\n", xpath);
                                        if (unlink(xpath))
                                        {
                                                Debug_printf("REMOVE: cannot delete '%s'\n", xpath);
                                                device[cunit].status.err = 255;
                                        }
                                        delcnt++;
                                }
                        }
                }
                closedir(deldir);
                if (delcnt == 0)
                        device[cunit].status.err = 170;
                goto complete;
        }

        if (fno == 0x0d)        /* CHMOD */
        {
                char newpath[1024];
                DIR *chmdir;
                ulong fcnt = 0;
                uchar fatr2 = device[cunit].parbuf.fatr2;

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                if (fatr2 & (SA_SUBDIR | SA_UNSUBDIR))
                {
                        Debug_printf("illegal fatr2 $%02x\n", fatr2);
                        device[cunit].status.err = 146;
                        goto complete;
                }

                create_user_path(devno, cunit, newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                Debug_printf("local path '%s', fatr1 $%02x fatr2 $%02x\n", newpath, \
                                device[cunit].parbuf.fatr1, fatr2);

                chmdir = opendir(newpath);

                if (chmdir == NULL)
                {
                        Debug_printf("CHMOD: cannot open dir '%s'\n", newpath);
                        device[cunit].status.err = 255;
                        goto complete;
                }


                device[cunit].status.err = 1;

                while ((dp = readdir(chmdir)) != NULL)
                {
                        char raw_name[12];

                        if (check_dos_name(newpath, dp, &sb))
                                continue;

                        /* convert 8+3 to NNNNNNNNXXX */
                        ugefina(dp->d_name, raw_name);

                        /* match */
                        if (match_dos_names(raw_name, (char *)device[cunit].parbuf.name, \
                                device[cunit].parbuf.fatr1, &sb) == 0)
                        {
                                char xpath[1024];
                                mode_t newmode = sb.st_mode;

                                strcpy(xpath, newpath);
                                strcat(xpath, "/");
                                strcat(xpath, dp->d_name);
                                Debug_printf("CHMOD: change atrs in '%s'\n", xpath);

                                /* On Unix, ignore Hidden and Archive bits */
                                if (fatr2 & SA_UNPROTECT)
                                        newmode |= S_IWUSR;
                                if (fatr2 & SA_PROTECT)
                                        newmode &= ~S_IWUSR;
                                // TODO - chmod is not available on platformio
#ifndef ESP_PLATFORM
                                if (chmod(xpath, newmode))
                                {
                                        Debug_printf("CHMOD: failed on '%s'\n", xpath);
                                        device[cunit].status.err |= 255;
                                }
#endif
                                fcnt++;
                        }
                }
                closedir(chmdir);
                if (fcnt == 0)
                        device[cunit].status.err = 170;
                goto complete;
        }

        if (fno == 0x0e)        /* MKDIR - warning, fatr2 is bogus */
        {
                char newpath[1024], fname[12];
                uchar dt[6];
                struct stat dummy;

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                create_user_path(devno, cunit, newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                uexpand(device[cunit].parbuf.name, fname);

                if (validate_dos_name(fname))
                {
                        Debug_printf("bad dir name '%s'\n", fname);
                        device[cunit].status.err = 165;
                        goto complete;
                }

                strcat(newpath, "/");
                strcat(newpath, fname);

                memcpy(dt, &device[cunit].parbuf.f1, sizeof(dt));

                Debug_printf("making dir '%s', time %2d-%02d-%02d %2d:%02d:%02d\n", newpath, \
                        dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);

                if (stat(newpath, &dummy) == 0)
                {
                        Debug_printf("MKDIR: '%s' already exists\n", newpath);
                        device[cunit].status.err = 151;
                        goto complete;
                }

                if (mkdir(newpath, S_IRWXU|S_IRWXG|S_IRWXO))
                {
                        Debug_printf("MKDIR: cannot make dir '%s'\n", newpath);
                        device[cunit].status.err = 255;
                }
                else
                {
                        time_t mtime = timestamp2mtime(dt);

                        device[cunit].status.err = 1;

                        if (mtime)
                        {
                utimbuf ub;
                ub.actime = mtime;
                ub.modtime = mtime;

# if 0
                                Debug_printf("MKDIR: setting timestamp in '%s'\n", newpath);
# endif

                utime(newpath, &ub);
                        }
                }
                goto complete;
        }

        if (fno == 0x0f)        /* RMDIR */
        {
                char newpath[1024], fname[12];

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                create_user_path(devno, cunit, newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                uexpand(device[cunit].parbuf.name, fname);

                if (validate_dos_name(fname))
                {
                        Debug_printf("bad dir name '%s'\n", fname);
                        device[cunit].status.err = 165;
                        goto complete;
                }

                strcat(newpath, "/");
                strcat(newpath, fname);

                if (stat(newpath, &sb) < 0)
                {
                        Debug_printf("cannot stat '%s'\n", newpath);
                        device[cunit].status.err = 170;
                        goto complete;
                }

                /*if (sb.st_uid != our_uid)
                {
                        Debug_printf("'%s' wrong uid\n", newpath);
                        device[cunit].status.err = 170;
                        goto complete;
                }*/

                if (!S_ISDIR(sb.st_mode))
                {
                        Debug_printf("'%s' is not a directory\n", newpath);
                        device[cunit].status.err = 170;
                        goto complete;
                }

                if ((sb.st_mode & (S_IWUSR|S_IWGRP)) == 0)
                {
                        Debug_printf("dir '%s' is write-protected\n", newpath);
                        device[cunit].status.err = 170;
                        goto complete;
                }

                Debug_printf("delete dir '%s'\n", newpath);

                device[cunit].status.err = 1;

                if (rmdir(newpath))
                {
                        Debug_printf("RMDIR: cannot del '%s', %s (%d)\n", newpath, strerror(errno), errno);
                        if (errno == ENOTEMPTY)
                                device[cunit].status.err = 167;
                        else
                                device[cunit].status.err = 255;
                }
                goto complete;
        }

        if (fno == 0x10)        /* CHDIR */
        {
                ulong i;
                char newpath[1024];

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                Debug_printf("req. path '%s'\n", device[cunit].parbuf.path);

                create_user_path(devno, cunit, newpath);
                Debug_printf("newpath '%s'\n", newpath);

                if (!validate_user_path(device[cunit].dirname, newpath))
                {
                        Debug_printf("invalid path '%s'\n", newpath);
                        device[cunit].status.err = 150;
                        goto complete;
                }

                // chdir and getcwd not implemented on platformio (as of July 2023)
                // https://github.com/espressif/esp-idf/issues/8540

                /* validate_user_path() guarantees that .dirname is part of newwd */
                i = strlen(device[cunit].dirname);
                strcpy((char *)device[cunit].cwd, newpath + i);
                Debug_printf("new current dir '%s'\n", (char *)device[cunit].cwd);

                device[cunit].status.err = 1;

                goto complete;
        }

        if (fno == 0x11)        /* GETCWD */
        {
                int i;
                uchar tempcwd[65];

                device[cunit].status.err = 1;

                if (ccom == 'P')
                {
                        Debug_printf("device $%02x\n", cunit);
                        goto complete;
                }

                pclink_ack(devno, cunit, 'A');  /* ack the command */

                tempcwd[0] = 0;

                for (i = 0; device[cunit].cwd[i] && (i < 64); i++)
                {
                        uchar a;

                        a = toupper(device[cunit].cwd[i]);
                        if (a == '/')
                                a = '>';
                        tempcwd[i] = a;
                }

                tempcwd[i] = 0;

                Debug_printf("send '%s'\n", tempcwd);

                //sck = calc_checksum(tempcwd, sizeof(tempcwd)-1);
                pclink_ack(devno, cunit, 'C');
                //com_write(tempcwd, sizeof(tempcwd)-1);
                //com_write(&sck, sizeof(sck));
                pclink_write(tempcwd, sizeof(tempcwd)-1); // write data + checksum byte

                goto exit;
        }

        if (fno == 0x13)        /* DFREE */
        {
                FILE *vf;
                int x;
                uchar c = 0, volname[8];
                char lpath[1024];
                static uchar dfree[64] =
                {
                        0x21,           /* data format version */
                        0x00, 0x00,     /* main directory ptr */
                        0xff, 0xff,     /* total sectors */
                        0xff, 0xff,     /* free sectors */
                        0x00,           /* bitmap length */
                        0x00, 0x00,     /* bitmap begin */
                        0x00, 0x00,     /* filef */
                        0x00, 0x00,     /* dirf */
                        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, /* volume name */
                        0x00,           /* number of tracks */
                        0x01,           /* bytes per sector, encoded */
                        0x80,           /* version number */
                        0x00, 0x02,     /* real bps */
                        0x00, 0x00,     /* fmapen */
                        0x01,           /* sectors per cluster */
                        0x00, 0x00,     /* nr seq and rnd */
                        0x00, 0x00,     /* bootp */
                        0x00,           /* lock */

                        0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,
                        0,0,0,0,0,
                        //0             /* CRC */
                };

                device[cunit].status.err = 1;

                if (ccom == 'P')
                {
                        Debug_printf("device $%02x\n", cunit);
                        goto complete;
                }

                pclink_ack(devno, cunit, 'A');  /* ack the command */

                memset(dfree + 0x0e, 0x020, 8);

                strcpy(lpath, (char *)device[cunit].dirname);
                strcat(lpath, "/");
                strcat(lpath, DEVICE_LABEL);

                Debug_printf("reading '%s'\n", lpath);

                vf = fopen(lpath, "r");

                if (vf)
                {
                        int r;
                        uchar a;

                        r = fread(volname, sizeof (uchar), 8, vf);

                        fclose(vf);

                        for (x = 0; x < r; x++)
                        {
                                a = volname[x];
                                if (a == 0x9b)
                                        break;
                                dfree[14+x] = a;
                        }
                }
                else
                {
                        uchar a;
                        int o;
                        for (o = strlen(device[cunit].dirname); o > 0; o--)
                        {
                                a = device[cunit].dirname[o-1];
                                if (a == '/')
                                        break;
                        }
                        for (x = 0; x < 8; x++)
                        {
                                a = device[cunit].dirname[o+x];
                                if (a == '\0')
                                        break;
                                dfree[14+x] = a;
                        }
                }

                x = 0;
                while (x < 8)
                {
                        c |= dfree[14+x];
                        x++;
                }

                if (c == 0x20)
                {
                        memcpy(dfree + 14, "PCLink  ", 8);
                        dfree[21] = cunit + 0x40;
                }

                Debug_printf("DFREE: send info (%d bytes)\n", (int)sizeof(dfree)-1);

                //dfree[64] = calc_checksum(dfree, sizeof(dfree)-1);
                pclink_ack(devno, cunit, 'C');
                //com_write(dfree, sizeof(dfree));
                pclink_write(dfree, sizeof(dfree)); // write data + checksum byte

                goto exit;
        }

        if (fno == 0x14)        /* CHVOL */
        {
                FILE *vf;
                ulong nl;
                char lpath[1024];

                device[cunit].status.err = 1;

                if (ccom == 'R')
                {
                        pclink_ack(devno, cunit, 'A');  /* ack the command */
                        device[cunit].status.err = 176;
                        Debug_printf("bad exec\n");
                        goto complete;
                }

                nl = strlen((char *)device[cunit].parbuf.name);

                if (nl == 0)
                {
                        Debug_printf("invalid name\n");
                        device[cunit].status.err = 156;
                        goto complete;
                }

                strcpy(lpath, device[cunit].dirname);
                strcat(lpath, "/");
                strcat(lpath, DEVICE_LABEL);

                Debug_printf("writing '%s'\n", lpath);

                vf = fopen(lpath, "w");

                if (vf)
                {
                        int x;
                        uchar a;

                        for (x = 0; x < 8; x++)
                        {
                                a = device[cunit].parbuf.name[x];
                                if (!a || (a == 0x9b))
                                        a = 0x20;
                                (void)fwrite(&a, sizeof(uchar), 1, vf);
                        }
                        fclose(vf);
                }
                else
                {
                        Debug_printf("CHVOL: %s\n", strerror(errno));
                        device[cunit].status.err = 255;
                }
                goto complete;
        }

        Debug_printf("fno $%02x: not implemented\n", fno);
        device[cunit].status.err = 146;

complete:
        pclink_ack(devno, cunit, 'C');

exit:
        old_ccom = ccom;

        return;
}

/*
 * PCLink specific version of bus_to_peripheral(), no ACK/NAK is send here
 */
static uint8_t
pclink_read(uint8_t *buf, int len)
{
    // Retrieve data frame from computer
    Debug_printf("<-SIO read (PCLINK) %hu bytes\n", len);

#ifndef ESP_PLATFORM
    if (SYSTEM_BUS.isBoIP())
    {
        SYSTEM_BUS.netsio_write_size(len); // set hint for NetSIO
    }
#endif

    __BEGIN_IGNORE_UNUSEDVARS
    size_t l = SYSTEM_BUS.read(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = SYSTEM_BUS.read();

#ifdef VERBOSE_SIO
    Debug_printf("RECV <%u> BYTES, checksum: %hu\n\t", (unsigned int)l, ck_rcv);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    return ck_rcv;
}

/*
 * PCLink specific version of bus_to_computer(), no C/E is send here
 */
static void
pclink_write(uint8_t *buf, int len)
{
    // Write data frame to computer
    Debug_printf("->SIO write (PCLINK) %hu bytes\n", len);
#ifdef VERBOSE_SIO
    Debug_printf("SEND <%u> BYTES\n\t", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    // Write data frame
    SYSTEM_BUS.write(buf, len);
    // Write checksum
    SYSTEM_BUS.write(sio_checksum(buf, len));

    SYSTEM_BUS.flushOutput();
}


static void
get_device_status(ushort devno, ushort d, uchar *st)
{
        //setup_status(d);
        st[0] = device[d].status.stat;
        st[1] = device[d].status.err;
        st[2] = device[d].status.tmot;
        st[3] = device[d].status.none;
}

static void
pclink_ack(ushort devno, ushort d, uchar what)
{
    fnSystem.delay_microseconds(DELAY_T4);

    // call one of sio_ack/sio_nak/sio_complete/sio_error
    pcLink.send_ack_byte(what);

        device[d].status.stat &= ~(0x01|0x04);

        switch (what)
        {
    case 'E':
        device[d].status.stat |= 0x04;  /* last operation failed */
        break;
    case 'N':
        device[d].status.stat |= 0x01;
        break;
        }

    fnSystem.delay_microseconds(DELAY_T4);

# ifdef SIOTRACE
        if (log_flag)
                Debug_printf("<- ACK '%c'\n", what);
# endif
}

sioPCLink::sioPCLink()
{
    do_pclink_init(1);
}

// public wrapper around sio_ack(), sio_nak(), etc...
void sioPCLink::send_ack_byte(uint8_t  what)
{
        switch (what)
        {
        case 'a':
#ifndef ESP_PLATFORM
        sio_late_ack();
        break;
#endif
    case 'A':
        sio_ack();
        break;
    case 'N':
        sio_nak();
        break;
    case 'C':
        sio_complete();
        break;
    case 'E':
        sio_error();
        break;
        }
}

void sioPCLink::mount(int no, const char* path)
{
    if(no<1 || no>15)return;

    fps_close(no);
    memset(&device[no].parbuf, 0, sizeof(PARBUF));

#ifdef ESP_PLATFORM
    strncpy(device[no].dirname,path,1023);
#else
    if (!abs_path(path, device[no].dirname, 1024))
    {
        Debug_printf("PCLINK failed to get absolute path for \"%s\"\n", path);
        return;
    }
#endif
    device[no].dirname[1023]=0;
    device[no].cwd[0]=0;
    device[no].on = 1;

    Debug_printf("PCLINK[%d] MOUNT \"%s\"\n", no, path);
}

void sioPCLink::unmount(int no)
{
    if(no<1 || no>15)return;

    fps_close(no);
    memset(&device[no].parbuf, 0, sizeof(PARBUF));

    device[no].on = 0;
    device[no].dirname[0]=0;
    device[no].cwd[0]=0;

    Debug_printf("PCLINK[%d] UNMOUNT\n", no);
}

// Status
void sioPCLink::sio_status()
{
// # ifdef SIOTRACE
//      if (log_flag)
                Debug_printf("STATUS: %02x %02x %02x %02x\n", status[0], status[1], status[2], status[3]);
// # endif
    bus_to_computer(status, sizeof(status), false);
}

// Process SIO command
void sioPCLink::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    uchar cunit = cmdFrame.aux2 & 0x0f; /* PCLink ignores DUNIT */
    uchar cdev = SIO_DEVICEID_PCLINK;
    uchar devno = cdev >> 4; // ??? magical 6

    if (!Config.get_pclink_enabled())
        {
        Debug_println("PCLink disabled, ignoring");
                return;
        }

    Debug_println("PCLink sio_process()");

    /* cunit == 0 is init during warm reset */
    if ((cunit == 0) || device[cunit].on)
    {
        switch (cmdFrame.comnd)
        {
        case 'P':
            Debug_println("PARBLK");
            do_pclink(devno, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);
            break;
        case 'R':
            Debug_println("EXEC");
            do_pclink(devno, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);
            break;
        case 'S':       /* status */
            Debug_println("STATUS");
            pclink_ack(devno, cunit, 'A');
            get_device_status(devno, cunit, status);
            sio_status();
            break;
        case '?':       /* send hi-speed index */
            Debug_println("HIGH SPEED INDEX");
            pclink_ack(devno, cunit, 'A');
            sio_high_speed();
            break;
        default:
            pclink_ack(devno, cunit, 'N');
            break;
        }
    }
}

#endif /* BUILD_ATARI */
