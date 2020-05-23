#include "networkProtocolTNFS.h"
#include "../../include/debug.h"

// Function that matches input str with
// given wildcard pattern
bool strmatch(char str[], char pattern[],
              int n, int m)
{
    // empty pattern can only match with
    // empty string
    if (m == 0)
        return (n == 0);

    // lookup table for storing results of
    // subproblems
    bool lookup[n + 1][m + 1];

    // initailze lookup table to false
    memset(lookup, false, sizeof(lookup));

    // empty pattern can match with empty string
    lookup[0][0] = true;

    // Only '*' can match with empty string
    for (int j = 1; j <= m; j++)
        if (pattern[j - 1] == '*')
            lookup[0][j] = lookup[0][j - 1];

    // fill the table in bottom-up fashion
    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            // Two cases if we see a '*'
            // a) We ignore ‘*’ character and move
            //    to next  character in the pattern,
            //     i.e., ‘*’ indicates an empty sequence.
            // b) '*' character matches with ith
            //     character in input
            if (pattern[j - 1] == '*')
                lookup[i][j] = lookup[i][j - 1] ||
                               lookup[i - 1][j];

            // Current characters are considered as
            // matching in two cases
            // (a) current character of pattern is '?'
            // (b) characters actually match
            else if (pattern[j - 1] == '?' ||
                     str[i - 1] == pattern[j - 1])
                lookup[i][j] = lookup[i - 1][j - 1];

            // If characters don't match
            else
                lookup[i][j] = false;
        }
    }

    return lookup[n][m];
}

networkProtocolTNFS::networkProtocolTNFS()
{
}

networkProtocolTNFS::~networkProtocolTNFS()
{
}

bool networkProtocolTNFS::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    directory = urlParser->path.substr(0, urlParser->path.find_last_of("/") - 1);
    filename = urlParser->path.substr(urlParser->path.find_last_of("/") + 1);

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (!tnfs_mount(&mountInfo))
        return false; // error

    if (cmdFrame->aux1 == 6)
    {
        // Directory open.
        string dirPath = urlParser->path.substr(0, urlParser->path.find_last_of("/") - 1);

        if (!tnfs_stat(&mountInfo, &fileStat, dirPath.c_str()))
            return false;

        if (!tnfs_opendir(&mountInfo, dirPath.c_str()))
            return false; // error
    }
    else
    {
        // File. Open file handle.
        int mode = 1;
        int create_perms = 0;

        switch (cmdFrame->aux1)
        {
        case 4:
            mode = O_RDONLY;
            break;
        case 8:
            mode = O_WRONLY | O_CREAT;
            create_perms = 0x1FF;
            break;
        case 9:
            mode = O_WRONLY | O_APPEND | O_CREAT;
            create_perms = 0x1FF;
            break;
        case 12:
            mode = O_RDWR | O_CREAT;
            create_perms = 0x1FF;
            break;
        }

        if (!tnfs_stat(&mountInfo, &fileStat, urlParser->path.c_str()))
            return false; // error

        if (!tnfs_open(&mountInfo, urlParser->path.c_str(), mode, create_perms, &fileHandle))
            return false; // error
    }

    return true;
}

bool networkProtocolTNFS::close()
{
    if (mountInfo.dir_handle != 0)
        tnfs_closedir(&mountInfo);

    if (fileHandle != 0)
        tnfs_close(&mountInfo, fileHandle);

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return false;
}

bool networkProtocolTNFS::read(byte *rx_buf, unsigned short len)
{
    if (mountInfo.dir_handle != 0) // are we reading directory?
    {
        char tmp[256];
        string wildcard = filename;

    nextEntry:
        if (tnfs_readdir(&mountInfo, (char *)tmp, len) != 0)
            return true; // error

        if (strmatch(tmp, (char *)wildcard.c_str(), strlen(tmp), wildcard.length()))
            strcpy((char *)rx_buf, tmp);
        else
            goto nextEntry;
    }
    else
    {
        // Reading from a file
        if (!tnfs_read(&mountInfo,fileHandle,rx_buf,len,NULL))
        {
            return true;
        }
        else
        {
            fileStat.filesize-=len;
        }
    }
    return false;
}

bool networkProtocolTNFS::write(byte *tx_buf, unsigned short len)
{
    if (!tnfs_write(&mountInfo,fileHandle,tx_buf,len,NULL))
        return true;
        
    return false;
}

bool networkProtocolTNFS::status(byte *status_buf)
{
    unsigned short available_bytes = (fileStat.filesize > 65535 ? 65535 : fileStat.filesize);
    status_buf[0] = available_bytes & 0xFF;
    status_buf[1] = available_bytes >> 8;
    status_buf[2] = 1;
    status_buf[3] = 1;
    return false;
}

bool networkProtocolTNFS::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolTNFS::special_supported_00_command(unsigned char comnd)
{
    return false;
}