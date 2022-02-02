/**
 * fnFTP implementation
 */

#include "fnFTP.h"

#include <string.h>

#include "../../include/debug.h"

#include "fnSystem.h"

/*
ftpparse(&fp,buf,len) tries to parse one line of LIST output.

The line is an array of len characters stored in buf.
It should not include the terminating CR LF; so buf[len] is typically CR.

If ftpparse() can't find a filename, it returns 0.

If ftpparse() can find a filename, it fills in fp and returns 1.
fp is a struct ftpparse, defined below.
The name is an array of fp.namelen characters stored in fp.name;
fp.name points somewhere within buf.
*/

struct ftpparse
{
    char *name; /* not necessarily 0-terminated */
    int namelen;
    int flagtrycwd;  /* 0 if cwd is definitely pointless, 1 otherwise */
    int flagtryretr; /* 0 if retr is definitely pointless, 1 otherwise */
    int sizetype;
    long size; /* number of octets */
    int mtimetype;
    time_t mtime; /* modification time */
    int idtype;
    char *id; /* not necessarily 0-terminated */
    int idlen;
};

#define FTPPARSE_SIZE_UNKNOWN 0
#define FTPPARSE_SIZE_BINARY 1 /* size is the number of octets in TYPE I */
#define FTPPARSE_SIZE_ASCII 2  /* size is the number of octets in TYPE A */

#define FTPPARSE_MTIME_UNKNOWN 0
#define FTPPARSE_MTIME_LOCAL 1        /* time is correct */
#define FTPPARSE_MTIME_REMOTEMINUTE 2 /* time zone and secs are unknown */
#define FTPPARSE_MTIME_REMOTEDAY 3    /* time zone and time of day are unknown */
/*
When a time zone is unknown, it is assumed to be GMT. You may want
to use localtime() for LOCAL times, along with an indication that the
time is correct in the local time zone, and gmtime() for REMOTE* times.
*/

#define FTPPARSE_ID_UNKNOWN 0
#define FTPPARSE_ID_FULL 1 /* unique identifier for files on this FTP server */

/* ftpparse.c, ftpparse.h: library for parsing FTP LIST responses
20001223
D. J. Bernstein, djb@cr.yp.to
http://cr.yp.to/ftpparse.html

Commercial use is fine, if you let me know what programs you're using this in.

Currently covered formats:
EPLF.
UNIX ls, with or without gid.
Microsoft FTP Service.
Windows NT FTP Server.
VMS.
WFTPD.
NetPresenz (Mac).
NetWare.
MSDOS.

Definitely not covered: 
Long VMS filenames, with information split across two lines.
NCSA Telnet FTP server. Has LIST = NLST (and bad NLST for directories).
*/

#include <time.h>

static long totai(long year, long month, long mday)
{
    long result;
    if (month >= 2)
        month -= 2;
    else
    {
        month += 10;
        --year;
    }
    result = (mday - 1) * 10 + 5 + 306 * month;
    result /= 10;
    if (result == 365)
    {
        year -= 3;
        result = 1460;
    }
    else
        result += 365 * (year % 4);
    year /= 4;
    result += 1461 * (year % 25);
    year /= 25;
    if (result == 36524)
    {
        year -= 3;
        result = 146096;
    }
    else
    {
        result += 36524 * (year % 4);
    }
    year /= 4;
    result += 146097 * (year - 5);
    result += 11017;
    return result * 86400;
}

static int flagneedbase = 1;
static time_t base; /* time() value on this OS at the beginning of 1970 TAI */
static long now;    /* current time */
static int flagneedcurrentyear = 1;
static long currentyear; /* approximation to current year */

static void initbase(void)
{
    struct tm *t;
    if (!flagneedbase)
        return;

    base = 0;
    t = gmtime(&base);
    base = -(totai(t->tm_year + 1900, t->tm_mon, t->tm_mday) + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
    /* assumes the right time_t, counting seconds. */
    /* base may be slightly off if time_t counts non-leap seconds. */
    flagneedbase = 0;
}

static void initnow(void)
{
    long day;
    long year;

    initbase();
    now = time((time_t *)0) - base;

    if (flagneedcurrentyear)
    {
        day = now / 86400;
        if ((now % 86400) < 0)
            --day;
        day -= 11017;
        year = 5 + day / 146097;
        day = day % 146097;
        if (day < 0)
        {
            day += 146097;
            --year;
        }
        year *= 4;
        if (day == 146096)
        {
            year += 3;
            day = 36524;
        }
        else
        {
            year += day / 36524;
            day %= 36524;
        }
        year *= 25;
        year += day / 1461;
        day %= 1461;
        year *= 4;
        if (day == 1460)
        {
            year += 3;
            day = 365;
        }
        else
        {
            year += day / 365;
            day %= 365;
        }
        day *= 10;
        if ((day + 5) / 306 >= 10)
            ++year;
        currentyear = year;
        flagneedcurrentyear = 0;
    }
}

/* UNIX ls does not show the year for dates in the last six months. */
/* So we have to guess the year. */
/* Apparently NetWare uses ``twelve months'' instead of ``six months''; ugh. */
/* Some versions of ls also fail to show the year for future dates. */
static long guesstai(long month, long mday)
{
    long year;
    long t;

    initnow();

    for (year = currentyear - 1; year < currentyear + 100; ++year)
    {
        t = totai(year, month, mday);
        if (now - t < 350 * 86400)
            return t;
    }
    return -1;
}

static int check(char *buf, const char *monthname)
{
    if ((buf[0] != monthname[0]) && (buf[0] != monthname[0] - 32))
        return 0;
    if ((buf[1] != monthname[1]) && (buf[1] != monthname[1] - 32))
        return 0;
    if ((buf[2] != monthname[2]) && (buf[2] != monthname[2] - 32))
        return 0;
    return 1;
}

static const char *months[12] = {
    "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

static int getmonth(char *buf, int len)
{
    int i;
    if (len == 3)
        for (i = 0; i < 12; ++i)
            if (check(buf, months[i]))
                return i;
    return -1;
}

static long getlong(char *buf, int len)
{
    long u = 0;
    while (len-- > 0)
        u = u * 10 + (*buf++ - '0');
    return u;
}

int ftpparse(struct ftpparse *fp, char *buf, int len)
{
    int i = 0;
    int j = 0;
    int state = 0;
    long size = 0;
    long year = 0;
    long month = 0;
    long mday = 0;
    long hour = 0;
    long minute = 0;

    fp->name = 0;
    fp->namelen = 0;
    fp->flagtrycwd = 0;
    fp->flagtryretr = 0;
    fp->sizetype = FTPPARSE_SIZE_UNKNOWN;
    fp->size = 0;
    fp->mtimetype = FTPPARSE_MTIME_UNKNOWN;
    fp->mtime = 0;
    fp->idtype = FTPPARSE_ID_UNKNOWN;
    fp->id = 0;
    fp->idlen = 0;

    if (len < 2) /* an empty name in EPLF, with no info, could be 2 chars */
        return 0;

    switch (*buf)
    {
    /* see http://pobox.com/~djb/proto/eplf.txt */
    /* "+i8388621.29609,m824255902,/,\tdev" */
    /* "+i8388621.44468,m839956783,r,s10376,\tRFCEPLF" */
    case '+':
        i = 1;
        for (j = 1; j < len; ++j)
        {
            if (buf[j] == 9)
            {
                fp->name = buf + j + 1;
                fp->namelen = len - j - 1;
                return 1;
            }
            if (buf[j] == ',')
            {
                switch (buf[i])
                {
                case '/':
                    fp->flagtrycwd = 1;
                    break;
                case 'r':
                    fp->flagtryretr = 1;
                    break;
                case 's':
                    fp->sizetype = FTPPARSE_SIZE_BINARY;
                    fp->size = getlong(buf + i + 1, j - i - 1);
                    break;
                case 'm':
                    fp->mtimetype = FTPPARSE_MTIME_LOCAL;
                    initbase();
                    fp->mtime = base + getlong(buf + i + 1, j - i - 1);
                    break;
                case 'i':
                    fp->idtype = FTPPARSE_ID_FULL;
                    fp->id = buf + i + 1;
                    fp->idlen = j - i - 1;
                }
                i = j + 1;
            }
        }
        return 0;

    /* UNIX-style listing, without inum and without blocks */
    /* "-rw-r--r--   1 root     other        531 Jan 29 03:26 README" */
    /* "dr-xr-xr-x   2 root     other        512 Apr  8  1994 etc" */
    /* "dr-xr-xr-x   2 root     512 Apr  8  1994 etc" */
    /* "lrwxrwxrwx   1 root     other          7 Jan 25 00:17 bin -> usr/bin" */
    /* Also produced by Microsoft's FTP servers for Windows: */
    /* "----------   1 owner    group         1803128 Jul 10 10:18 ls-lR.Z" */
    /* "d---------   1 owner    group               0 May  9 19:45 Softlib" */
    /* Also WFTPD for MSDOS: */
    /* "-rwxrwxrwx   1 noone    nogroup      322 Aug 19  1996 message.ftp" */
    /* Also NetWare: */
    /* "d [R----F--] supervisor            512       Jan 16 18:53    login" */
    /* "- [R----F--] rhesus             214059       Oct 20 15:27    cx.exe" */
    /* Also NetPresenz for the Mac: */
    /* "-------r--         326  1391972  1392298 Nov 22  1995 MegaPhone.sit" */
    /* "drwxrwxr-x               folder        2 May 10  1996 network" */
    case 'b':
    case 'c':
    case 'd':
    case 'l':
    case 'p':
    case 's':
    case '-':

        if (*buf == 'd')
            fp->flagtrycwd = 1;
        if (*buf == '-')
            fp->flagtryretr = 1;
        if (*buf == 'l')
            fp->flagtrycwd = fp->flagtryretr = 1;

        state = 1;
        i = 0;
        for (j = 1; j < len; ++j)
            if ((buf[j] == ' ') && (buf[j - 1] != ' '))
            {
                switch (state)
                {
                case 1: /* skipping perm */
                    state = 2;
                    break;
                case 2: /* skipping nlink */
                    state = 3;
                    if ((j - i == 6) && (buf[i] == 'f')) /* for NetPresenz */
                        state = 4;
                    break;
                case 3: /* skipping uid */
                    state = 4;
                    break;
                case 4: /* getting tentative size */
                    size = getlong(buf + i, j - i);
                    state = 5;
                    break;
                case 5: /* searching for month, otherwise getting tentative size */
                    month = getmonth(buf + i, j - i);
                    if (month >= 0)
                        state = 6;
                    else
                        size = getlong(buf + i, j - i);
                    break;
                case 6: /* have size and month */
                    mday = getlong(buf + i, j - i);
                    state = 7;
                    break;
                case 7: /* have size, month, mday */
                    if ((j - i == 4) && (buf[i + 1] == ':'))
                    {
                        hour = getlong(buf + i, 1);
                        minute = getlong(buf + i + 2, 2);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                        initbase();
                        fp->mtime = base + guesstai(month, mday) + hour * 3600 + minute * 60;
                    }
                    else if ((j - i == 5) && (buf[i + 2] == ':'))
                    {
                        hour = getlong(buf + i, 2);
                        minute = getlong(buf + i + 3, 2);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                        initbase();
                        fp->mtime = base + guesstai(month, mday) + hour * 3600 + minute * 60;
                    }
                    else if (j - i >= 4)
                    {
                        year = getlong(buf + i, j - i);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
                        initbase();
                        fp->mtime = base + totai(year, month, mday);
                    }
                    else
                        return 0;
                    fp->name = buf + j + 1;
                    fp->namelen = len - j - 1;
                    state = 8;
                    break;
                case 8: /* twiddling thumbs */
                    break;
                }
                i = j + 1;
                while ((i < len) && (buf[i] == ' '))
                    ++i;
            }

        if (state != 8)
            return 0;

        fp->size = size;
        fp->sizetype = FTPPARSE_SIZE_BINARY;

        if (*buf == 'l')
            for (i = 0; i + 3 < fp->namelen; ++i)
                if (fp->name[i] == ' ')
                    if (fp->name[i + 1] == '-')
                        if (fp->name[i + 2] == '>')
                            if (fp->name[i + 3] == ' ')
                            {
                                fp->namelen = i;
                                break;
                            }

        /* eliminate extra NetWare spaces */
        if ((buf[1] == ' ') || (buf[1] == '['))
            if (fp->namelen > 3)
                if (fp->name[0] == ' ')
                    if (fp->name[1] == ' ')
                        if (fp->name[2] == ' ')
                        {
                            fp->name += 3;
                            fp->namelen -= 3;
                        }

        return 1;
    }

    /* MultiNet (some spaces removed from examples) */
    /* "00README.TXT;1      2 30-DEC-1996 17:44 [SYSTEM] (RWED,RWED,RE,RE)" */
    /* "CORE.DIR;1          1  8-SEP-1996 16:09 [SYSTEM] (RWE,RWE,RE,RE)" */
    /* and non-MutliNet VMS: */
    /* "CII-MANUAL.TEX;1  213/216  29-JAN-1996 03:33:12  [ANONYMOU,ANONYMOUS]   (RWED,RWED,,)" */
    for (i = 0; i < len; ++i)
        if (buf[i] == ';')
            break;
    if (i < len)
    {
        fp->name = buf;
        fp->namelen = i;
        if (i > 4)
            if (buf[i - 4] == '.')
                if (buf[i - 3] == 'D')
                    if (buf[i - 2] == 'I')
                        if (buf[i - 1] == 'R')
                        {
                            fp->namelen -= 4;
                            fp->flagtrycwd = 1;
                        }
        if (!fp->flagtrycwd)
            fp->flagtryretr = 1;
        while (buf[i] != ' ')
            if (++i == len)
                return 0;
        while (buf[i] == ' ')
            if (++i == len)
                return 0;
        while (buf[i] != ' ')
            if (++i == len)
                return 0;
        while (buf[i] == ' ')
            if (++i == len)
                return 0;
        j = i;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        mday = getlong(buf + i, j - i);
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        month = getmonth(buf + i, j - i);
        if (month < 0)
            return 0;
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ' ')
            if (++j == len)
                return 0;
        year = getlong(buf + i, j - i);
        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ':')
            if (++j == len)
                return 0;
        hour = getlong(buf + i, j - i);
        while (buf[j] == ':')
            if (++j == len)
                return 0;
        i = j;
        while ((buf[j] != ':') && (buf[j] != ' '))
            if (++j == len)
                return 0;
        minute = getlong(buf + i, j - i);

        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
        initbase();
        fp->mtime = base + totai(year, month, mday) + hour * 3600 + minute * 60;

        return 1;
    }

    /* MSDOS format */
    /* 04-27-00  09:09PM       <DIR>          licensed */
    /* 07-18-00  10:16AM       <DIR>          pub */
    /* 04-14-00  03:47PM                  589 readme.htm */
    if ((*buf >= '0') && (*buf <= '9'))
    {
        i = 0;
        j = 0;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        month = getlong(buf + i, j - i) - 1;
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        mday = getlong(buf + i, j - i);
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ' ')
            if (++j == len)
                return 0;
        year = getlong(buf + i, j - i);
        if (year < 50)
            year += 2000;
        if (year < 1000)
            year += 1900;
        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ':')
            if (++j == len)
                return 0;
        hour = getlong(buf + i, j - i);
        while (buf[j] == ':')
            if (++j == len)
                return 0;
        i = j;
        while ((buf[j] != 'A') && (buf[j] != 'P'))
            if (++j == len)
                return 0;
        minute = getlong(buf + i, j - i);
        if (hour == 12)
            hour = 0;
        if (buf[j] == 'A')
            if (++j == len)
                return 0;
        if (buf[j] == 'P')
        {
            hour += 12;
            if (++j == len)
                return 0;
        }
        if (buf[j] == 'M')
            if (++j == len)
                return 0;

        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        if (buf[j] == '<')
        {
            fp->flagtrycwd = 1;
            while (buf[j] != ' ')
                if (++j == len)
                    return 0;
        }
        else
        {
            i = j;
            while (buf[j] != ' ')
                if (++j == len)
                    return 0;
            fp->size = getlong(buf + i, j - i);
            fp->sizetype = FTPPARSE_SIZE_BINARY;
            fp->flagtryretr = 1;
        }
        while (buf[j] == ' ')
            if (++j == len)
                return 0;

        fp->name = buf + j;
        fp->namelen = len - j;

        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
        initbase();
        fp->mtime = base + totai(year, month, mday) + hour * 3600 + minute * 60;

        return 1;
    }

    /* Some useless lines, safely ignored: */
    /* "Total of 11 Files, 10966 Blocks." (VMS) */
    /* "total 14786" (UNIX) */
    /* "DISK$ANONFTP:[ANONYMOUS]" (VMS) */
    /* "Directory DISK$PCSA:[ANONYM]" (VMS) */

    return 0;
}

fnFTP::fnFTP()
{
    _stor = false;
    _expect_control_response = false;
    control = new fnTcpClient();
    data = new fnTcpClient();
}

fnFTP::~fnFTP()
{
    if (control != nullptr)
        delete control;
    if (data != nullptr)
        delete data;
}

bool fnFTP::login(string _username, string _password, string _hostname, unsigned short _port)
{
    username = _username;
    password = _password;
    hostname = _hostname;
    control_port = _port;

    Debug_printf("fnFTP::login(%s,%u)\n", hostname.c_str(), control_port);

    // Attempt to open control socket.
    if (!control->connect(hostname.c_str(), control_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not log in, errno = %u\n", errno);
        _statusCode = 421; // service not available
        return true;
    }

    Debug_printf("Connected, waiting for 220.\n");

    // Wait for banner.
    if (parse_response())
    {
        Debug_printf("Timed out waiting for 220 banner.\n");
        return true;
    }

    Debug_printf("Sending USER.\n");

    if (is_positive_completion_reply() && is_connection())
    {
        // send username.
        USER();
    }
    else
    {
        Debug_printf("Could not send username. Response was: %s\n", controlResponse.c_str());
        return true;
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 331.\n");
        return true;
    }

    Debug_printf("Sending PASS.\n");

    if (is_positive_intermediate_reply() && is_authentication())
    {
        // Send password
        PASS();
    }
    else
    {
        Debug_printf("Could not send password. Response was: %s\n", controlResponse.c_str());
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 230.\n");
        return true;
    }

    if (is_positive_completion_reply() && is_authentication())
    {
        Debug_printf("Logged in successfully. Setting type.\n");
        TYPE();
    }
    else
    {
        Debug_printf("Could not finish log in. Response was: %s\n", controlResponse.c_str());
        return true;
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 200.\n");
        return true;
    }

    if (is_positive_completion_reply() && is_syntax())
    {
        Debug_printf("Logged in\n");
    }
    else
    {
        Debug_printf("Could not set image type. Ignoring.\n");
    }

    return false;
}

bool fnFTP::logout()
{
    Debug_printf("fnFTP::logout()\n");
    if (!control->connected())
    {
        Debug_printf("Logout called when not connected.\n");
        return false;
    }

    if (data->connected())
    {
        ABOR();
        parse_response(); // Ignored.
        data->stop();
    }

    QUIT();

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 221.\n");
    }

    control->stop();

    return false;
}

bool fnFTP::open_file(string path, bool stor)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_file(%s) attempted while not logged in. Aborting.\n", path.c_str());
        return true;
    }

    if (get_data_port())
    {
        Debug_printf("fnFTP::get_data_port() - could not get data port. Aborting.\n");
        return true;
    }

    // Do command
    if (stor == true)
    {
        STOR(path);
    }
    else
    {
        RETR(path);
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 150 response.\n");
        return true;
    }

    if (is_positive_preliminary_reply() && is_filesystem_related())
    {
        _stor = stor;
        _expect_control_response = !stor;
        Debug_printf("Server began transfer.\n");
        return false;
    }
    else
    {
        Debug_printf("Server could not begin transfer. Response was: %s\n", controlResponse.c_str());
        return true;
    }
}

bool fnFTP::open_directory(string path, string pattern)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_directory(%s%s) attempted while not logged in. Aborting.\n", path.c_str(), pattern.c_str());
        return true;
    }

    if (get_data_port())
    {
        Debug_printf("fnFTP::open_directory(%s%s) could not get data port, aborting.\n", path.c_str(), pattern.c_str());
        return true;
    }

    // perform LIST
    LIST(path, pattern);

    if (parse_response())
    {
        Debug_printf("fnFTP::open_directory(%s%s) Timed out waiting for 150 response.\n", path.c_str(), pattern.c_str());
        return true;
    }

    Debug_printf("fnFTP::open_directory(%s%s) - %s\n", path.c_str(), pattern.c_str(), controlResponse.c_str());

    if (is_positive_preliminary_reply() && is_filesystem_related())
    {
        // Do nothing.
        Debug_printf("Got our 150\n");
    }
    else
    {
        Debug_printf("Didn't get our 150\n");
        return true;
    }

    uint8_t buf[256];

    // if (buf == nullptr)
    // {
    //     Debug_printf("fnFTP::open_directory() - Could not allocate 2048 bytes.\n");
    //     return true;
    // }

    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    bool got_response = false;
    // Retrieve listing into buffer.
    do 
    {
        if (data->available() == 0)
        {
            if (--tmout_counter == 0)
            {
                // no data & no control message
                Debug_printf("fnFTP::open_directory - Timeout\n");
                break;
            }
            fnSystem.delay(50); // wait for more data or control message
        }
        if (data->available())
        {
            Debug_printf("Retrieving directory list\n");
            while (data->available())
            {
                int len = data->available();
                memset(buf, 0, sizeof(buf));
                int num_read = data->read(buf, len > sizeof(buf) ? sizeof(buf) : len);
                dirBuffer << string((const char *)buf, num_read);
            }
            tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
        }
        if (got_response == false && control->available())
        {
            got_response = !parse_response();
        }
    } while (data->available() > 0 || data->connected());

    if (data->connected()) // still connected, but data retrieval timed out
        data->stop();

    if (tmout_counter == 0 || (got_response == false && parse_response()))
    {
        Debug_printf("fnFTP::open_directory(%s%s) Timed out waiting for 226 response.\n", path.c_str(), pattern.c_str());
        return true;
    }

    return false; // all good.
}

bool fnFTP::read_directory(string &name, long &filesize, bool &is_dir)
{
    string line;
    struct ftpparse parse;

    getline(dirBuffer, line);

    if (line.empty())
        return true;

    Debug_printf("fnFTP::read_directory - %s\n",line.c_str());
    line = line.substr(0, line.size() - 1);
    ftpparse(&parse, (char *)line.c_str(), line.length());
    name = string(parse.name ? parse.name : "???");
    filesize = parse.size;
    is_dir = (parse.flagtrycwd == 1);
    Debug_printf("Name: %s filesize: %lu\n", name.c_str(), filesize);
    return dirBuffer.eof();
}

bool fnFTP::read_file(uint8_t *buf, unsigned short len)
{
    Debug_printf("fnFTP::read_file(%p, %u)\n", buf, len);
    if (!data->connected() && data->available() == 0)
    {
        Debug_printf("fnFTP::read_file(%p,%u) - data socket not connected, aborting.\n", buf, len);
        return true;
    }
    return len != data->read(buf, len);
}

bool fnFTP::write_file(uint8_t *buf, unsigned short len)
{
    Debug_printf("fnFTP::write_file(%p,%u)\n", buf, len);
    if (!data->connected())
    {
        Debug_printf("fnFTP::write_file(%p,%u) - data socket not connected, aborting.\n", buf, len);
        return true;
    }

    return len != data->write(buf, len);
}

bool fnFTP::close()
{
    bool res = false;
    Debug_printf("fnFTP::close()\n");
    if (_stor)
    {
        if (data->connected())
        {
            data->close();
        }
        if (parse_response())
        {
            Debug_printf("Timed out waiting for 226.\n");
            res = true;
        }
    }
    _stor = false;
    _expect_control_response = false;
    return res;
}

int fnFTP::status()
{
    return _statusCode;
}

int fnFTP::data_available()
{
    return data->available();
}

bool fnFTP::data_connected()
{
    if (_expect_control_response && control->available())
        _expect_control_response = parse_response();
    return _expect_control_response || data->connected();
}

/** FTP UTILITY FUNCTIONS **********************************************************************/

bool fnFTP::parse_response()
{
    char respBuf[384];  // room for control message incl. file path and file size
    int num_read = 0;
    bool multi_line = false;

    controlResponse.clear();

    while(true)
    {
        num_read = read_response_line(respBuf, sizeof(respBuf));
        if (num_read < 0)
        {
            // Timeout
            _statusCode = 421;  // service not available
            return true;        // error
        }
        if (num_read >= 4)
        {
            if (isdigit(respBuf[0]) && isdigit(respBuf[1]) && isdigit(respBuf[2]))
            {
                if (respBuf[3] == ' ')  // done, got NNN<space>
                    break;
                if (respBuf[3] == '-')
                {
                    // head of multi-line response
                    multi_line = true;
                    continue;
                }
            }
        }
        if (multi_line) // ignore body of multi-line response
            continue;
        // error - nothing above
        _statusCode = 501;  //syntax error
        return true;        // error
    }

    // update control response and status code
    controlResponse = string((char *)respBuf, num_read);
    _statusCode = atoi(controlResponse.substr(0, 3).c_str());
    Debug_printf("fnFTP::parse_response() - %d, \"%s\"\n", _statusCode, controlResponse.c_str());

    return false; // ok
}

int fnFTP::read_response_line(char *buf, int buflen)
{
    int num_read = 0;
    int c;
    int tmout_counter = 1 + FTP_TIMEOUT / 50;

    while(true)
    {
        if (control->available() == 0)
        {
            if (--tmout_counter == 0)
            {
                Debug_printf("fnFTP::read_response_line() - Timeout waiting response\n");
                return -1;
            }
            fnSystem.delay(50);
            continue;
        }

        c = control->read(); // singe byte
        if (c < 0)
            break;  // read error

        if(c == '\n' || c == '\r') // almost done, got line
        {
            // eat all line terminators
            if (control->available())
            {
                // test next byte
                c = control->peek();
                if (c == '\n' || c== '\r')
                    continue; // read it
            }
            break; // done
        }
        // store char, ignore rest of too long response
        if (num_read < buflen)
            buf[num_read++] = (char) c;
        tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
    }
    return num_read;
}

bool fnFTP::get_data_port()
{
    size_t port_pos_beg, port_pos_end;

    Debug_printf("fnFTP::get_data_port()\n");

    EPSV();

    Debug_printf("Did EPSV, getting response.\n");

    if (parse_response())
    {
        Debug_printf("Timed out waiting for response.\n");
        return true;
    }

    if (is_negative_permanent_reply())
    {
        Debug_printf("Server unable to reserve port. Response was: %s\n", controlResponse.c_str());
        return true;
    }

    // At this point, we have a port mapping trapped in (|||1234|), peel it out of there.
    port_pos_beg = controlResponse.find_first_of("|") + 3;
    port_pos_end = controlResponse.find_last_of("|");
    data_port = atoi(controlResponse.substr(port_pos_beg, port_pos_end).c_str());

    Debug_printf("Server gave us data port: %u\n", data_port);

    // Go ahead and connect to data port, so that control port is unblocked, if it's blocked.
    if (!data->connect(hostname.c_str(), data_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not open data port %u, errno = %u\n", data_port, errno);
        return true;
    }
    else
    {
        Debug_printf("Data port %u opened.\n", data_port);
    }

    return false;
}

/** FTP VERBS **********************************************************************************/

void fnFTP::USER()
{
    control->write("USER " + username + "\r\n");
}

void fnFTP::PASS()
{
    control->write("PASS " + password + "\r\n");
}

void fnFTP::TYPE()
{
    Debug_printf("fnFTP::TYPE()\n");
    control->write("TYPE I\r\n");
}

void fnFTP::QUIT()
{
    Debug_printf("fnFTP::QUIT()\n");
    control->write("QUIT\r\n");
}

void fnFTP::EPSV()
{
    Debug_printf("fnFTP::EPSV()\n");
    control->write("EPSV\r\n");
}

void fnFTP::RETR(string path)
{
    Debug_printf("fnFTP::RETR(%s)\n",path.c_str());
    control->write("RETR " + path + "\r\n");
}

void fnFTP::CWD(string path)
{
    Debug_printf("fnFTP::CWD(%s)\n",path.c_str());
    control->write("CWD " + path + "\r\n");
}

void fnFTP::LIST(string path, string pattern)
{
    Debug_printf("fnFTP::LIST(%s,%s)\n",path.c_str(),pattern.c_str());
    control->write("LIST " + path + pattern + "\r\n");
}

void fnFTP::ABOR()
{
    Debug_printf("fnFTP::ABOR()\n");
    control->write("ABOR\r\n");
}

void fnFTP::STOR(string path)
{
    Debug_printf("fnFTP::STOR(%s)\n",path.c_str());
    control->write("STOR " + path + "\r\n");
}